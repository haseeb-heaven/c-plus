#!/bin/sh
# Deterministic build-and-run harness for the four data_2 apps.
#
#   sh apps/legacy_examples/expanded/data_2/run.sh
#
# Every compiler and program invocation is bounded by `timeout 30`. All input
# is fixed argv, generated here from fixed bytes. No clock, network, interactive
# prompt or user path is involved. A scenario passes only when the exit code
# matches, stderr is empty and stdout matches the expected bytes exactly.
# A scenario that expects exit 2 is a hostile case that must be REFUSED.
#
# Final line: DATA_2 <apps> <passed> <failed> <refused>
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$DIR/.run"
KILL_AFTER=30

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 2
if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS" >&2
  exit 2
fi

cd "$ROOT" || exit 2

apps=4
passed=0
failed=0
refused=0
app_status=1

# Pure-POSIX fixed-string repeaters (no python, no seq).
rep() {
  _c=$1; _n=$2; _out=''
  while [ "$_n" -gt 0 ]; do _out="$_out$_c"; _n=$((_n - 1)); done
  printf '%s' "$_out"
}
repstr() {
  _s=$1; _n=$2; _out=''
  while [ "$_n" -gt 0 ]; do _out="$_out$_s"; _n=$((_n - 1)); done
  printf '%s' "$_out"
}
A64=$(rep a 64)
A65=$(rep a 65)
A129=$(rep a 129)
CTL=$(printf 'a\001b')
STEP_PAT=$(repstr '*a' 20)
STEP_TXT="$(rep a 120)b"

build_app() {
  name=$1
  timeout -k 5 "$KILL_AFTER" "$CPLUS" build "$DIR/$name.cp" \
    --strict --sanitize=none -I. -o "$OUT/$name" \
    >"$OUT/$name.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ]; then
    echo "BUILD-FAIL $name (rc=$brc)"
    tail -n 8 "$OUT/$name.build.log" | sed 's/^/           /'
    app_status=0
    return 1
  fi
  if [ ! -x "$OUT/$name" ]; then
    echo "BUILD-FAIL $name (no binary)"
    app_status=0
    return 1
  fi
  echo "build ok   $name"
  return 0
}

check() {
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
  if [ "$ok" -eq 1 ]; then
    echo "ok         $label"
    if [ "$want_rc" -ne 0 ]; then
      refused=$((refused + 1))
    fi
  else
    echo "FAIL       $label (rc=$got_rc want=$want_rc)"
    diff -u "$want_file" "$OUT/$label.out" 2>/dev/null | head -n 20 \
      | sed 's/^/           /'
    head -n 5 "$OUT/$label.err" | sed 's/^/           stderr: /'
    app_status=0
  fi
}

finish_app() {
  if [ "$app_status" -eq 1 ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
  app_status=1
}

# ---- sort_tool ------------------------------------------------------------
echo "== sort_tool =="
if build_app sort_tool; then
  SORT_BIN="$OUT/sort_tool"

  cat >"$OUT/exp_sort_int_asc" <<'EOF'
SORT app=sort_tool mode=int order=asc n=5 max_items=24 max_token=24
SORT in 0=5
SORT in 1=3
SORT in 2=9
SORT in 3=3
SORT in 4=1
SORT out 0=1 src=4
SORT out 1=3 src=1
SORT out 2=3 src=3
SORT out 3=5 src=0
SORT out 4=9 src=2
SORT check sorted=1 stable=1 cmp=9
SORT fp=81
SORT done
EOF
  check sort_int_asc 0 "$OUT/exp_sort_int_asc" "$SORT_BIN" int asc 5 3 9 3 1

  cat >"$OUT/exp_sort_int_desc" <<'EOF'
SORT app=sort_tool mode=int order=desc n=5 max_items=24 max_token=24
SORT in 0=5
SORT in 1=3
SORT in 2=9
SORT in 3=3
SORT in 4=1
SORT out 0=9 src=2
SORT out 1=5 src=0
SORT out 2=3 src=1
SORT out 3=3 src=3
SORT out 4=1 src=4
SORT check sorted=1 stable=1 cmp=5
SORT fp=45
SORT done
EOF
  check sort_int_desc 0 "$OUT/exp_sort_int_desc" "$SORT_BIN" int desc 5 3 9 3 1

  cat >"$OUT/exp_sort_str_asc" <<'EOF'
SORT app=sort_tool mode=str order=asc n=4 max_items=24 max_token=24
SORT in 0=banana
SORT in 1=apple
SORT in 2=cherry
SORT in 3=apple
SORT out 0=apple src=1
SORT out 1=apple src=3
SORT out 2=banana src=0
SORT out 3=cherry src=2
SORT check sorted=1 stable=1 cmp=5
SORT fp=981
SORT done
EOF
  check sort_str_asc 0 "$OUT/exp_sort_str_asc" "$SORT_BIN" \
    str asc banana apple cherry apple

  cat >"$OUT/exp_sort_str_desc" <<'EOF'
SORT app=sort_tool mode=str order=desc n=4 max_items=24 max_token=24
SORT in 0=banana
SORT in 1=apple
SORT in 2=cherry
SORT in 3=apple
SORT out 0=cherry src=2
SORT out 1=banana src=0
SORT out 2=apple src=1
SORT out 3=apple src=3
SORT check sorted=1 stable=1 cmp=4
SORT fp=974
SORT done
EOF
  check sort_str_desc 0 "$OUT/exp_sort_str_desc" "$SORT_BIN" \
    str desc banana apple cherry apple

  printf '%s\n' 'SORT error code=101 reason=too-many-items' \
    >"$OUT/exp_sort_over"
  check sort_over 2 "$OUT/exp_sort_over" "$SORT_BIN" int asc \
    1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25

  printf '%s\n' 'SORT error code=101 reason=token-too-long' \
    >"$OUT/exp_sort_token"
  check sort_token_over 2 "$OUT/exp_sort_token" "$SORT_BIN" \
    str asc "$A65" ok

  printf '%s\n' 'SORT error code=108 reason=invalid-integer' \
    >"$OUT/exp_sort_int_bad"
  check sort_int_bad 2 "$OUT/exp_sort_int_bad" "$SORT_BIN" int asc 3 abc 1

  printf '%s\n' 'SORT error code=101 reason=integer-overflow' \
    >"$OUT/exp_sort_int_over"
  check sort_int_over 2 "$OUT/exp_sort_int_over" "$SORT_BIN" \
    int asc 999999999999

  printf '%s\n' 'SORT error code=108 reason=invalid-string' \
    >"$OUT/exp_sort_str_bad"
  check sort_str_bad 2 "$OUT/exp_sort_str_bad" "$SORT_BIN" \
    str asc "$CTL" ok

  printf '%s\n' 'SORT error code=108 reason=bad-mode' >"$OUT/exp_sort_badmode"
  check sort_badmode 2 "$OUT/exp_sort_badmode" "$SORT_BIN" \
    bogus asc 1 2
fi
finish_app

# ---- hash_tool ------------------------------------------------------------
echo "== hash_tool =="
if build_app hash_tool; then
  HASH_BIN="$OUT/hash_tool"

  cat >"$OUT/exp_hash_vector" <<'EOF'
HASH app=hash_tool alg=FNV-1a-32 offset=0x811c9dc5 prime=0x01000193 max_len=64 selfcheck=ok n=3
HASH 0 len=5 hash=0x4f9f2cab text=hello
HASH 1 len=3 hash=0x1a47e90b text=abc
HASH 2 len=6 hash=0x54497523 text=OpenAI
HASH xor=0x0191b083
HASH done
EOF
  check hash_vector 0 "$OUT/exp_hash_vector" "$HASH_BIN" hello abc OpenAI

  {
    printf '%s\n' 'HASH app=hash_tool alg=FNV-1a-32 offset=0x811c9dc5 prime=0x01000193 max_len=64 selfcheck=ok n=1'
    printf 'HASH 0 len=64 hash=0xd96f0f85 text=%s\n' "$A64"
    printf '%s\n' 'HASH xor=0xd96f0f85' 'HASH done'
  } >"$OUT/exp_hash_boundary"
  check hash_boundary 0 "$OUT/exp_hash_boundary" "$HASH_BIN" "$A64"

  printf '%s\n' 'HASH error code=101 reason=input-too-long' \
    >"$OUT/exp_hash_over"
  check hash_input_over 2 "$OUT/exp_hash_over" "$HASH_BIN" "$A65"

  printf '%s\n' 'HASH error code=101 reason=too-many-inputs' \
    >"$OUT/exp_hash_count"
  check hash_count_over 2 "$OUT/exp_hash_count" "$HASH_BIN" \
    a a a a a a a a a a a a a a a a a

  printf '%s\n' 'HASH error code=108 reason=invalid-byte' \
    >"$OUT/exp_hash_bad"
  check hash_bad_byte 2 "$OUT/exp_hash_bad" "$HASH_BIN" "$CTL"
fi
finish_app

# ---- rle ------------------------------------------------------------------
echo "== rle =="
if build_app rle; then
  RLE_BIN="$OUT/rle"

  cat >"$OUT/exp_rle_enc" <<'EOF'
RLE app=rle op=enc max_raw=64 max_run=99 magic=RLE1;
RLE in_len=6 in=aaabbc
RLE group 0 byte=a run=3
RLE group 1 byte=b run=2
RLE group 2 byte=c run=1
RLE encoded=RLE1;a03;b02;c01 groups=3 enc_len=16
RLE roundtrip ok=1 decoded_len=6
RLE done
EOF
  check rle_enc 0 "$OUT/exp_rle_enc" "$RLE_BIN" enc aaabbc

  cat >"$OUT/exp_rle_dec" <<'EOF'
RLE app=rle op=dec max_raw=64 max_run=99 magic=RLE1;
RLE enc=RLE1;a03;b02;c01
RLE group 0 byte=a run=3
RLE group 1 byte=b run=2
RLE group 2 byte=c run=1
RLE decoded_len=6 decoded=aaabbc
RLE roundtrip ok=1 groups=3
RLE done
EOF
  check rle_dec 0 "$OUT/exp_rle_dec" "$RLE_BIN" dec 'RLE1;a03;b02;c01'

  {
    printf '%s\n' 'RLE app=rle op=enc max_raw=64 max_run=99 magic=RLE1;'
    printf 'RLE in_len=64 in=%s\n' "$A64"
    printf '%s\n' 'RLE group 0 byte=a run=64' \
      'RLE encoded=RLE1;a64 groups=1 enc_len=8' \
      'RLE roundtrip ok=1 decoded_len=64' 'RLE done'
  } >"$OUT/exp_rle_max"
  check rle_max 0 "$OUT/exp_rle_max" "$RLE_BIN" enc "$A64"

  # Harness-level byte-exact roundtrip: encode, extract the stream, decode it
  # again and require the exact dec transcript (including decoded=aaabbc).
  rm -f "$OUT/rle_pipe.enc"
  timeout -k 5 "$KILL_AFTER" "$RLE_BIN" enc aaabbc \
    >"$OUT/rle_pipe.enc" 2>"$OUT/rle_pipe.err" </dev/null
  ENCVAL=$(sed -n 's/^RLE encoded=\([^ ]*\) groups=.*/\1/p' "$OUT/rle_pipe.enc")
  check rle_pipeline 0 "$OUT/exp_rle_dec" "$RLE_BIN" dec "$ENCVAL"

  {
    printf '%s\n' 'RLE app=rle op=enc max_raw=64 max_run=99 magic=RLE1;'
    printf 'RLE in_len=65 in=%s\n' "$A65"
    printf '%s\n' 'RLE error code=101 reason=raw-too-long'
  } >"$OUT/exp_rle_raw_over"
  check rle_raw_over 2 "$OUT/exp_rle_raw_over" "$RLE_BIN" enc "$A65"

  cat >"$OUT/exp_rle_magic" <<'EOF'
RLE app=rle op=dec max_raw=64 max_run=99 magic=RLE1;
RLE enc=BAD1;a03
RLE error code=108 reason=bad-magic
EOF
  check rle_bad_magic 2 "$OUT/exp_rle_magic" "$RLE_BIN" dec 'BAD1;a03'

  cat >"$OUT/exp_rle_count" <<'EOF'
RLE app=rle op=dec max_raw=64 max_run=99 magic=RLE1;
RLE enc=RLE1;aXY
RLE error code=108 reason=malformed-stream
EOF
  check rle_bad_count 2 "$OUT/exp_rle_count" "$RLE_BIN" dec 'RLE1;aXY'

  cat >"$OUT/exp_rle_overflow" <<'EOF'
RLE app=rle op=dec max_raw=64 max_run=99 magic=RLE1;
RLE enc=RLE1;a99;b99;c99;d99;e99;f99;g99
RLE error code=101 reason=decoded-overflow
EOF
  check rle_dec_overflow 2 "$OUT/exp_rle_overflow" "$RLE_BIN" dec \
    'RLE1;a99;b99;c99;d99;e99;f99;g99'

  cat >"$OUT/exp_rle_trailing" <<'EOF'
RLE app=rle op=dec max_raw=64 max_run=99 magic=RLE1;
RLE enc=RLE1;a03;
RLE error code=108 reason=malformed-stream
EOF
  check rle_trailing_sep 2 "$OUT/exp_rle_trailing" "$RLE_BIN" dec 'RLE1;a03;'
fi
finish_app

# ---- regex_lite -----------------------------------------------------------
echo "== regex_lite =="
if build_app regex_lite; then
  REGEX_BIN="$OUT/regex_lite"

  cat >"$OUT/exp_regex_table" <<'EOF'
REGEX app=regex_lite max_pat=64 max_text=128 max_steps=256 selfcheck=ok cases=6
REGEX case 0 pattern=* text=anything match=1 want=1 ok=1 steps=17 backtracks=8
REGEX case 1 pattern=a?c text=abc match=1 want=1 ok=1 steps=3 backtracks=0
REGEX case 2 pattern=a?c text=ac match=0 want=0 ok=1 steps=2 backtracks=0
REGEX case 3 pattern=a*b text=aaab match=1 want=1 ok=1 steps=7 backtracks=2
REGEX case 4 pattern=a*b text=aaac match=0 want=0 ok=1 steps=8 backtracks=3
REGEX case 5 pattern=a*b?c text=aaabcc match=1 want=1 ok=1 steps=9 backtracks=2
REGEX done cases=6
EOF
  check regex_table 0 "$OUT/exp_regex_table" "$REGEX_BIN"

  cat >"$OUT/exp_regex_pair" <<'EOF'
REGEX pattern=a*b?c text=aaabcc match=1 steps=9 backtracks=2 plen=5 tlen=6
REGEX done
EOF
  check regex_pair 0 "$OUT/exp_regex_pair" "$REGEX_BIN" 'a*b?c' aaabcc

  printf '%s\n' 'REGEX error code=101 reason=pattern-too-long' \
    >"$OUT/exp_regex_pat_over"
  check regex_pattern_over 2 "$OUT/exp_regex_pat_over" "$REGEX_BIN" \
    "$A65" abc

  printf '%s\n' 'REGEX error code=101 reason=text-too-long' \
    >"$OUT/exp_regex_text_over"
  check regex_text_over 2 "$OUT/exp_regex_text_over" "$REGEX_BIN" \
    'a*' "$A129"

  printf '%s\n' 'REGEX error code=107 reason=step-budget-exhausted' \
    >"$OUT/exp_regex_steps"
  check regex_step_guard 2 "$OUT/exp_regex_steps" "$REGEX_BIN" \
    "$STEP_PAT" "$STEP_TXT"

  printf '%s\n' 'REGEX error code=108 reason=invalid-pattern' \
    >"$OUT/exp_regex_bad"
  check regex_bad_pattern 2 "$OUT/exp_regex_bad" "$REGEX_BIN" "$CTL" abc

  printf '%s\n' 'REGEX error code=108 reason=usage' >"$OUT/exp_regex_usage"
  check regex_usage 2 "$OUT/exp_regex_usage" "$REGEX_BIN" onlyone
fi
finish_app

printf 'DATA_2 %d %d %d %d\n' "$apps" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
