#!/bin/sh
# Reproducible build+run for the ADV1000 memstr adversarial suite.
# All artifacts stay inside this directory. No compiler/library/runtime edits.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$HERE"

ARCH="$ROOT/build/safe/libcplus_safecrt.a"
CORE="$ROOT/build/libcplus_core.a"
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
CC=/usr/bin/clang

mkdir -p results

echo "== [1/5] symbol verification (nm) =="
nm -g "$ARCH" | grep -E ' T _cplus_(sc_)?(memcpy_s|memmove_s|memset_s|memcmp_s|memzero_s|strcpy_s|strncpy_s|strcat_s|strncat_s|strnlen_s|strcmp_s|strtok_s|strerror_s)$' | sed 's/^/  /'

echo "== [2/5] build (/usr/bin/clang -std=c99, both archives) =="
# "both archives" = the checked safe-CRT archive plus the C+ core archive.
# The core archive is linked for runtime symbol completeness; all 26 checked
# entry points under test are defined in libcplus_safecrt.a (verified above).
$CC -O2 -std=c99 -pedantic -Wall -Wextra $INC adv1000_memstr.c "$ARCH" "$CORE" -pthread -o adv1000_memstr

echo "== [3/5] run (timeout 120) =="
set +e
timeout 120 ./adv1000_memstr > results/stdout.log 2> results/stderr.log
RC=$?
set -e
cat results/stdout.log
echo "-- stderr --"
cat results/stderr.log
echo "exit=$RC"

echo "== [4/5] scenario log + digest =="
# The program writes exactly the 1000 'S ...' lines to results/scenarios.log;
# stdout additionally carries the aggregate line (kept in results/stdout.log).
shasum -a 256 results/scenarios.log > results/scenarios.sha256
cat results/scenarios.sha256

echo "== [5/5] sanity checks =="
LINES=$(grep -c '^S ' results/scenarios.log)
APIS=$(awk '{print $3}' results/scenarios.log | sort -u | wc -l | tr -d ' ')
CASES=$(awk '{print $4}' results/scenarios.log | sort -u | wc -l | tr -d ' ')
echo "scenario lines=$LINES unique APIs=$APIS unique cases=$CASES"
[ "$LINES" -eq 1000 ]
[ "$APIS" -eq 26 ]
[ "$CASES" -eq 7 ]
grep -q '^ADV1000 memstr 1000 ' results/stdout.log
exit "$RC"
