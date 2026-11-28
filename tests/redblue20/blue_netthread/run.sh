#!/bin/sh
# BLUE-TEAM agent 14/20 - blue_netthread network/thread safety proof runner.
# STRICT OWNERSHIP: writes only under tests/redblue20/blue_netthread/.
set -u
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
OUT="$SCRIPT_DIR/out"
mkdir -p "$OUT"

# The C+ driver links only libcplus_runtime.a by default. Expose the additive
# checked-sync archive through a conventional -l name so it can be passed as a
# link-only extra arg (a raw .a path is fed to the C compile step too).
ln -sf "$ROOT/build/libc/libcplus_hardened.a" "$OUT/libchecked.a"

BUILD_LOG="$OUT/build.log"
"$ROOT/build/c+" build "$SCRIPT_DIR/proof.cp" --sanitize=none -w \
    -I"$ROOT" -L"$OUT" -lchecked -lcurl -pthread -o "$OUT/proof" \
    >"$BUILD_LOG" 2>&1
if [ $? -ne 0 ]; then
    echo "BLUE20 netthread BUILD FAILED"
    tail -40 "$BUILD_LOG"
    exit 3
fi

# Fork-per-case isolation lives in proof.cp (alarm(5) per child, alarm(55) in
# the parent). This outer bound makes the whole run strictly <= 60 seconds.
timeout 60 "$OUT/proof" >"$OUT/run.log" 2>&1
RC=$?
grep '^BLUE20 netthread' "$OUT/run.log" | tail -1
exit "$RC"
