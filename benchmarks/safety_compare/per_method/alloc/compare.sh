#!/bin/sh
# Per-method memory-safety comparison driver: C+ checked alloc APIs vs Rust.
#
# Compiles both harnesses with the pinned toolchains, runs each under a 30s
# timeout, diffs the "<method> <case> <token>" lines, and prints
#   METHOD_ALLOC <cases> <matched> <mismatched> <blocked>
# Exits nonzero only on a real token mismatch (BLOCKED cases are reported but
# are not counted as mismatches).
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
cd "$ROOT" || exit 2

SRC="$ROOT/benchmarks/safety_compare/per_method/alloc"
# Keep every generated artifact inside this agent's owned directory.
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
ls -l "$SAFE" "$HARD"

# Symbol verification (both archives must provide the checked allocator API).
echo "== symbol check =="
for s in malloc_s calloc_s realloc_s free_s free_checked_s aligned_alloc_s \
         cplus_safe_ptr_is_live cplus_safe_ptr_size cplus_safe_alloc_fail_after \
         cplus_safe_alloc_fail_reset; do
  if ! nm -g --defined-only "$SAFE" 2>/dev/null | grep -qE " T _?${s}\$"; then
    echo "MISSING SAFECRT symbol: $s" >&2; exit 2
  fi
done
for s in cplus_malloc cplus_calloc cplus_realloc cplus_free cplus_free_checked \
         cplus_aligned_alloc; do
  if ! nm -g --defined-only "$HARD" 2>/dev/null | grep -qE " T _?${s}\$"; then
    echo "MISSING HARDENED symbol: $s" >&2; exit 2
  fi
done
echo "all required symbols present"

# Build.
CFLAGS="-std=c99 -O1 -g -I. -Ilibc/install-include -Ithird_party/safeclib/include"
echo "== build C99 harness =="
"$CC" $CFLAGS "$SRC/alloc_harness.c" "$SAFE" "$HARD" -lm -pthread \
  -o "$OUT/alloc_harness" || exit 2
echo "== build Rust harness =="
"$RUSTC" -O "$SRC/alloc.rs" -o "$OUT/alloc_rust" || exit 2

# Run under timeout; record exit status.
"$TMO" "$TIMEOUT" "$OUT/alloc_harness" > "$OUT/c.out" 2> "$OUT/c.err"
c_rc=$?
"$TMO" "$TIMEOUT" "$OUT/alloc_rust" > "$OUT/rust.out" 2> "$OUT/rust.err"
r_rc=$?
echo "$c_rc" > "$OUT/c.exit"
echo "$r_rc" > "$OUT/rust.exit"
echo "run exit codes: c=$c_rc rust=$r_rc"
if [ "$c_rc" -ne 0 ] || [ "$r_rc" -ne 0 ]; then
  echo "ERROR: a harness did not complete under timeout 30 (timeout=124)" >&2
  exit 2
fi

# Diff token streams.
python3 - "$OUT/c.out" "$OUT/rust.out" <<'PY'
import sys

def load(path):
    rows, order = {}, []
    with open(path) as f:
        for line in f:
            parts = line.split()
            if len(parts) != 3:
                continue
            key = (parts[0], parts[1])
            rows[key] = parts[2]
            order.append(key)
    return rows, order

c, c_order = load(sys.argv[1])
r, _ = load(sys.argv[2])

print("== per-case comparison (C token / Rust token / status) ==")
print(f"{'method':18s} {'case':10s} {'C':10s} {'Rust':10s} status")
cases = matched = mismatched = blocked = 0
for key in c_order:
    cases += 1
    ct = c.get(key, "MISSING")
    rt = r.get(key, "MISSING")
    if ct == "BLOCKED" or rt == "BLOCKED":
        status = "BLOCKED"
        blocked += 1
    elif ct == rt:
        status = "MATCH"
        matched += 1
    else:
        status = "MISMATCH"
        mismatched += 1
    print(f"{key[0]:18s} {key[1]:10s} {ct:10s} {rt:10s} {status}")

print(f"METHOD_ALLOC {cases} {matched} {mismatched} {blocked}")
sys.exit(1 if mismatched else 0)
PY
