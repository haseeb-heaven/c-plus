#!/bin/sh
# Build and run the 1000-scenario deterministic adversarial harness for the
# checked C+ format / stream-I/O APIs. All generated artifacts live in the
# gitignored results/ directory next to this script.
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1

CC=/usr/bin/clang
INC="-Istdlib/safe -Ilibc/install-include -Ilibc -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBS="build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a"
RES="$HERE/results"
mkdir -p "$RES"
BIN="$RES/adv1000_fmt_io"
LOG="$RES/scenarios.log"
ERR="$RES/scenarios.err"
SUM="$RES/scenarios.log.sha256"

echo "== ADV1000 fmt_io build =="
"$CC" -O1 -g -std=c99 -Wall -Wextra \
    -Wno-format-nonliteral -Wno-format-security -Wno-unused-function \
    $INC "$HERE/adv1000_fmt_io.c" $LIBS -lm -pthread -o "$BIN"
if [ $? -ne 0 ]; then echo "BUILD FAILED"; exit 1; fi

echo "== ADV1000 fmt_io run (timeout 120) =="
timeout 120 "$BIN" >"$LOG" 2>"$ERR"
rc=$?
echo "harness exit = $rc"

# Digest of the exact 1000-line scenario log.
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$LOG" >"$SUM"
elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$LOG" >"$SUM"
fi
echo "digest: $(cat "$SUM" 2>/dev/null)"

nlines=$(grep -c '^S ' "$LOG" 2>/dev/null || echo 0)
nfinal=$(grep -c '^ADV1000 ' "$LOG" 2>/dev/null || echo 0)
echo "scenario lines = $nlines ; summary lines = $nfinal"
echo "tail of log:"
tail -3 "$LOG"

if [ "$nlines" -ne 1000 ] || [ "$nfinal" -ne 1 ]; then
    echo "SHAPE FAILED"
    exit 1
fi
exit "$rc"
