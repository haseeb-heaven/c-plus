#!/bin/sh
# Build and run the four 100%-public-method suites for net/http + adaptation
# headers.  Prints one "<header> <method> PASS|FAIL" line per method from each
# binary, then the aggregate "METHOD_TEST net_http <total> <passed> <failed>
# <skipped>" line.  Exits non-zero if any build, run or method fails.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
CPLUS="$ROOT/build/c+"
OUT=${METHOD_TEST_OUT:-${TMPDIR:-/tmp}/cplus_method_tests_net_http}
mkdir -p "$OUT"
cd "$ROOT" || exit 1

TOTAL=0; PASSED=0; FAILED=0; SKIPPED=0
RC=0

run_suite() {
    _name=$1; _source=$2; _mode=$3; _extra=$4
    _bin="$OUT/$_name"
    if ! "$CPLUS" build "$_source" $_mode --sanitize=none $_extra -o "$_bin" \
            >"$OUT/$_name.build.log" 2>&1; then
        printf '%s BUILD FAIL\n' "$_name"
        cat "$OUT/$_name.build.log"
        RC=1
        return
    fi
    timeout 30 "$_bin" >"$OUT/$_name.stdout" 2>"$OUT/$_name.stderr"
    _rc=$?
    cat "$OUT/$_name.stdout"
    if [ -s "$OUT/$_name.stderr" ]; then
        printf 'stderr(%s):\n' "$_name"
        cat "$OUT/$_name.stderr"
    fi
    if [ "$_rc" -ne 0 ]; then
        printf '%s RUN FAIL exit=%s\n' "$_name" "$_rc"
        RC=1
    fi
    _p=$(grep -c ' PASS$' "$OUT/$_name.stdout" || true)
    _f=$(grep -c ' FAIL$' "$OUT/$_name.stdout" || true)
    _s=$(grep -c ' SKIPPED' "$OUT/$_name.stdout" || true)
    PASSED=$((PASSED + _p)); FAILED=$((FAILED + _f)); SKIPPED=$((SKIPPED + _s))
    TOTAL=$((TOTAL + _p + _f + _s))
}

# These suites deliberately exercise unsafe runtime paths (double drop, null
# handles, stale views) that the STRICT compile-time checker now rejects at
# build time.  Run them in --default (the mode that actually reaches the runtime
# refusals under test); STRICT remains the mode for safe production code.
run_suite net      tests/stdlib/method_tests/net_http/methods_net.cp            --default ''
run_suite gsl      tests/stdlib/method_tests/net_http/methods_gsl_adapt.cp      --default ''
run_suite lifetime tests/stdlib/method_tests/net_http/methods_lifetime_adapt.cp --default ''
# http.hp cannot be lowered under --strict (CPLUS-ARITH at http.hp:106:30);
# the repository's own run_stdlib_tests.sh builds it with the default checker.
run_suite http     tests/stdlib/method_tests/net_http/methods_http.cp           --default '-lcurl'

printf 'METHOD_TEST net_http %d %d %d %d\n' "$TOTAL" "$PASSED" "$FAILED" "$SKIPPED"
exit "$RC"
