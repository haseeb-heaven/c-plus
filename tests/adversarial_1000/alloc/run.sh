#!/bin/sh
# Build and run the 1000-scenario deterministic adversarial allocation suite.
# Creates only files under tests/adversarial_1000/alloc/results/ (gitignored).
set -u

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../../.." && pwd)
SAFE="$ROOT/build/safe/libcplus_safecrt.a"
LIBC="$ROOT/build/libc/libcplus_hardened.a"
OUT="$HERE/results"
BIN="$OUT/adv1000_alloc"

mkdir -p "$OUT"
echo "ROOT=$ROOT"
echo "SAFE=$SAFE"
echo "LIBC=$LIBC"

# --- symbol verification with nm (macOS symbols carry a leading underscore) --
echo "--- nm verification ---"
NM_OK=1
for sym in malloc_s calloc_s realloc_s free_s free_checked_s aligned_alloc_s \
           cplus_safe_ptr_is_live cplus_safe_ptr_size cplus_safe_ptr_was_freed \
           cplus_safe_alloc_fail_after cplus_safe_alloc_fail_reset; do
    if /usr/bin/nm -gU "$SAFE" | grep -q "_${sym}$"; then
        echo "NM OK $sym"
    else
        echo "NM MISSING $sym"
        NM_OK=0
    fi
done
if [ "$NM_OK" -ne 1 ]; then
    echo "FATAL: required symbol(s) missing"
    exit 2
fi

# --- compile -----------------------------------------------------------------
CC_CMD="/usr/bin/clang -std=c99 -O1 -Wall -Wextra -I$ROOT/stdlib/safe $HERE/adv1000_alloc.c $SAFE $LIBC -lm -pthread -o $BIN"
echo "--- compile ---"
echo "$CC_CMD"
/usr/bin/clang -std=c99 -O1 -Wall -Wextra -I"$ROOT/stdlib/safe" \
    "$HERE/adv1000_alloc.c" "$SAFE" "$LIBC" -lm -pthread -o "$BIN" 2>"$OUT/build.stderr"
BUILD_STATUS=$?
echo "BUILD_STATUS=$BUILD_STATUS"
cat "$OUT/build.stderr"
if [ "$BUILD_STATUS" -ne 0 ]; then exit 3; fi

# --- run ---------------------------------------------------------------------
RUN_CMD="timeout 120 $BIN > $OUT/scenarios.log 2> $OUT/stderr.log"
echo "--- run ---"
echo "$RUN_CMD"
timeout 120 "$BIN" >"$OUT/scenarios.log" 2>"$OUT/stderr.log"
RUN_STATUS=$?
echo "RUN_STATUS=$RUN_STATUS"

# --- digest + accounting -----------------------------------------------------
LINES=$(wc -l < "$OUT/scenarios.log" | tr -d ' ')
DIGEST=$(shasum -a 256 "$OUT/scenarios.log" | awk '{print $1}')
printf '%s\n' "$DIGEST" > "$OUT/scenarios.log.sha256"
echo "LINES=$LINES"
echo "SHA256=$DIGEST"
echo "--- final summary line ---"
tail -n 1 "$OUT/scenarios.log"
echo "--- stderr from run (should be empty: logging defaults OFF) ---"
cat "$OUT/stderr.log"

if [ "$RUN_STATUS" -ne 0 ]; then
    echo "SUITE FAILED (exit $RUN_STATUS)"
    exit "$RUN_STATUS"
fi
if [ "$LINES" -ne 1001 ]; then
    echo "FATAL: expected 1001 lines (1000 scenarios + summary), got $LINES"
    exit 4
fi
echo "SUITE PASSED"
exit 0
