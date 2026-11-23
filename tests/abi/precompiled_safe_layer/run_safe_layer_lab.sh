#!/bin/sh
# Top-level driver for the "safe layer needs no library rebuild" lab.
#   sh tests/abi/precompiled_safe_layer/run_safe_layer_lab.sh
# Everything generated lives under tests/abi/precompiled_safe_layer/artifacts/.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)

echo "== stage 1: build the plain-C library ONCE =="
sh "$HERE/build_lib_once.sh" || exit 1

echo
echo "== stage 2: build 2 callers + negative against the SAME archive, run+compare =="
sh "$HERE/build_and_test_callers.sh" || exit 1

echo
echo "safe_layer_lab: PASS"
exit 0
