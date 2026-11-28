#!/bin/sh
set -eu

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
COMPILER="$ROOT/build/c+"
SOURCE="$ROOT/scratch/bugs3.cp"
OUTPUT="/tmp/bugs3_test_out.log"

if [ ! -x "$COMPILER" ]; then
    echo "ERROR: Compiler $COMPILER not found or not executable"
    exit 1
fi

if [ ! -f "$SOURCE" ]; then
    echo "ERROR: Source $SOURCE not found"
    exit 1
fi

echo "=== Testing bugs3.cp with $COMPILER ==="
# We expect bugs3.cp to be rejected at compile time due to safety violations:
# 1. Unchecked return value of fwrite
# 2. Overlapping pointers to restrict parameters
# 3. Variable-length stack allocation without bounds check
# 4. Warnings for static state (inet_ntoa, strtok) and longjmp/setjmp
set +e
"$COMPILER" "$SOURCE" -o /tmp/bugs3_bin > "$OUTPUT" 2>&1
RC=$?
set -e

cat "$OUTPUT"

if [ "$RC" -eq 0 ]; then
    echo "FAIL: bugs3.cp compiled successfully, expected compile-time safety rejection"
    exit 1
fi

# Verify expected safety errors and warnings
grep -q "check the return value of fwrite" "$OUTPUT" || { echo "FAIL: missing fwrite check diagnostic"; exit 1; }
grep -q "overlapping pointers to restrict parameters" "$OUTPUT" || { echo "FAIL: missing restrict diagnostic"; exit 1; }
grep -q "variable-length stack allocation has no bounds check" "$OUTPUT" || { echo "FAIL: missing VLA diagnostic"; exit 1; }
grep -q "inet_ntoa.*keeps static state" "$OUTPUT" || { echo "FAIL: missing inet_ntoa static state warning"; exit 1; }
grep -q "strtok.*keeps static state" "$OUTPUT" || { echo "FAIL: missing strtok static state warning"; exit 1; }
grep -q "longjmp.*bypasses C+ deterministic cleanup" "$OUTPUT" || { echo "FAIL: missing longjmp warning"; exit 1; }

echo "PASS: test_bugs3 verified all safety rejections and diagnostics"
rm -f "$OUTPUT" /tmp/bugs3_bin
