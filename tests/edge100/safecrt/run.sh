#!/bin/sh
# EDGE100 safecrt -- reproducible build + run of the fork-per-case edge harness.
# Owns ONLY tests/edge100/safecrt/. No source/header edits, no git ops.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$HERE"

SAFE="$ROOT/build/safe/libcplus_safecrt.a"
HARD="$ROOT/build/libc/libcplus_hardened.a"
EX4C="$ROOT/third_party/exceptions4c"
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/libc/install-include -I$EX4C"
CC=${CC:-/usr/bin/clang}

mkdir -p results

echo "== [1/4] toolchain + archives =="
"$CC" --version | head -2
ls -l "$SAFE" "$HARD" | sed 's/^/  /'
echo "arch: $(uname -m)  pagesize: $(getconf PAGESIZE)"

echo "== [2/4] build harness =="
"$CC" -O1 -g -std=c99 -Wall -Wextra $INC harness.c "$SAFE" "$HARD" -pthread -o harness

echo "== [3/4] run edge cases (bounded: alarm(3)/child, timeout 60 total) =="
set +e
ulimit -c 0
timeout 60 ./harness results/cases.log > results/summary.txt 2> results/stderr.txt
HRC=$?
set -e
cat results/summary.txt
echo "-- stderr (first 40) --"
head -40 results/stderr.txt

echo "== [4/4] triage =="
echo "cases logged: $(grep -c '^[A-Za-z_]' results/cases.log)"
echo "by verdict:"; awk '{print $3}' results/cases.log | sort | uniq -c
echo "by header group:"; awk -F. '{print $1}' results/cases.log | sort | uniq -c | head -20
shasum -a 256 results/cases.log | tee results/digests.sha256
exit "$HRC"
