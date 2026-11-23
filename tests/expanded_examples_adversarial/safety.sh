#!/bin/sh
# Adversarial driver: self-contained safety demos under examples/expanded/safety/.
#
# These demos take no input; each one constructs its own hostile scenario
# (one-over write / out-of-range span / stale generation / double release) and
# must report a REFUSED outcome with its canary/sentinel intact. Hostile argv
# is injected to verify the demo ignores it without crashing.
set -u
. "$(dirname -- "$0")/common.sh" || exit 1
make_fixtures

BIG=$(cat "$FIX/oversized_10k.txt")
SIZEMAX=18446744073709551615

build_one memcpy_s_demo examples/expanded/safety/memcpy_s_demo.cp -- \
    "$ROOT/build/safe/libcplus_safecrt.a" \
    "$ROOT/build/libc/libcplus_hardened.a" || true
build_one bounds_demo   examples/expanded/safety/bounds_demo.cp   || true
build_one lifetime_demo examples/expanded/safety/lifetime_demo.cp || true

safety_cases() {
    _demo=$1
    _stoken=$2
    _rtoken=$3
    [ -x "$BIN/$_demo" ] || return 0
    ADV_SURVIVE_TOKEN="$_stoken" ADV_REQUIRE_TOKEN="$_rtoken" \
        run_case "$_demo" oversized_argv SURVIVE "$BIN/$_demo" "$BIG"
    ADV_SURVIVE_TOKEN="$_stoken" ADV_REQUIRE_TOKEN="$_rtoken" \
        run_case "$_demo" size_max_argv SURVIVE "$BIN/$_demo" "$SIZEMAX"
    ADV_SURVIVE_TOKEN="$_stoken" ADV_REQUIRE_TOKEN="$_rtoken" \
        run_case "$_demo" empty_argv SURVIVE "$BIN/$_demo" ""
    ADV_SURVIVE_TOKEN="$_stoken" ADV_REQUIRE_TOKEN="$_rtoken" \
        run_case "$_demo" null_like_argv SURVIVE "$BIN/$_demo" "NULL"
}

safety_cases memcpy_s_demo 'checked_one_over REFUSED' 'parent_canary OK'
safety_cases bounds_demo 'bounds one_over REFUSED' 'bounds stale_generation REFUSED'
safety_cases lifetime_demo 'lifetime stale_token_refused REFUSED' \
    'lifetime double_release_refused REFUSED'

subtotal safety
