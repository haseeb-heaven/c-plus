#!/bin/sh
# Deterministic build+run for the checked-memory hardening regressions.
#
# By default this script links the archive produced by the parent-owned build
# script:  CPLUS_CLANG=/usr/bin/clang sh scripts/build_safecrt.sh
# Pass SAFECRT_ARCHIVE=/path/to/lib to verify a locally rebuilt copy without
# touching build/ (used for the before/after evidence in HARDENING.md).
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
CC=${CPLUS_CLANG:-/usr/bin/clang}
ARCH=${SAFECRT_ARCHIVE:-$ROOT/build/safe/libcplus_safecrt.a}
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/build/safe"
OUT="$HERE/out"
mkdir -p "$OUT"

echo "== compile =="
# Same flags as scripts/build_safecrt.sh CFLAGS_W for first-party C sources.
$CC -O2 -Wall -Wextra -std=c11 $INC "$HERE/test_memory_hardening.c" \
    "$ARCH" -pthread -o "$OUT/test_memory_hardening"

echo "== run (timeout 30) =="
set +e
timeout 30 "$OUT/test_memory_hardening" >"$OUT/stdout.log" 2>"$OUT/stderr.log"
RC=$?
set -e
cat "$OUT/stdout.log"
cat "$OUT/stderr.log"
echo "exit=$RC"
exit "$RC"
