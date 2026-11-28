#!/bin/sh
# BLUE-20 SafeCRT proof driver.
# Builds the fork-per-case harness against the checked SafeCRT layer and the
# hardened libc, verifies every symbol it calls exists in the archives (or
# fails closed), then runs the fixed-seed case suite under a 60s bound.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)

CLANG=${CLANG:-/usr/bin/clang}
NM=${NM:-nm}
TIMEOUT=${TIMEOUT:-timeout}
CASES=${CASES:-2400}

SAFECRT="$ROOT/build/safe/libcplus_safecrt.a"
HARDENED="$ROOT/build/libc/libcplus_hardened.a"
INCLUDE="-I$ROOT/stdlib/safe -I$ROOT/libc/install-include -I$ROOT/third_party/safeclib/include"

command -v "$TIMEOUT" >/dev/null 2>&1 || { echo "missing timeout: $TIMEOUT" >&2; exit 2; }
command -v "$NM" >/dev/null 2>&1 || { echo "missing nm: $NM" >&2; exit 2; }
[ -f "$SAFECRT" ] || { echo "missing $SAFECRT" >&2; exit 2; }
[ -f "$HARDENED" ] || { echo "missing $HARDENED" >&2; exit 2; }

mkdir -p "$HERE/out"
: > "$HERE/out/symbols.txt"
: > "$HERE/out/build.log"
: > "$HERE/out/run.log"

# Fail closed if any checked adapter or support symbol is absent.
for sym in \
    cplus_memcpy_s cplus_memmove_s cplus_memset_s cplus_memcmp_s \
    cplus_strcpy_s cplus_strncpy_s cplus_strcat_s cplus_strncat_s \
    cplus_runtime_set_violation_trap \
    cplus_sc_set_mem_constraint_handler_s_wrapper \
    cplus_sc_set_str_constraint_handler_s_wrapper; do
    if "$NM" -g "$SAFECRT" 2>/dev/null | awk -v s="_$sym" '$2=="T" && $3==s {f=1} END{exit !f}'; then
        echo "SYMBOL OK $sym" >> "$HERE/out/symbols.txt"
    else
        echo "SYMBOL MISSING $sym" >> "$HERE/out/symbols.txt"
        echo "missing required symbol: $sym" >&2
        exit 2
    fi
done

# Build: link checked SafeCRT archive first, hardened libc archive second.
# shellcheck disable=SC2086
$CLANG -std=c99 -O1 $INCLUDE "$HERE/proof.c" \
    "$SAFECRT" "$HARDENED" -lm -pthread -o "$HERE/out/proof" \
    > "$HERE/out/build.log" 2>&1 || {
        echo "build failed" >&2
        cat "$HERE/out/build.log" >&2
        exit 2
    }

# Run the whole fork-per-case suite under the 60 second bound.
$TIMEOUT 60 "$HERE/out/proof" "$CASES" > "$HERE/out/run.log" 2>&1
rcec=$?

RESULT=$(awk '/^BLUE20 safecrt [0-9]+ [0-9]+ [0-9]+$/ {print; found=1} END{exit !found}' "$HERE/out/run.log")
if [ $rcec -ne 0 ] || [ -z "$RESULT" ]; then
    echo "BLUE20 safecrt harness did not complete cleanly (exit=$rcec)" >&2
    tail -40 "$HERE/out/run.log" >&2
    exit 1
fi

echo "$RESULT"
echo "  log: $HERE/out/run.log"

CASES_N=$(echo "$RESULT" | awk '{print $3}')
PASSED_N=$(echo "$RESULT" | awk '{print $4}')
FAILED_N=$(echo "$RESULT" | awk '{print $5}')
if [ "$CASES_N" -lt 2000 ] || [ "$FAILED_N" -ne 0 ] || [ "$PASSED_N" -ne "$CASES_N" ]; then
    exit 1
fi
exit 0
