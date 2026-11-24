#!/bin/sh
# BLUE20 checked format/IO safety proof driver.
# Fork-per-case C harness (proof.c), linked against the safecrt archive,
# executed under a bounded 60-second timeout.  Prints the exact result line
#   BLUE20 format <cases> <passed> <failed>
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 1

OUT="$ROOT/tests/redblue20/blue_format/out"
mkdir -p "$OUT"

# Rebuild the safecrt archive with the system SDK compiler (the Homebrew
# clang on this host has no matching sysroot).  This is the archive every
# case is linked against.
CC=${CPLUS_CLANG:-/usr/bin/clang}
CPLUS_CLANG="$CC" sh scripts/build_safecrt.sh >"$OUT/safecrt_build.log" 2>&1 || {
    echo "BLUE20 format BUILD FAIL (safecrt archive)"
    tail -20 "$OUT/safecrt_build.log"
    exit 1
}
SAFE="$ROOT/build/safe/libcplus_safecrt.a"

INC="-I. -Istdlib/safe -Ilibc -Ilibc/install-include -Ithird_party/safeclib/include"
BIN="$OUT/proof"
"$CC" -O1 -g -std=c99 -Wall -Wextra -pedantic $INC \
    tests/redblue20/blue_format/proof.c \
    "$SAFE" -lm -pthread -o "$BIN" >"$OUT/proof.build.log" 2>&1 || {
    echo "BLUE20 format BUILD FAIL (proof harness)"
    tail -30 "$OUT/proof.build.log"
    exit 1
}

timeout 60 "$BIN" >"$OUT/proof.log" 2>&1
RUN_RC=$?

# Surface the evidence: per-category counts, seed/digest, final summary,
# and any failing case lines.
grep -E '^(CAT |SEED |case=.*result=FAIL)' "$OUT/proof.log" | head -80

SUMMARY=$(grep -E '^BLUE20 format [0-9]+ [0-9]+ [0-9]+$' "$OUT/proof.log" | tail -1)
if [ -z "$SUMMARY" ]; then
    echo "BLUE20 format 0 0 0 (no summary; run_rc=$RUN_RC)"
    tail -20 "$OUT/proof.log"
    exit 1
fi

CASES=$(printf '%s\n' "$SUMMARY" | awk '{print $3}')
PASSED=$(printf '%s\n' "$SUMMARY" | awk '{print $4}')
FAILED=$(printf '%s\n' "$SUMMARY" | awk '{print $5}')

if [ "$RUN_RC" -eq 0 ] && [ "$FAILED" -eq 0 ] && [ "$PASSED" -eq "$CASES" ] && [ "$CASES" -ge 2000 ]; then
    echo "$SUMMARY"
    exit 0
fi
echo "BLUE20 format $CASES $PASSED $FAILED (verification failed; run_rc=$RUN_RC)"
exit 1
