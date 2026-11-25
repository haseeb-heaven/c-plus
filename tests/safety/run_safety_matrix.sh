#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT"

sh scripts/build_safecrt.sh >/dev/null
sh tests/libc/build_libc.sh >/dev/null

CC=${CPLUS_CLANG:-clang}
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/build/safety"
mkdir -p "$OUT"

pass=0
fail=0

# 1. C scenarios with recoverable UBSan + SafeCRT + Hardened Libc
if "$CC" -O0 -g -Wall -Wextra -Werror -std=c99 -pthread \
     -fsanitize=undefined,bounds,null,alignment -fsanitize-recover=all -fno-sanitize-trap=all -fno-omit-frame-pointer \
     -Istdlib/safe -Ilibc -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
     tests/safety/test_scenarios_c.c stdlib/safe/src/safe_runtime.c \
     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
     -o "$OUT/test_scenarios_c" >"$OUT/c.log" 2>&1 \
   && "$OUT/test_scenarios_c" >"$OUT/c.out" 2>&1; then
  echo "ok test_scenarios_c"
  pass=$((pass + 1))
else
  echo "FAIL test_scenarios_c"
  cat "$OUT/c.log" "$OUT/c.out" 2>/dev/null || true
  fail=$((fail + 1))
fi

# 2. C+ scenarios in safe mode
if "$CPLUS" build --no-werror tests/safety/test_scenarios_cp.cp -o "$OUT/test_scenarios_cp" >"$OUT/cp.log" 2>&1 \
   && "$OUT/test_scenarios_cp" >"$OUT/cp.out" 2>&1; then
  echo "ok test_scenarios_cp"
  pass=$((pass + 1))
else
  echo "FAIL test_scenarios_cp"
  cat "$OUT/cp.log" "$OUT/cp.out" 2>/dev/null || true
  fail=$((fail + 1))
fi

# 3. Multithreaded safety & ownership handoff scenarios
if "$CC" -O1 -g -Wall -Wextra -Werror -std=c99 -pthread \
     -fsanitize=undefined -fno-sanitize-recover=all \
     -Istdlib/safe -Ilibc \
     tests/safety/test_scenarios_threads.c \
     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
     -o "$OUT/test_scenarios_threads" >"$OUT/threads.log" 2>&1 \
   && "$OUT/test_scenarios_threads" >"$OUT/threads.out" 2>&1; then
  echo "ok test_scenarios_threads"
  pass=$((pass + 1))
else
  echo "FAIL test_scenarios_threads"
  cat "$OUT/threads.log" "$OUT/threads.out" 2>/dev/null || true
  fail=$((fail + 1))
fi

echo "safety matrix tests: $pass passed, $fail failed"
[ "$fail" -eq 0 ]
