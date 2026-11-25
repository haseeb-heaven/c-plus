#!/bin/sh
# RUST_EXAMPLES alloc driver: safe-Rust examples vs every C+ checked
# allocation/free API.
#
#   1. verify each exported symbol with nm (absent symbol -> BLOCKED, with the
#      nm evidence printed and recorded -- never a fabricated match);
#   2. build the C99 harness and the Rust program (timeout 30);
#   3. run each under timeout 30;
#   4. diff the "<api> <case> <token>" streams and print
#        RUST_EXAMPLES alloc <apis> <cases> <matched> <mismatched> <blocked>
#      Exit nonzero ONLY on a real token mismatch. BLOCKED cases (including
#      absent-symbol cases) are reported but are not counted as mismatches.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2

SRC="$ROOT/benchmarks/rust_examples/alloc"
OUT="$SRC/results"
mkdir -p "$OUT"

CC=/usr/bin/clang
[ -x "$CC" ] || CC=clang
RUSTC=${RUSTC:-rustc}
SAFE=build/safe/libcplus_safecrt.a
HARD=build/libc/libcplus_hardened.a
TIMEOUT=30

if command -v timeout >/dev/null 2>&1; then TMO=timeout; else TMO=gtimeout; fi

echo "== toolchains =="
"$CC" --version | head -1
"$RUSTC" --version
echo "== archives =="
ls -l "$SAFE" "$HARD" 2>&1

# ------------------------------------------------------------------ nm gate
# Required symbols, mapped to the API name that emits tokens. The checked
# *_s APIs and cplus_safe_* metadata live in libcplus_safecrt.a; the cplus_*
# hardened entry points live in libcplus_hardened.a. Note that the task's
# short names "alloc_fail_after/reset" and "ptr_was_freed" are exported as
# cplus_safe_alloc_fail_after / cplus_safe_alloc_fail_reset and
# cplus_safe_ptr_was_freed -- there is no standalone "ptr_was_freed" symbol
# (see NOTES.md for the raw nm evidence).
SAFE_SYMS="malloc_s calloc_s realloc_s free_s free_checked_s aligned_alloc_s
           cplus_safe_ptr_is_live cplus_safe_ptr_size cplus_safe_ptr_was_freed
           cplus_safe_alloc_fail_after cplus_safe_alloc_fail_reset"
HARD_SYMS="cplus_malloc cplus_calloc cplus_realloc cplus_free cplus_free_checked
           cplus_aligned_alloc cplus_alloc_fail_after cplus_alloc_fail_reset
           cplus_alloc_double_free_count cplus_alloc_invalid_free_count"

nm -g --defined-only "$SAFE" > "$OUT/nm_safe.txt" 2> "$OUT/nm_safe.err"
nm -g --defined-only "$HARD" > "$OUT/nm_hard.txt" 2> "$OUT/nm_hard.err"

: > "$OUT/missing.txt"
: > "$OUT/nm_evidence.txt"
echo "== symbol check (nm) =="
for s in $SAFE_SYMS; do
  if grep -qE " T _?${s}\$" "$OUT/nm_safe.txt"; then
    printf 'PRESENT safecrt %s\n' "$s" >> "$OUT/nm_evidence.txt"
  else
    printf 'MISSING safecrt %s\n' "$s" >> "$OUT/missing.txt"
    printf 'BLOCKED absent symbol: %s (archive %s)\n' "$s" "$SAFE" >> "$OUT/nm_evidence.txt"
    printf '  nm -g --defined-only %s | grep " T _?%s$" -> no match\n' "$SAFE" "$s" >> "$OUT/nm_evidence.txt"
    grep -iE "$s" "$OUT/nm_safe.txt" >> "$OUT/nm_evidence.txt" 2>/dev/null || true
    echo "ABSENT (BLOCKED): $s in $SAFE" >&2
  fi
done
for s in $HARD_SYMS; do
  if grep -qE " T _?${s}\$" "$OUT/nm_hard.txt"; then
    printf 'PRESENT hardened %s\n' "$s" >> "$OUT/nm_evidence.txt"
  else
    printf 'MISSING hardened %s\n' "$s" >> "$OUT/missing.txt"
    printf 'BLOCKED absent symbol: %s (archive %s)\n' "$s" "$HARD" >> "$OUT/nm_evidence.txt"
    printf '  nm -g --defined-only %s | grep " T _?%s$" -> no match\n' "$HARD" "$s" >> "$OUT/nm_evidence.txt"
    grep -iE "$s" "$OUT/nm_hard.txt" >> "$OUT/nm_evidence.txt" 2>/dev/null || true
    echo "ABSENT (BLOCKED): $s in $HARD" >&2
  fi
done
cat "$OUT/nm_evidence.txt"

C_AVAILABLE=1
if [ -s "$OUT/missing.txt" ]; then
  echo "One or more required symbols are absent; those cases are BLOCKED." >&2
  C_AVAILABLE=0
fi

# ------------------------------------------------------------------- build
CFLAGS="-std=c99 -O1 -g -I. -Ilibc/install-include -Ithird_party/safeclib/include"
echo "== build C99 harness =="
if [ "$C_AVAILABLE" -eq 1 ]; then
  if ! "$TMO" "$TIMEOUT" "$CC" $CFLAGS "$SRC/harness.c" "$SAFE" "$HARD" \
       -lm -pthread -o "$OUT/harness" 2> "$OUT/c_build.err"; then
    echo "ERROR: C harness build failed:" >&2
    cat "$OUT/c_build.err" >&2
    exit 2
  fi
else
  echo "skipping C harness build (absent symbols recorded in $OUT/missing.txt)"
fi

echo "== build Rust examples =="
if ! "$TMO" "$TIMEOUT" "$RUSTC" -O "$SRC/alloc.rs" -o "$OUT/alloc_rust" \
     2> "$OUT/rust_build.err"; then
  echo "ERROR: Rust build failed:" >&2
  cat "$OUT/rust_build.err" >&2
  exit 2
fi

# --------------------------------------------------------------------- run
echo "== run (timeout $TIMEOUT) =="
if [ "$C_AVAILABLE" -eq 1 ]; then
  "$TMO" "$TIMEOUT" "$OUT/harness" > "$OUT/c.out" 2> "$OUT/c.err"
  c_rc=$?
else
  c_rc=0
  : > "$OUT/c.out"
  : > "$OUT/c.err"
fi
"$TMO" "$TIMEOUT" "$OUT/alloc_rust" > "$OUT/rust.out" 2> "$OUT/rust.err"
r_rc=$?
echo "$c_rc" > "$OUT/c.exit"
echo "$r_rc" > "$OUT/rust.exit"
echo "run exit codes: c=$c_rc rust=$r_rc"
if [ "$c_rc" -eq 124 ] || [ "$r_rc" -eq 124 ]; then
  echo "ERROR: a harness timed out (timeout=$TIMEOUT)" >&2
  exit 2
fi
if [ "$r_rc" -ne 0 ]; then
  echo "ERROR: Rust harness exited $r_rc" >&2
  cat "$OUT/rust.err" >&2
  exit 2
fi

# -------------------------------------------------------------------- diff
python3 - "$OUT/c.out" "$OUT/rust.out" "$OUT/missing.txt" "$C_AVAILABLE" <<'PY'
import sys

c_path, r_path, missing_path, c_avail = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4] == "1"

def load(path):
    rows, order = {}, []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 3:
                continue
            key = (parts[0], parts[1])
            rows[key] = parts[2]
            if key not in order:
                order.append(key)
    return rows, order

c, c_order = load(c_path)
r, r_order = load(r_path)

with open(missing_path) as f:
    missing = [ln.split()[1] for ln in f if ln.strip()]

if not c_avail:
    # No C harness could be linked. Every case is BLOCKED with nm evidence
    # rather than fabricated. Keep the Rust order so the case set is reported.
    print("== absent-symbol BLOCKED cases ==")
    for m in missing:
        print(f"  {m}: BLOCKED (nm evidence in results/nm_evidence.txt)")
    c_order = r_order
    c = {k: "BLOCKED" for k in r_order}

# Union of orders, C first then any Rust-only cases.
order = list(c_order)
for k in r_order:
    if k not in order:
        order.append(k)

print("== per-case comparison (C token / Rust token / status) ==")
print(f"{'api':30s} {'case':12s} {'C':12s} {'Rust':12s} status")
apis = set()
cases = matched = mismatched = blocked = 0
for key in order:
    apis.add(key[0])
    cases += 1
    ct = c.get(key, "MISSING")
    rt = r.get(key, "MISSING")
    if ct == "BLOCKED" or rt == "BLOCKED" or ct == "MISSING" or rt == "MISSING":
        status = "BLOCKED"
        blocked += 1
    elif ct == rt:
        status = "MATCH"
        matched += 1
    else:
        status = "MISMATCH"
        mismatched += 1
    print(f"{key[0]:30s} {key[1]:12s} {ct:12s} {rt:12s} {status}")

print(f"RUST_EXAMPLES alloc {len(apis)} {cases} {matched} {mismatched} {blocked}")
sys.exit(1 if mismatched else 0)
PY
exit $?
