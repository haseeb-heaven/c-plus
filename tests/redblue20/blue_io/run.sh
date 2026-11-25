#!/bin/sh
# BLUE-TEAM agent 13/20 - blue_io
# Rebuild a proof-local hardened yyjson (explicit reader/writer depth caps),
# compile proof.cp with the project C+ compiler, run 2400 fixed-seed forked
# cases under 60 s per-case bounds, check the case log and reproducibility.
# STRICT OWNERSHIP: this script writes only under tests/redblue20/blue_io/.
set -u
SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
cd "$ROOT" || exit 2
export PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH"
unset SDKROOT || true

BLUE="$ROOT/tests/redblue20/blue_io"
OUT="$BLUE/out"
mkdir -p "$OUT"

YYFLAGS="-DYYJSON_READER_DEPTH_LIMIT=16384 -DYYJSON_WRITER_DEPTH_LIMIT=16384"
YYFLAGS="$YYFLAGS -DYYJSON_DISABLE_NON_STANDARD=0 -DYYJSON_DISABLE_UTF8_VALIDATION=0"

# 1. Local yyjson with compile-time depth caps and all validation enabled.
# shellcheck disable=SC2086
/usr/bin/clang -O2 -std=c11 $YYFLAGS -I"$ROOT/third_party/yyjson" \
  -c "$ROOT/third_party/yyjson/yyjson.c" -o "$OUT/yyjson_hardened.o" \
  >"$OUT/yyjson.build.log" 2>&1
if [ $? -ne 0 ]; then
  echo "YYJSON_BUILD_FAILED" >&2
  exit 2
fi
/usr/bin/libtool -static -o "$OUT/libyyjson_hardened.a" \
  "$OUT/yyjson_hardened.o" >>"$OUT/yyjson.build.log" 2>&1
if [ $? -ne 0 ]; then
  echo "YYJSON_ARCHIVE_FAILED" >&2
  exit 2
fi

# 2. Compile the proof driver. Default mode: STRICT currently rejects
#    pre-existing header internals in stdlib/json.hp and yyjson.h.
timeout 600 ./build/c+ build "$BLUE/proof.cp" --default --sanitize=none \
  -DYYJSON_READER_DEPTH_LIMIT=16384 -DYYJSON_WRITER_DEPTH_LIMIT=16384 \
  -I. -Istdlib/safe -Ithird_party/yyjson \
  -L"$OUT" -lyyjson_hardened \
  -Lbuild/safe -lcplus_safecrt -Lbuild/libc -lcplus_hardened -pthread \
  -o "$OUT/proof" >"$OUT/proof.build.log" 2>&1
if [ $? -ne 0 ]; then
  echo "PROOF_BUILD_FAILED" >&2
  exit 2
fi

# 3. Two independent bounded runs in a private scratch dir; only paths differ.
SCRATCH=$(mktemp -d "$OUT/scratch.XXXXXX")
trap 'rm -rf "$SCRATCH"' EXIT HUP INT TERM
mkdir -p "$SCRATCH/run1" "$SCRATCH/run2"
/usr/bin/timeout 400 "$OUT/proof" "$SCRATCH/run1" "$SCRATCH/cases1.log" \
  >"$OUT/run1.stdout" 2>"$OUT/run1.stderr"
rc1=$?
/usr/bin/timeout 400 "$OUT/proof" "$SCRATCH/run2" "$SCRATCH/cases2.log" \
  >"$OUT/run2.stdout" 2>"$OUT/run2.stderr"
rc2=$?

cp "$SCRATCH/cases1.log" "$OUT/cases.log"
cp "$SCRATCH/cases2.log" "$OUT/cases2.log"

expected="BLUE20 io 2400 2400 0"
result1=$(head -n 1 "$OUT/run1.stdout")
result2=$(head -n 1 "$OUT/run2.stdout")
echo "$result1"
if [ "$result1" != "$expected" ] || [ "$rc1" -ne 0 ]; then
  echo "RUN1_FAILED rc=$rc1 result=$result1" >&2
  exit 3
fi
if [ "$result2" != "$expected" ] || [ "$rc2" -ne 0 ]; then
  echo "RUN2_FAILED rc=$rc2 result=$result2" >&2
  exit 3
fi
lines1=$(grep -c '^C ' "$OUT/cases.log")
lines2=$(grep -c '^C ' "$OUT/cases2.log")
if [ "$lines1" -ne 2400 ] || [ "$lines2" -ne 2400 ]; then
  echo "LOG_SHAPE_FAILED $lines1 $lines2" >&2
  exit 4
fi
if ! cmp -s "$OUT/cases.log" "$OUT/cases2.log"; then
  echo "NON_DETERMINISTIC_LOGS" >&2
  exit 5
fi
if ! grep -q '^DIGEST ' "$OUT/cases.log"; then
  echo "MISSING_DIGEST" >&2
  exit 4
fi
printf 'cases=%s passed=%s failed=%s rc1=%s rc2=%s\n' \
  "$lines1" 2400 0 "$rc1" "$rc2"
printf 'digest=%s\n' "$(grep '^DIGEST ' "$OUT/cases.log")"
shasum -a 256 "$OUT/cases.log"
exit 0
