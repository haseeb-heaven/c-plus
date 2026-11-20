#!/bin/sh
# C+ benchmark suite.
#
# Each benchmark program is built three times from the same source:
#
#   c+ --release            -O2 with the C+ runtime checks ON (safe default)
#   c+ --release --legacy-c -O2 with the same C+ lowering, no runtime checks
#   clang -O2               -O2 plain C, the hand-written equivalent
#
# The legacy column is what isolates the cost of the language features
# themselves (lowered classes, inserted destructors, managed allocator) from
# the cost of the sanitizer instrumentation.  Ratios are against plain C.
#
# Usage: benchmarks/run_bench.sh [name-pattern]

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
CLANG=${CPLUS_CLANG:-clang}
OUT="$ROOT/benchmarks/out"
RUNS=${CPLUS_BENCH_RUNS:-5}
PATTERN=${1:-}

mkdir -p "$OUT"

if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler not found at $CPLUS (build with cmake --build build)" >&2
  exit 1
fi

if ! command -v "$CLANG" >/dev/null 2>&1; then
  echo "error: reference C compiler '$CLANG' not found" >&2
  exit 1
fi

echo "C+ benchmarks"
echo "runs per binary: $RUNS (median reported)"
echo

printf '%-16s %9s %9s %9s %7s %7s\n' \
  "benchmark" "c+ safe" "c+ legacy" "clang -O2" "safe" "legacy"
printf '%-16s %9s %9s %9s %7s %7s\n' \
  "----------------" "---------" "---------" "---------" "-----" "------"

FAILED=0

for src in "$ROOT"/benchmarks/*.c; do
  [ -e "$src" ] || continue
  name=$(basename "$src" .c)
  case "$name" in
    *"$PATTERN"*) ;;
    *) continue ;;
  esac

  safe="$OUT/$name.safe"
  legacy="$OUT/$name.legacy"
  plain="$OUT/$name.c"

  if ! "$CPLUS" build --release "$src" -o "$safe" >"$OUT/$name.safe.log" 2>&1; then
    echo "  FAIL $name: c+ --release failed; see benchmarks/out/$name.safe.log"
    FAILED=1
    continue
  fi
  if ! "$CPLUS" build --release --legacy-c "$src" -o "$legacy" \
      >"$OUT/$name.legacy.log" 2>&1; then
    echo "  FAIL $name: c+ --legacy-c failed; see benchmarks/out/$name.legacy.log"
    FAILED=1
    continue
  fi
  if ! "$CLANG" -std=gnu17 -O2 -I"$ROOT/include" -include cplus/cplus.h \
      "$src" -o "$plain" >"$OUT/$name.c.log" 2>&1; then
    echo "  FAIL $name: clang build failed; see benchmarks/out/$name.c.log"
    FAILED=1
    continue
  fi

  safe_time=$("$ROOT/benchmarks/timeit.py" "$RUNS" "$safe") || {
    echo "  FAIL $name: safe binary did not run"
    FAILED=1
    continue
  }
  legacy_time=$("$ROOT/benchmarks/timeit.py" "$RUNS" "$legacy") || {
    echo "  FAIL $name: legacy binary did not run"
    FAILED=1
    continue
  }
  plain_time=$("$ROOT/benchmarks/timeit.py" "$RUNS" "$plain") || {
    echo "  FAIL $name: clang binary did not run"
    FAILED=1
    continue
  }

  python3 - "$name" "$safe_time" "$legacy_time" "$plain_time" <<'EOF'
import sys

name = sys.argv[1]
safe_s = float(sys.argv[2].split()[0])
legacy_s = float(sys.argv[3].split()[0])
plain_s = float(sys.argv[4].split()[0])
safe_ratio = safe_s / plain_s if plain_s else 0.0
legacy_ratio = legacy_s / plain_s if plain_s else 0.0
print(
    f"{name:<16} {safe_s:>8.4f}s {legacy_s:>8.4f}s {plain_s:>8.4f}s "
    f"{safe_ratio:>6.2f}x {legacy_ratio:>5.2f}x"
)
EOF
done

echo
if [ "$FAILED" -ne 0 ]; then
  echo "C+ benchmarks: build or run failure"
  exit 1
fi
echo "C+ benchmarks: all ran"
