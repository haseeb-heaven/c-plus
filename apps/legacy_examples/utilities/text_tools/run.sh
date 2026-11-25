#!/bin/sh
# Deterministic build-and-run harness for the five real-world text utilities.
#
#   sh apps/legacy_examples/utilities/text_tools/run.sh
#
# Every compiler and program invocation is bounded by `timeout 30`.  Fixtures
# are generated here from fixed bytes in a private temp directory; no clock,
# network, interactive prompt or user path is involved.  Binaries and logs are
# written under .run/ inside the repository (the C+ driver needs an in-repo
# output path).  A case passes only when exit code, empty stderr and exact
# stdout bytes all match.  "refused" counts hostile cases that were correctly
# rejected with the exact expected refusal line and a non-zero exit code.
#
# Final line: UTIL_TEXT <tools> <passed> <failed> <refused>
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$DIR/.run"
KILL_AFTER=30

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 2
FIX=$(mktemp -d "${TMPDIR:-/tmp}/util-text.XXXXXX") || exit 2
trap 'rm -rf "$FIX"' 0 1 2 3 15

if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
fi
if ! command -v python3 >/dev/null 2>&1; then
  echo "error: python3 not found; refusing to fabricate binary fixtures" >&2
  exit 2
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS" >&2
  exit 2
fi

cd "$ROOT" || exit 2

tools=5
passed=0
failed=0
refused=0
tool_ok=1
case_ok=0

build_tool() {
  name=$1
  timeout -k 5 "$KILL_AFTER" "$CPLUS" build "$DIR/$name.cp" \
    --strict --sanitize=none -I. -o "$OUT/$name" \
    >"$OUT/$name.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ]; then
    echo "BUILD-FAIL $name (rc=$brc)"
    tail -n 10 "$OUT/$name.build.log" | sed 's/^/           /'
    tool_ok=0
    return 1
  fi
  echo "build ok   $name"
  return 0
}

run_case() {
  label=$1
  want_rc=$2
  want_file=$3
  shift 3
  rm -f "$OUT/$label.out" "$OUT/$label.err"
  timeout -k 5 "$KILL_AFTER" "$@" \
    >"$OUT/$label.out" 2>"$OUT/$label.err" </dev/null
  got_rc=$?
  ok=1
  [ "$got_rc" -eq "$want_rc" ] || ok=0
  if [ -s "$OUT/$label.err" ]; then ok=0; fi
  if ! cmp -s "$OUT/$label.out" "$want_file"; then ok=0; fi
  case_ok=$ok
  if [ "$ok" -eq 1 ]; then
    echo "ok         $label"
  else
    echo "FAIL       $label (rc=$got_rc want=$want_rc)"
    diff -u "$want_file" "$OUT/$label.out" 2>/dev/null | head -n 16 \
      | sed 's/^/           /'
    head -n 5 "$OUT/$label.err" | sed 's/^/           stderr: /'
    tool_ok=0
  fi
  return 0
}

check() {
  run_case "$@"
}

refuse() {
  run_case "$@"
  if [ "$case_ok" -eq 1 ]; then
    refused=$((refused + 1))
  fi
}

finish_tool() {
  if [ "$tool_ok" -eq 1 ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
  tool_ok=1
}

# ---- fixed fixtures -------------------------------------------------------
printf 'alpha beta gamma\ndelta alpha\nomega\n' >"$FIX/small.txt"
printf 'one two\nthree' >"$FIX/no_final_nl.txt"
: >"$FIX/empty.txt"
awk 'BEGIN { for (i = 0; i < 300; i++) printf "a"; printf "\n" }' >"$FIX/longline.txt"
head -c 65537 /dev/zero >"$FIX/oversize.bin"
head -c 4097 /dev/zero >"$FIX/b64over.bin"
printf 'hello world' >"$FIX/hw.bin"
printf '\000\001\002' >"$FIX/bytes3.bin"
printf 'abc\000\377' >"$FIX/bin5.bin"
python3 -c 'import sys;sys.stdout.buffer.write(bytes(range(256)))' >"$FIX/all.bin"
python3 -c 'import sys;sys.stdout.buffer.write(bytes(4096))' >"$FIX/max.bin"
python3 -c 'import base64,sys;sys.stdout.write(base64.b64encode(bytes(range(256))).decode())' >"$FIX/all.tok"
python3 -c 'import base64,sys;sys.stdout.write(base64.b64encode(bytes(4096)).decode())' >"$FIX/max.tok"
python3 -c 'import sys;sys.stdout.write(bytes(range(256)).hex())' >"$FIX/all.hex"
LONGPAT=$(awk 'BEGIN { for (i = 0; i < 64; i++) printf "x" }')
LONGSET_A=$(awk 'BEGIN { for (i = 0; i < 64; i++) printf "x" }')
LONGSET_B=$(awk 'BEGIN { for (i = 0; i < 64; i++) printf "y" }')
printf 'aGVsbG8gd29ybGQ=' >"$FIX/good.tok"
printf 'aGVsbG8gd29ybGQ=\n' >"$FIX/good_nl.tok"
printf 'YWJjAP8=' >"$FIX/bin5.tok"
printf 'abc!' >"$FIX/bad.tok"
printf 'Zg' >"$FIX/short.tok"
printf 'Zh==' >"$FIX/noncanon.tok"
printf 'Zg==AAAA' >"$FIX/midpad.tok"

# ---- twc ------------------------------------------------------------------
echo "== twc =="
tool_ok=1
if build_tool twc; then
  TWC_BIN="$OUT/twc"
  printf '%s\n' 'TWC 3 6 35' >"$FIX/exp_twc_small"
  check twc_small 0 "$FIX/exp_twc_small" "$TWC_BIN" "$FIX/small.txt"

  printf '%s\n' 'TWC 2 3 13' >"$FIX/exp_twc_no_final_nl"
  check twc_no_final_nl 0 "$FIX/exp_twc_no_final_nl" "$TWC_BIN" "$FIX/no_final_nl.txt"

  printf '%s\n' 'TWC 0 0 0' >"$FIX/exp_twc_empty"
  check twc_empty 0 "$FIX/exp_twc_empty" "$TWC_BIN" "$FIX/empty.txt"

  printf '%s\n' 'TWC error cannot-open' >"$FIX/exp_twc_missing"
  refuse twc_missing 1 "$FIX/exp_twc_missing" "$TWC_BIN" "$FIX/does-not-exist"

  printf '%s\n' 'TWC error usage' >"$FIX/exp_twc_usage"
  refuse twc_usage 2 "$FIX/exp_twc_usage" "$TWC_BIN"

  printf '%s\n' 'TWC refuse oversize' >"$FIX/exp_twc_oversize"
  refuse twc_oversize 1 "$FIX/exp_twc_oversize" "$TWC_BIN" "$FIX/oversize.bin"
fi
finish_tool

# ---- tgrep ----------------------------------------------------------------
echo "== tgrep =="
tool_ok=1
if build_tool tgrep; then
  TG_BIN="$OUT/tgrep"
  printf '%s\n' \
    'TGREP 0 alpha beta gamma' \
    'TGREP 1 delta alpha' \
    'TGREP matches 2' >"$FIX/exp_tgrep_literal"
  check tgrep_literal 0 "$FIX/exp_tgrep_literal" "$TG_BIN" "$FIX/small.txt" alpha

  printf '%s\n' 'TGREP matches 0' >"$FIX/exp_tgrep_none"
  check tgrep_none 0 "$FIX/exp_tgrep_none" "$TG_BIN" "$FIX/small.txt" zzz

  printf '%s\n' \
    'TGREP 0 alpha beta gamma' \
    'TGREP 1 delta alpha' \
    'TGREP matches 2' >"$FIX/exp_tgrep_wild"
  check tgrep_wild 0 "$FIX/exp_tgrep_wild" "$TG_BIN" "$FIX/small.txt" 'a*a'

  printf '%s\n' 'TGREP error usage' >"$FIX/exp_tgrep_usage"
  refuse tgrep_usage 2 "$FIX/exp_tgrep_usage" "$TG_BIN" "$FIX/small.txt"

  printf '%s\n' 'TGREP refuse pattern' >"$FIX/exp_tgrep_pattern"
  refuse tgrep_pattern 1 "$FIX/exp_tgrep_pattern" "$TG_BIN" "$FIX/small.txt" "$LONGPAT"

  printf '%s\n' 'TGREP refuse line-too-long' >"$FIX/exp_tgrep_longline"
  refuse tgrep_longline 1 "$FIX/exp_tgrep_longline" "$TG_BIN" "$FIX/longline.txt" a

  printf '%s\n' 'TGREP error cannot-open' >"$FIX/exp_tgrep_missing"
  refuse tgrep_missing 1 "$FIX/exp_tgrep_missing" "$TG_BIN" "$FIX/does-not-exist" a
fi
finish_tool

# ---- thead ----------------------------------------------------------------
echo "== thead =="
tool_ok=1
if build_tool thead; then
  TH_BIN="$OUT/thead"
  printf '%s\n' \
    'THEAD 0 alpha beta gamma' \
    'THEAD 1 delta alpha' \
    'THEAD lines 2' >"$FIX/exp_thead_2"
  check thead_2 0 "$FIX/exp_thead_2" "$TH_BIN" "$FIX/small.txt" 2

  printf '%s\n' 'THEAD 0 alpha beta gamma' 'THEAD lines 1' >"$FIX/exp_thead_1"
  check thead_1 0 "$FIX/exp_thead_1" "$TH_BIN" "$FIX/small.txt" 1

  printf '%s\n' \
    'THEAD 0 alpha beta gamma' \
    'THEAD 1 delta alpha' \
    'THEAD 2 omega' \
    'THEAD lines 3' >"$FIX/exp_thead_all"
  check thead_all 0 "$FIX/exp_thead_all" "$TH_BIN" "$FIX/small.txt" 3

  printf '%s\n' 'THEAD refuse N' >"$FIX/exp_thead_refuse"
  refuse thead_zero 1 "$FIX/exp_thead_refuse" "$TH_BIN" "$FIX/small.txt" 0
  refuse thead_neg 1 "$FIX/exp_thead_refuse" "$TH_BIN" "$FIX/small.txt" -3
  refuse thead_big 1 "$FIX/exp_thead_refuse" "$TH_BIN" "$FIX/small.txt" 1001
  refuse thead_nonnumeric 1 "$FIX/exp_thead_refuse" "$TH_BIN" "$FIX/small.txt" abc
  refuse thead_trailing 1 "$FIX/exp_thead_refuse" "$TH_BIN" "$FIX/small.txt" 2x

  printf '%s\n' 'THEAD refuse line-too-long' >"$FIX/exp_thead_longline"
  refuse thead_longline 1 "$FIX/exp_thead_longline" "$TH_BIN" "$FIX/longline.txt" 1

  printf '%s\n' 'THEAD error usage' >"$FIX/exp_thead_usage"
  refuse thead_usage 2 "$FIX/exp_thead_usage" "$TH_BIN" "$FIX/small.txt"
fi
finish_tool

# ---- ttr ------------------------------------------------------------------
echo "== ttr =="
tool_ok=1
if build_tool ttr; then
  TR_BIN="$OUT/ttr"
  printf '%s\n' \
    'TTR XlphX YetX gXmmX' \
    'TTR deltX XlphX' \
    'TTR omegX' \
    'TTR lines 3' >"$FIX/exp_ttr_map"
  check ttr_map 0 "$FIX/exp_ttr_map" "$TR_BIN" "$FIX/small.txt" abc XYZ

  printf '%s\n' \
    'TTR alpha beta gamma' \
    'TTR delta alpha' \
    'TTR omega' \
    'TTR lines 3' >"$FIX/exp_ttr_identity"
  check ttr_identity 0 "$FIX/exp_ttr_identity" "$TR_BIN" "$FIX/small.txt" a a

  printf '%s\n' 'TTR refuse sets' >"$FIX/exp_ttr_refuse_sets"
  refuse ttr_mismatch 1 "$FIX/exp_ttr_refuse_sets" "$TR_BIN" "$FIX/small.txt" ab X
  refuse ttr_longset 1 "$FIX/exp_ttr_refuse_sets" "$TR_BIN" \
    "$FIX/small.txt" "$LONGSET_A" "$LONGSET_B"

  printf '%s\n' 'TTR refuse line-too-long' >"$FIX/exp_ttr_longline"
  refuse ttr_longline 1 "$FIX/exp_ttr_longline" "$TR_BIN" \
    "$FIX/longline.txt" a b

  printf '%s\n' 'TTR error usage' >"$FIX/exp_ttr_usage"
  refuse ttr_usage 2 "$FIX/exp_ttr_usage" "$TR_BIN" "$FIX/small.txt" a
fi
finish_tool

# ---- tbase64 --------------------------------------------------------------
echo "== tbase64 =="
tool_ok=1
if build_tool tbase64; then
  B64_BIN="$OUT/tbase64"

  printf '%s\n' 'TBASE64 aGVsbG8gd29ybGQ=' >"$FIX/exp_b64_enc_hw"
  check b64_enc_hw 0 "$FIX/exp_b64_enc_hw" "$B64_BIN" enc "$FIX/hw.bin"

  printf '%s\n' 'TBASE64 AAEC' >"$FIX/exp_b64_enc_bytes"
  check b64_enc_bytes 0 "$FIX/exp_b64_enc_bytes" "$B64_BIN" enc "$FIX/bytes3.bin"

  printf '%s\n' 'TBASE64 YWJjAP8=' >"$FIX/exp_b64_enc_bin5"
  check b64_enc_bin5 0 "$FIX/exp_b64_enc_bin5" "$B64_BIN" enc "$FIX/bin5.bin"

  printf '%s\n' 'TBASE64 11 68656c6c6f20776f726c64' >"$FIX/exp_b64_dec_hw"
  check b64_dec_hw 0 "$FIX/exp_b64_dec_hw" "$B64_BIN" dec "$FIX/good.tok"
  check b64_dec_nl 0 "$FIX/exp_b64_dec_hw" "$B64_BIN" dec "$FIX/good_nl.tok"

  printf '%s\n' 'TBASE64 5 61626300ff' >"$FIX/exp_b64_dec_bin5"
  check b64_dec_bin5 0 "$FIX/exp_b64_dec_bin5" "$B64_BIN" dec "$FIX/bin5.tok"

  { printf 'TBASE64 '; cat "$FIX/all.tok"; printf '\n'; } >"$FIX/exp_b64_all_enc"
  { printf 'TBASE64 256 '; cat "$FIX/all.hex"; printf '\n'; } >"$FIX/exp_b64_all_dec"
  { printf 'TBASE64 '; cat "$FIX/max.tok"; printf '\n'; } >"$FIX/exp_b64_max_enc"
  check b64_all_enc 0 "$FIX/exp_b64_all_enc" "$B64_BIN" enc "$FIX/all.bin"
  check b64_max_enc 0 "$FIX/exp_b64_max_enc" "$B64_BIN" enc "$FIX/max.bin"
  check b64_all_roundtrip 0 "$FIX/exp_b64_all_dec" sh -c \
    '"$1" enc "$2" | sed "s/^TBASE64 //" > "$3"; "$1" dec "$3"' \
    b64rt "$B64_BIN" "$FIX/all.bin" "$FIX/all.tok"

  printf '%s\n' 'TBASE64 refuse malformed' >"$FIX/exp_b64_malformed"
  refuse b64_dec_bad 1 "$FIX/exp_b64_malformed" "$B64_BIN" dec "$FIX/bad.tok"
  refuse b64_dec_short 1 "$FIX/exp_b64_malformed" "$B64_BIN" dec "$FIX/short.tok"
  refuse b64_dec_noncanon 1 "$FIX/exp_b64_malformed" "$B64_BIN" dec "$FIX/noncanon.tok"
  refuse b64_dec_midpad 1 "$FIX/exp_b64_malformed" "$B64_BIN" dec "$FIX/midpad.tok"

  printf '%s\n' 'TBASE64 refuse input-too-large' >"$FIX/exp_b64_oversize"
  refuse b64_enc_oversize 1 "$FIX/exp_b64_oversize" "$B64_BIN" enc "$FIX/b64over.bin"

  printf '%s\n' 'TBASE64 error cannot-open' >"$FIX/exp_b64_missing"
  refuse b64_missing 1 "$FIX/exp_b64_missing" "$B64_BIN" enc "$FIX/does-not-exist"

  printf '%s\n' 'TBASE64 error usage' >"$FIX/exp_b64_usage"
  refuse b64_usage_mode 2 "$FIX/exp_b64_usage" "$B64_BIN" nope "$FIX/hw.bin"
  refuse b64_usage_arity 2 "$FIX/exp_b64_usage" "$B64_BIN" enc
fi
finish_tool

printf 'UTIL_TEXT %d %d %d %d\n' "$tools" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
