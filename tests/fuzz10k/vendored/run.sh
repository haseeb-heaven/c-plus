#!/bin/sh
# Deterministic 10k+ fuzz over all 26 vendored cplus_v_* functions.
# Builds with /usr/bin/clang -std=c99 and both checked archives, then
# runs the forked-subprocess harness under `timeout 600`.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
CC=/usr/bin/clang

cd "$HERE"
mkdir -p bin logs

"$CC" -O1 -g -std=c99 -Wall -Wextra -Wno-unused-parameter \
  "$HERE/fuzz10k_vendored.c" \
  "$ROOT/build/libc/libcplus_hardened.a" \
  "$ROOT/build/safe/libcplus_safecrt.a" \
  -lm -pthread -o "$HERE/bin/fuzz10k_vendored"

timeout 600 "$HERE/bin/fuzz10k_vendored"
