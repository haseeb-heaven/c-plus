#!/bin/sh
# FUZZ10K / io -- build + run the 12,000-scenario deterministic forked fuzz
# harness for the checked C+ stream-I/O APIs. All generated artifacts live in
# tests/fuzz10k/io/results/, which is gitignored by the repository root .gitignore.
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1

CC=/usr/bin/clang
INC="-Istdlib/safe -Ilibc/install-include -Ilibc -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include"
LIBS="build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a"
RES="$HERE/results"
mkdir -p "$RES"
BIN="$RES/fuzz10k_io"
LOG="$RES/scenarios.log"
ERR="$RES/scenarios.err"
SUMFILE="$RES/summary.log"
DIGEST="$RES/scenarios.log.sha256"
BUILDLOG="$RES/build.log"

# Bounded temp dir for all fixtures. No user/home path is ever referenced.
TMPD=$(mktemp -d "${TMPDIR:-/tmp}/fuzz10k_io.XXXXXX") || exit 1
cleanup() {
    case "$TMPD" in
        */fuzz10k_io.*) rm -rf -- "$TMPD" ;;
    esac
}
trap cleanup EXIT HUP INT TERM

echo "== FUZZ10K io build =="
# Verify the six in-scope symbols exist in the safecrt archive before building.
nm -gU build/safe/libcplus_safecrt.a 2>/dev/null | \
    grep -E '^[0-9a-f]+ T _cplus_(fopen_s|fclose_s|fread_s|fwrite_s|fprintf_s|fscanf_s)$' \
    > "$RES/symbols.txt"
if [ "$(wc -l < "$RES/symbols.txt" | tr -d ' ')" -ne 6 ]; then
    echo "SYMBOL CHECK FAILED (expected 6 in-scope symbols)"
    cat "$RES/symbols.txt"
    exit 1
fi
cat "$RES/symbols.txt"

"$CC" -O1 -g -std=c99 -Wall -Wextra -Wno-unused-parameter \
    -Wno-format-nonliteral -Wno-format-security \
    $INC "$HERE/fuzz10k_io.c" $LIBS -lm -pthread -o "$BIN" >"$BUILDLOG" 2>&1
if [ $? -ne 0 ]; then
    echo "BUILD FAILED"
    cat "$BUILDLOG"
    exit 1
fi
echo "build ok: $BIN"

echo "== FUZZ10K io run (fixed seed 0x46555A5A3130304B, timeout 600) =="
/usr/bin/timeout 600 "$BIN" "$LOG" "$TMPD" >"$SUMFILE" 2>"$ERR"
hrc=$?
echo "harness exit = $hrc"

# Digest of the exact scenario log (S-lines + trailers).
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$LOG" > "$DIGEST"
elif command -v sha256sum >/dev/null 2>&1; then
    sha256sum "$LOG" > "$DIGEST"
fi
echo "digest: $(cat "$DIGEST" 2>/dev/null)"

nlines=$(grep -c '^S ' "$LOG" 2>/dev/null || true)
ndistinct=$(grep -c '^FUZZ10K_DISTINCT ' "$LOG" 2>/dev/null || true)
nfinal=$(grep -c '^FUZZ10K io ' "$SUMFILE" 2>/dev/null || true)
echo "scenario lines = $nlines ; distinct trailers = $ndistinct ; summary lines = $nfinal"
tail -3 "$LOG" 2>/dev/null
tail -2 "$ERR" 2>/dev/null

fail=0
if [ "$nlines" -ne 12000 ] || [ "$ndistinct" -ne 1 ] || [ "$nfinal" -ne 1 ]; then
    echo "SHAPE FAILED"
    fail=1
fi
if [ "$hrc" -eq 124 ] || [ "$hrc" -eq 137 ]; then
    echo "PROCESS-WIDE TIMEOUT / KILL"
    fail=1
fi
if [ -f "$SUMFILE" ]; then
    cat "$SUMFILE"
else
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    exit 1
fi
exit "$hrc"
