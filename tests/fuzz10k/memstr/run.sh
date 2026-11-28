#!/bin/sh
# Reproducible build + run for the FUZZ10K memstr evidence suite.
#
# Runs 10,240 deterministic fuzz scenarios against the checked memory/string
# APIs in build/safe/libcplus_safecrt.a, with a forked child per scenario so a
# crash or hang is recorded without killing the harness.  Everything produced
# here lives in this directory; no library sources or build outputs are touched.
set -eu

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$HERE"

ARCH="$ROOT/build/safe/libcplus_safecrt.a"
CORE="$ROOT/build/libcplus_core.a"
INC="-I$ROOT -I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
CC=${CPLUS_CLANG:-/usr/bin/clang}
BASEFLAGS="-O1 -g -std=c99 -pedantic -Wall -Wextra $INC"
LIBS="$ARCH $CORE -pthread"

mkdir -p results

echo "== [1/6] toolchain =="
echo "CC=$CC"
$CC --version | head -1
echo "ARCH=$ARCH"
echo "CORE=$CORE"

echo "== [2/6] source digests =="
shasum -a 256 fuzz10k_memstr.c run.sh

echo "== [3/6] symbol verification (nm, defined T symbols) =="
nm -g "$ARCH" | grep -E ' T _cplus_(sc_)?(memcpy_s|memmove_s|memset_s|memcmp_s|memzero_s|strcpy_s|strncpy_s|strcat_s|strncat_s|strnlen_s|strcmp_s)$' | sed 's/^/  /'
# 11 cplus_sc_*_s + 11 cplus_*_s = 22 entry points under test.
NSYM=$(nm -g "$ARCH" | grep -Ec ' T _cplus_(sc_)?(memcpy_s|memmove_s|memset_s|memcmp_s|memzero_s|strcpy_s|strncpy_s|strcat_s|strncat_s|strnlen_s|strcmp_s)$')
echo "  verified entry-point symbols: $NSYM"
[ "$NSYM" -eq 22 ]

echo "== [4/6] build + primary run (timeout 600) =="
echo "$CC $BASEFLAGS fuzz10k_memstr.c $LIBS -o fuzz10k_memstr"
# shellcheck disable=SC2086
$CC $BASEFLAGS fuzz10k_memstr.c $LIBS -o fuzz10k_memstr
set +e
timeout 600 ./fuzz10k_memstr > results/stdout.log 2> results/stderr.log
RC=$?
set -e
cat results/stdout.log
echo "-- stderr --"
cat results/stderr.log
echo "exit=$RC"

echo "== [5/6] scenario log digest + UBSan replay =="
shasum -a 256 results/scenarios.log > results/scenarios.sha256
cat results/scenarios.sha256
echo "$CC $BASEFLAGS -fsanitize=undefined -fno-sanitize-recover=all fuzz10k_memstr.c $LIBS -o fuzz10k_memstr_ubsan"
# shellcheck disable=SC2086
$CC $BASEFLAGS -fsanitize=undefined -fno-sanitize-recover=all fuzz10k_memstr.c $LIBS -o fuzz10k_memstr_ubsan
set +e
timeout 600 ./fuzz10k_memstr_ubsan > results/ubsan_stdout.log 2> results/ubsan_stderr.log
URC=$?
set -e
cat results/ubsan_stdout.log
echo "-- ubsan stderr --"
cat results/ubsan_stderr.log
echo "ubsan_exit=$URC"

echo "== [6/6] sanity checks =="
LINES=$(grep -c '^S ' results/scenarios.log)
APIS=$(awk '{print $3}' results/scenarios.log | sort -u | wc -l | tr -d ' ')
CASES=$(awk '{print $4}' results/scenarios.log | sort -u | wc -l | tr -d ' ')
echo "scenario lines=$LINES unique APIs=$APIS unique cases=$CASES"
[ "$LINES" -eq 10240 ]
[ "$APIS" -eq 22 ]
[ "$CASES" -eq 7 ]
grep -q '^FUZZ10K memstr 10240 0 0 0 ' results/stdout.log
[ "$RC" -eq 0 ]
[ "$URC" -eq 0 ]
grep -q '^FUZZ10K memstr 10240 0 0 0 ' results/ubsan_stdout.log

echo "fuzz10k run: PASS (primary + UBSan, 10240 scenarios each)"
exit "$RC"
