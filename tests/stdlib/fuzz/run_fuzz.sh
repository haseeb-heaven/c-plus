#!/bin/sh
# Build and run the libFuzzer targets for the stdlib (issue #8).
#   tests/stdlib/fuzz/run_fuzz.sh [runs] [target]
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
CXX=${CPLUS_CLANG:-clang}
OUT="$ROOT/build/fuzz"
RUNS=${1:-500}
TARGET=${2:-all}
mkdir -p "$OUT"
cd "$ROOT" || exit 1

CPLUS_LIBS="-Ithird_party/yyjson -Lbuild/ext -lyyjson"
FAIL=0

# libFuzzer when the toolchain ships its runtime, otherwise the deterministic
# mutation driver in fuzz_main.c.  AddressSanitizer is unusable on some hosts
# (see docs/LIMITATIONS.md), so the fallback relies on safe mode's UBSan set
# plus the runtime allocator's double-free detection.
FUZZER_FLAGS="-fsanitize=fuzzer,address"
MODE="libfuzzer"
if ! printf 'int LLVMFuzzerTestOneInput(const unsigned char *d, unsigned long n){return 0;}' >"$OUT/probe.c" ||
   ! "$CXX" -fsanitize=fuzzer,address "$OUT/probe.c" -o "$OUT/probe" >"$OUT/probe.log" 2>&1; then
  MODE="mutate"
  "$CXX" -c tests/stdlib/fuzz/fuzz_main.c -o "$OUT/fuzz_main.o" \
    -fsanitize=undefined,bounds,null,alignment -fno-sanitize-recover=all || exit 1
  ar rcs "$OUT/libfuzzmain.a" "$OUT/fuzz_main.o" || exit 1
fi
echo "fuzz mode: $MODE"

build() { # name
  case "$1" in
    fuzz_json) libs="$CPLUS_LIBS" ;;
    *) libs="" ;;
  esac
  if [ "$MODE" = "libfuzzer" ]; then
    "$CPLUS" build "tests/stdlib/fuzz/$1.cp" $FUZZER_FLAGS $libs -o "$OUT/$1" \
      >"$OUT/$1.build.log" 2>&1 || return 1
  else
    "$CPLUS" build "tests/stdlib/fuzz/$1.cp" $libs -L"$OUT" -lfuzzmain \
      -o "$OUT/$1" >"$OUT/$1.build.log" 2>&1 || return 1
  fi
  return 0
}

run() { # name corpus-subdir
  if [ "$TARGET" != "all" ] && [ "$TARGET" != "$1" ]; then return 0; fi
  if ! build "$1"; then
    echo "FAIL build $1 (see $OUT/$1.build.log)" >&2
    FAIL=1
    return 0
  fi
  if [ "$MODE" = "libfuzzer" ]; then
    "$OUT/$1" -runs="$RUNS" -max_len=2048 "tests/stdlib/fuzz/corpus" \
      >"$OUT/$1.run.log" 2>&1
  else
    "$OUT/$1" tests/stdlib/fuzz/corpus "$RUNS" >"$OUT/$1.run.log" 2>&1
  fi
  if [ $? -eq 0 ]; then
    printf 'ok   %s (%s inputs, %s)\n' "$1" "$RUNS" "$MODE"
  else
    echo "FAIL $1 (see $OUT/$1.run.log)" >&2
    FAIL=1
  fi
}

run fuzz_json
run fuzz_string
exit "$FAIL"
