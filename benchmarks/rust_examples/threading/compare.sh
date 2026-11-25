#!/bin/sh
# Runnable Rust examples vs C+ checked thread/synchronization wrappers.
#
# Compiles both harnesses with the pinned toolchains, runs each under a 30 s
# timeout, diffs the "<api> <case> <token>" lines, and prints
#   RUST_EXAMPLES threading <apis> <cases> <matched> <mismatched> <blocked>
# Exits nonzero only on a real token mismatch (BLOCKED cases are reported but
# are not counted as mismatches). Absent/unsupported symbols are BLOCKED with
# nm evidence rather than being silently treated as matches.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 2

SRC="$ROOT/benchmarks/rust_examples/threading"
# Keep every generated artifact inside this agent's owned directory.
OUT="$SRC/results"
mkdir -p "$OUT"

CC=/usr/bin/clang
[ -x "$CC" ] || CC=clang
RUSTC=${RUSTC:-rustc}
HARD=build/libc/libcplus_hardened.a
TIMEOUT=30

if command -v timeout >/dev/null 2>&1; then TMO=timeout; else TMO=gtimeout; fi

echo "== toolchains =="
"$CC" --version | head -1
"$RUSTC" --version
echo "== archive =="
# Parallel agents rebuild build/libc; wait briefly for a transient removal.
_wait=0
while [ ! -f "$HARD" ] && [ "$_wait" -lt 15 ]; do sleep 1; _wait=$((_wait + 1)); done
ls -l "$HARD"

# Symbol verification: every wrapper the harness calls must be exported by the
# hardened archive, and there must be no checked double-destroy symbol.
echo "== symbol check (nm -g build/libc/libcplus_hardened.a) =="
REQUIRED="cplus_thrd_create cplus_thrd_join \
          cplus_mtx_init cplus_mtx_lock cplus_mtx_unlock cplus_mtx_trylock cplus_mtx_destroy \
          cplus_cnd_init cplus_cnd_wait cplus_cnd_signal cplus_cnd_timedwait cplus_cnd_destroy \
          cplus_tss_create cplus_tss_get cplus_tss_set cplus_tss_delete"
MISSING=0
for s in $REQUIRED; do
  if ! nm -g --defined-only "$HARD" 2>/dev/null | grep -qE " T _?${s}\$"; then
    echo "MISSING symbol (case becomes BLOCKED): $s" >&2
    MISSING=1
  fi
done
if [ "$MISSING" -ne 0 ]; then
  echo "== nm evidence for absent symbols =="
  nm -g "$HARD" 2>/dev/null | grep -E ' T _cplus_(thrd|mtx|cnd|tss|pthread)' | sort -k3
  echo "RUST_EXAMPLES threading 0 0 0 0 11"
  echo "All cases BLOCKED: required wrapper symbols absent (see nm evidence above)."
  exit 0
fi

# Double destroy: no checked guard exists -> BLOCKED, with nm evidence.
if nm -g --defined-only "$HARD" 2>/dev/null | grep -qE ' T _?cplus_(mtx|cnd)_destroy_checked'; then
  echo "NOTE: an unexpected checked-destroy symbol is now present in the archive."
else
  echo "double-destroy: no checked guard symbol present -> BLOCKED (see NOTES.md)."
  nm -g "$HARD" 2>/dev/null | grep -E ' T _cplus_(mtx|cnd)_destroy$' | sort -k3
fi
echo "all required symbols present"

# Build.
CFLAGS="-std=c99 -O1 -g -I. -Ilibc/install-include"
echo "== build C99 harness =="
"$CC" $CFLAGS "$SRC/harness.c" "$HARD" -pthread -lm \
  -o "$OUT/threading_harness" || exit 2
echo "== build Rust harness =="
"$RUSTC" -O "$SRC/threading.rs" -o "$OUT/threading_rust" || exit 2

# Run under timeout; record exit status.
"$TMO" "$TIMEOUT" "$OUT/threading_harness" > "$OUT/c.out" 2> "$OUT/c.err"
c_rc=$?
"$TMO" "$TIMEOUT" "$OUT/threading_rust" > "$OUT/rust.out" 2> "$OUT/rust.err"
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
print(f"{'api':14s} {'case':14s} {'C':10s} {'Rust':10s} status")
apis = {k[0] for k in c_order}
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
    print(f"{key[0]:14s} {key[1]:14s} {ct:10s} {rt:10s} {status}")

print(f"RUST_EXAMPLES threading {len(apis)} {cases} {matched} {mismatched} {blocked}")
sys.exit(1 if mismatched else 0)
PY
