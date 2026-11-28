#!/bin/sh
# Compile and run the Zig language-rounds benchmark. POSIX sh, bounded.
# Asserts the fixed workload checksum; timing is reported by the binary itself.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
D=benchmarks/language_rounds
EXPECTED=16776346274400
mkdir -p build/lang_rounds
if ! command -v zig >/dev/null 2>&1; then
  echo "ZIG_FAIL zig toolchain not found"
  exit 1
fi
zig build-exe -O ReleaseFast "$D/bench.zig" -femit-bin=build/lang_rounds/bench_zig || {
  echo "ZIG_FAIL build failed"
  exit 1
}
out=$(timeout 60 build/lang_rounds/bench_zig) || {
  echo "ZIG_FAIL run failed"
  exit 1
}
echo "$out"
checksum=$(printf '%s\n' "$out" | awk '{print $3}')
if [ "$checksum" = "$EXPECTED" ]; then
  echo "ZIG_OK $checksum"
else
  echo "ZIG_FAIL expected $EXPECTED got $checksum"
  exit 1
fi
