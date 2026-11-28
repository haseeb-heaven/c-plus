#!/bin/sh
# Regression: a sanitized C+ build passes application build flags through to
# the driver.  Linker-only flags must never reach the instrumented runtime's
# compile-only (-c) invocation, even under the default warnings-as-errors
# policy.  See the -ldefinitely_nonexistent case below.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/tests/out"
mkdir -p "$OUT"

src="$ROOT/tests/compiler/instrumented_runtime_linker_flags.cp"
obj="$OUT/instrumented_runtime_linker_flags.o"
log="$OUT/instrumented_runtime_linker_flags.log"

rm -f "$obj"
# Default Safe mode selects UBSan, so the driver recompiles the C+ runtime
# with -c.  Before the fix, -ldefinitely_nonexistent reached that invocation
# and clang failed with "-l...: 'linker' input unused" (an error under
# -Werror).  -c keeps this regression scoped to the runtime phase: the
# missing library is intentionally never linked.
if ! "$CPLUS" build "$src" -DCPLUS_IRLF_DEFINE_SEEN=1 \
        -ldefinitely_nonexistent -c -o "$obj" >"$log" 2>&1; then
  printf '  FAIL instrumented_runtime_linker_flags: %s\n' \
    "$(tail -3 "$log" | tr '\n' ' ')" >&2
  exit 1
fi
if [ ! -f "$obj" ]; then
  printf '  FAIL instrumented_runtime_linker_flags: no object produced\n' >&2
  exit 1
fi
printf '  ok   instrumented_runtime_linker_flags\n'
