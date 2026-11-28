#!/bin/sh
# RED20 safecrt -- reproducible build + run of the forked-child red-team harness.
# All artifacts stay inside this directory. No source/header/build edits, no git ops.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$HERE"

SAFE="$ROOT/build/safe/libcplus_safecrt.a"
HARD="$ROOT/build/libc/libcplus_hardened.a"
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
CC=${CC:-/usr/bin/clang}

mkdir -p results

echo "== [1/5] toolchain + symbols =="
"$CC" --version | head -2
echo "arch: $(uname -m)  pagesize: $(getconf PAGESIZE)"
nm -g "$SAFE" 2>/dev/null | grep -E ' T _cplus_(sc_)?(memcpy_s|strcpy_s|memcmp_s_auto|remaining_capacity)' | head -8 | sed 's/^/  /'

echo "== [2/4] build harness + reproducers =="
"$CC" -O1 -g -std=c99 -Wall -Wextra $INC harness.c "$SAFE" "$HARD" -pthread -o harness
"$CC" -O0 -g -std=c99 $INC repro.c "$SAFE" "$HARD" -pthread -o repro

echo "== [3/5] run >=3000 cases (60s cap) =="
set +e
ulimit -c 0
timeout 60 ./harness results/scenarios.log 14 > results/harness.stdout 2> results/harness.stderr
HRC=$?
set -e
cat results/harness.stdout
echo "-- findings/stderr (first 60) --"
head -60 results/harness.stderr
echo "harness exit=$HRC"

echo "== [4/5] standalone reproducers =="
./repro null || true
./repro oob || true
(./repro unterm; echo "unterm exit=$?") || true

echo "== [5/5] triage helpers =="
echo "cases logged: $(grep -c '^[0-9]' results/scenarios.log)"
echo "by outcome:"; awk '{print $7}' results/scenarios.log | sort | uniq -c
echo "UNEXPECTED_OK by api:"; grep 'UNEXPECTED_OK' results/scenarios.log | awk '{print $2}' | sort | uniq -c
shasum -a 256 results/scenarios.log | tee results/digests.sha256
exit "$HRC"
