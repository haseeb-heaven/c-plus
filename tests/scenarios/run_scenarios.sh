#!/bin/sh
# High-volume scenario runner: 1000+ distinct cases per C bug family.
# Each scenario_NN.c uses ORDINARY C names hardened by stdlib/safe/safe_c.h.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"
CC=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/scenarios"
mkdir -p "$OUT"
sh tests/libc/build_libc.sh >/dev/null
sh scripts/build_safecrt.sh >/dev/null

LIBC="$ROOT/build/libc/libcplus_hardened.a"
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
pass=0; fail=0

report() {
  if [ "$1" -eq 0 ]; then echo "ok   $2"; pass=$((pass + 1));
  else echo "FAIL $2"; shift 2; tail -12 "$@" 2>/dev/null || true; fail=$((fail + 1)); fi
}

for t in tests/scenarios/scenario_*.c tests/scenarios/runtime_*.c; do
  [ -f "$t" ] || continue
  name=$(basename "$t" .c)

  # Compile-only negative fixtures: success would be the failure.
  case "$name" in
    scenario_06)
      ./build/c+ check "$t" --default >"$OUT/$name.log" 2>&1
      rc=$?
      n=$(grep -c '\[CPLUS-' "$OUT/$name.log" 2>/dev/null || true)
      if [ "$rc" -ne 0 ] && [ "${n:-0}" -ge 1 ]; then
        echo "ok   $name (compile-only: $n diagnostics)"
        pass=$((pass + 1))
      else
        echo "FAIL $name (expected rejection with [CPLUS- diagnostics, rc=$rc n=$n)"
        tail -12 "$OUT/$name.log" || true
        fail=$((fail + 1))
      fi
      continue
      ;;
  esac

  for mode in plain ubsan; do
    if [ "$mode" = ubsan ]; then
      SAN="-fsanitize=undefined -fno-sanitize-recover=all -fno-sanitize=float-divide-by-zero"
    else
      SAN=""
    fi
    bin="$OUT/${name}_${mode}"
    if "$CC" -O2 -std=c99 -Wall -Wextra -pedantic $SAN $INC "$t" "$SAFE" "$LIBC" -lm -pthread \
        -o "$bin" >"$OUT/${name}_${mode}.build.log" 2>&1 \
       && "$bin" >"$OUT/${name}_${mode}.run.log" 2>&1; then
      echo "ok   $name ($mode): $(tail -1 "$OUT/${name}_${mode}.run.log")"
      pass=$((pass + 1))
    else
      echo "FAIL $name ($mode)"
      tail -12 "$OUT/${name}_${mode}.build.log" 2>/dev/null || true
      tail -12 "$OUT/${name}_${mode}.run.log" 2>/dev/null || true
      fail=$((fail + 1))
    fi
  done

  # The uninitialized-field fixture has an extra compile-only probe mode.
  if [ "$name" = scenario_09 ]; then
    if ./build/c+ check -DCPLUS_UNINIT_COMPILE_PROBE "$t" --default >"$OUT/$name.probe.log" 2>&1; then
      echo "FAIL $name (probe expected rejection)"
      fail=$((fail + 1))
    else
      n=$(grep -c '\[CPLUS-' "$OUT/$name.probe.log" 2>/dev/null || true)
      echo "ok   $name (compile probe: ${n:-0} diagnostics)"
      pass=$((pass + 1))
    fi
  fi
done

echo "scenario suites: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
