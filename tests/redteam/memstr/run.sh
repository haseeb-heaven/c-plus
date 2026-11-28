#!/bin/sh
# REDTEAM memstr -- reproducible generate/build/run with forked-child harness.
# All artifacts stay inside this directory. No source/header/build edits.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$HERE"

ARCH="$ROOT/build/safe/libcplus_safecrt.a"
CORE="$ROOT/build/libcplus_core.a"
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
CC=/usr/bin/clang

mkdir -p results

echo "== [1/5] toolchain + symbols (nm) =="
"$CC" --version | head -2
echo "arch: $(uname -m)"
nm -g "$ARCH" | grep -E ' T _cplus_(sc_)?(memcpy_s|memmove_s|memset_s|memcmp_s|memzero_s|strcpy_s|strncpy_s|strcat_s|strncat_s|strnlen_s|strcmp_s|strtok_s)$' | sed 's/^/  /' > results/nm_evidence.txt
cat results/nm_evidence.txt

echo "== [2/5] generate deterministic cases =="
python3 attack_cases.py | tee results/generator.out

echo "== [3/5] build fork harness =="
"$CC" -O1 -std=c99 -Wall -Wextra $INC attack_harness.c "$ARCH" "$CORE" -pthread -o attack_harness

echo "== [4/5] run (\"findings are reported, not hidden\") =="
set +e
ulimit -c 0
timeout 300 ./attack_harness > results/harness.stdout 2> results/harness.stderr
HRC=$?
set -e
cat results/harness.stdout
echo "-- findings/stderr (first 40) --"
head -40 results/harness.stderr
echo "harness exit=$HRC"

echo "== [5/5] log + digest =="
shasum -a 256 results/attack_cases.tsv results/scenarios.log | tee results/digests.sha256
echo "scenario lines: $(grep -c '^[0-9]' results/scenarios.log)"
exit "$HRC"
