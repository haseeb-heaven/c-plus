#!/bin/sh
# Checked CTL span wrapper suite (issue #24). POSIX sh, bounded, exact diff.
# Prints: CTL_SPAN_TEST <cases> <pass> <fail>.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
unset SDKROOT
unset CPLUS_CLANG
D=tests/ctl
pass=0; fail=0; cases=0
if ! timeout 180 ./build/c+ build "$D/test_ctl_span.cp" --default --no-werror --sanitize=none \
      -I. -o "$D/test_ctl_span" -- build/libc/libcplus_hardened.a \
      >"$D/test_ctl_span.build.log" 2>&1; then
    echo "CTL_SPAN_TEST FAIL build"; tail -5 "$D/test_ctl_span.build.log"; exit 1
fi
timeout 30 "$D/test_ctl_span" >"$D/test_ctl_span.out" 2>"$D/test_ctl_span.err" || {
    echo "CTL_SPAN_TEST FAIL run"; cat "$D/test_ctl_span.out"; exit 1; }

cases=$((cases + 1))
if diff -u "$D/expected_ctl_span.txt" "$D/test_ctl_span.out" >/dev/null 2>&1; then
    pass=$((pass + 1))
else
    fail=$((fail + 1)); echo "CTL_SPAN_TEST FAIL diff"
fi
echo "CTL_SPAN_TEST $cases $pass $fail"
[ "$fail" -eq 0 ]
