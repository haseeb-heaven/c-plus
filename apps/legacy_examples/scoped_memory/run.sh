#!/bin/sh
# Build and run the two the inspiration language-inspired memory examples and diff their output
# byte-for-byte against the checked-in expected files.
# Prints: SCOPED_DEMO <cases> <pass> <fail>
set -u

GM_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$GM_DIR/../../.." && pwd)
RES="$GM_DIR/results"
C="$ROOT/build/c+"

# Reproducible toolchain environment for the C+ driver.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export PATH
unset SDKROOT
unset CPLUS_CLANG

cd "$ROOT" || exit 1
mkdir -p "$RES"

cases=0
passed=0
failed=0

run_case() {
    name=$1
    cases=$((cases + 1))

    echo "=== BUILD $name ==="
    if timeout 180 "$C" build "$GM_DIR/$name.cp" --default --no-werror --sanitize=none \
            -I. -o "$RES/$name" -- build/libc/libcplus_hardened.a \
            >"$RES/$name.build.log" 2>&1; then
        :
    else
        echo "FAIL $name build"
        cat "$RES/$name.build.log"
        failed=$((failed + 1))
        return
    fi

    echo "=== RUN $name ==="
    if timeout 30 "$RES/$name" >"$RES/$name.out" 2>"$RES/$name.err"; then
        :
    else
        echo "FAIL $name run exit=$?"
        cat "$RES/$name.out"
        cat "$RES/$name.err"
        failed=$((failed + 1))
        return
    fi

    if [ -s "$RES/$name.err" ]; then
        echo "FAIL $name stderr not empty"
        cat "$RES/$name.err"
        failed=$((failed + 1))
        return
    fi

    if diff -u "$GM_DIR/expected/$name.txt" "$RES/$name.out" \
            >"$RES/$name.diff" 2>&1; then
        :
    else
        echo "FAIL $name expected-output diff"
        cat "$RES/$name.diff"
        failed=$((failed + 1))
        return
    fi

    cat "$RES/$name.out"
    passed=$((passed + 1))
}

run_case stdlib_scoped_stack
run_case stable_pool

echo "SCOPED_DEMO $cases $passed $failed"
if [ "$failed" -ne 0 ]; then
    exit 1
fi
exit 0
