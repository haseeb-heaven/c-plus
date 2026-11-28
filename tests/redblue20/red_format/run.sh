#!/bin/sh
# RED20 format red-team -- reproducible generate/build/run with a forked-child harness.
# All artifacts stay inside this directory.  No source/header/build edits.
set -eu
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$HERE"

ARCH="$ROOT/build/safe/libcplus_safecrt.a"
CORE="$ROOT/build/libcplus_core.a"
INC="-I$ROOT/stdlib/safe -I$ROOT/third_party/safeclib/include -I$ROOT/third_party/intel-safestringlib/include"
CC=${CC:-/usr/bin/clang}
CASES=${RED20_CASES:-3600}

mkdir -p results

echo "== [1/5] toolchain + symbols =="
"$CC" --version | head -2
echo "arch: $(uname -m)"
if [ ! -f "$ARCH" ]; then
  sh "$ROOT/scripts/build_safecrt.sh" >/dev/null
fi
nm -g "$ARCH" | grep -E ' T _cplus_(snprintf_s|vsnprintf_s|sprintf_s|vsprintf_s|printf_s|fprintf_s|scanf_s|fscanf_s|sscanf_s|fopen_s|fclose_s|fgets_s_checked|fread_s_checked|fwrite_s_checked|io_stream_is_closed)$' \
  | sed 's/^/  /' | tee results/nm_evidence.txt

echo "== [2/5] build both object-size variants =="
COMMON="-O1 -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-format -Wno-format-security $INC"
# format-first: safe_format.h defines CPLUS_SAFE_OBJ_SIZE as BOS(p,1)
"$CC" $COMMON -DRED20_OBJ_TYPE=1 harness.c "$ARCH" "$CORE" -pthread -o harness_format_first
# io-first: safe_io.h defines CPLUS_SAFE_OBJ_SIZE as BOS(p,0)
"$CC" $COMMON -DRED20_OBJ_TYPE=0 -DRED20_IO_FIRST harness.c "$ARCH" "$CORE" -pthread -o harness_io_first

echo "== [3/5] run format-first variant ($CASES cases, 60s cap) =="
set +e
/usr/bin/timeout 60 ./harness_format_first "$CASES" results/format_first.tsv > results/format_first.out 2> results/format_first.err
RC1=$?
/usr/bin/timeout 60 ./harness_io_first "$CASES" results/io_first.tsv > results/io_first.out 2> results/io_first.err
RC2=$?
set -e
echo "  format-first: $(cat results/format_first.out)  exit=$RC1"
echo "  io-first:     $(cat results/io_first.out)  exit=$RC2"
echo "  stderr bytes: format=$(wc -c < results/format_first.err) io=$(wc -c < results/io_first.err)"

echo "== [4/5] aggregate =="
TOTAL=$(awk '
  /^RED20 format / { c+=$3; k+=$4; r+=$5; u+=$6; f+=$7 }
  END { printf "RED20 format %d %d %d %d %d\n", c, k, r, u, f }
' results/format_first.out results/io_first.out)
echo "$TOTAL" | tee results/summary.txt

echo "== [5/5] findings digest =="
if [ -f results/format_first.tsv ]; then
  echo "format-first non-REFUSED: $(wc -l < results/format_first.tsv)"
  head -8 results/format_first.tsv | sed 's/^/  /'
fi
if [ -f results/io_first.tsv ]; then
  echo "io-first non-REFUSED: $(wc -l < results/io_first.tsv)"
  head -8 results/io_first.tsv | sed 's/^/  /'
fi
shasum -a 256 "$HERE/harness.c" "$HERE/run.sh" results/format_first.tsv results/io_first.tsv \
  | tee results/digests.sha256

# Final machine-readable line (aggregated across both variants).
echo "$TOTAL"
exit 0
