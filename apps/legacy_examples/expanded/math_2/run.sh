#!/bin/sh
# Deterministic build-and-run harness for the four math/encoding apps.
#
#   sh apps/legacy_examples/expanded/math_2/run.sh
#
# Every compiler and program invocation is bounded by `timeout 30`. Each
# scenario must match exit code, empty stderr and byte-exact stdout. Refusal
# scenarios are counted separately from ordinary passing scenarios.
#
# Final line: MATH_2 <apps> <passed> <failed> <refused>
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
  echo "build ok   $name"
  return 0
}

verify() {
  label=$1
  want_rc=$2
  want_file=$3
  got_rc=$4
  hostile=$5
  ok=1
  [ "$got_rc" -eq "$want_rc" ] || ok=0
  if [ -s "$OUT/$label.err" ]; then ok=0; fi
  if ! cmp -s "$OUT/$label.out" "$want_file"; then ok=0; fi
  if [ "$ok" -eq 1 ]; then
    if [ "$hostile" -eq 1 ]; then
      echo "refused    $label"
      refused=$((refused + 1))
    else
      echo "ok         $label"
    fi
  else
    echo "FAIL       $label (rc=$got_rc want=$want_rc)"
    diff -u "$want_file" "$OUT/$label.out" 2>/dev/null | head -n 20 \
      | sed 's/^/           /'
    head -n 5 "$OUT/$label.err" | sed 's/^/           stderr: /'
    app_status=0
  fi
}

check() {
  label=$1
  want_rc=$2
  want_file=$3
  hostile=$4
  shift 4
  rm -f "$OUT/$label.out" "$OUT/$label.err"
  timeout -k 5 "$KILL_AFTER" "$@" \
    >"$OUT/$label.out" 2>"$OUT/$label.err" </dev/null
  verify "$label" "$want_rc" "$want_file" "$?" "$hostile"
}

finish_app() {
  if [ "$app_status" -eq 1 ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
  app_status=1
}

# ---- expected transcripts (values independently recomputed before writing) --
cat >"$OUT/exp_bi_default" <<'EOF'
BIGINT add 999999999999999 + 1 = 1000000000000000
BIGINT add 1234567890123456 + 7 = 1234567890123463
BIGINT sub 1000000000000000 - 1 = 999999999999999
BIGINT mul 99999 * 99999 = 9999800001
BIGINT mul 1234567 * 89 = 109876463
BIGINT OK count=5 cap=16 ops= 5
EOF
printf '%s\n' 'BIGINT REFUSED code= 101 overflow-input' >"$OUT/exp_bi_input"
printf '%s\n' 'BIGINT REFUSED code= 101 overflow-result' >"$OUT/exp_bi_result"
printf '%s\n' 'BIGINT REFUSED code= 101 underflow' >"$OUT/exp_bi_under"
printf '%s\n' 'BIGINT REFUSED code= 101 digit-syntax' >"$OUT/exp_bi_digit"
printf '%s\n' 'BIGINT REFUSED code= 108 bad-mode' >"$OUT/exp_bi_mode"

cat >"$OUT/exp_pr_default" <<'EOF'
PRNG seed=305419896 count=16 cap=256
PRNG 0 raw=2274908837 bounded=5
PRNG 1 raw=358294691 bounded=3
PRNG 2 raw=1210119364 bounded=4
PRNG 3 raw=2176035992 bounded=0
PRNG 4 raw=1882851208 bounded=0
PRNG 5 raw=698933837 bounded=5
PRNG 6 raw=2311737117 bounded=5
PRNG 7 raw=3306712617 bounded=1
PRNG 8 raw=3547882151 bounded=7
PRNG 9 raw=984697617 bounded=1
PRNG 10 raw=1774140920 bounded=0
PRNG 11 raw=1309615608 bounded=0
PRNG 12 raw=4240336032 bounded=0
PRNG 13 raw=895871509 bounded=5
PRNG 14 raw=2361839302 bounded=6
PRNG 15 raw=4141526377 bounded=1
PRNG sum=43 OK
EOF
cat >"$OUT/exp_pr_seed1" <<'EOF'
PRNG seed=1 count=8 cap=256
PRNG 0 raw=270369 bounded=1
PRNG 1 raw=67634689 bounded=1
PRNG 2 raw=2647435461 bounded=5
PRNG 3 raw=307599695 bounded=7
PRNG 4 raw=2398689233 bounded=1
PRNG 5 raw=745495504 bounded=0
PRNG 6 raw=632435482 bounded=2
PRNG 7 raw=435756210 bounded=2
PRNG sum=19 OK
EOF
printf '%s\n' 'PRNG REFUSED code= 101 seed-overflow' >"$OUT/exp_pr_seedov"
printf '%s\n' 'PRNG REFUSED code= 101 count-too-large' >"$OUT/exp_pr_countbig"
printf '%s\n' 'PRNG REFUSED code= 108 seed-syntax' >"$OUT/exp_pr_seedsyn"
printf '%s\n' 'PRNG REFUSED code= 101 count-overflow' >"$OUT/exp_pr_countov"
printf '%s\n' 'PRNG REFUSED code= 108 usage' >"$OUT/exp_pr_usage"

cat >"$OUT/exp_ck_default" <<'EOF'
CHECKSUM len=0 text= crc32=00000000 adler32=00000001
CHECKSUM len=9 text=123456789 crc32=cbf43926 adler32=091e01de
CHECKSUM len=43 text=The quick brown fox jumps over the lazy dog crc32=414fa339 adler32=5bdc0fda
CHECKSUM len=3 text=abc crc32=352441c2 adler32=024d0127
CHECKSUM OK count=4 cap=128 n= 4
EOF
printf '%s\n' 'CHECKSUM len=5 text=hello crc32=3610a686 adler32=062c0215' >"$OUT/exp_ck_hello"
printf '%s\n' 'CHECKSUM REFUSED code= 101 input-too-long' >"$OUT/exp_ck_long"
printf '%s\n' 'CHECKSUM REFUSED code= 108 bad-mode' >"$OUT/exp_ck_mode"

cat >"$OUT/exp_b64_default" <<'EOF'
BASE64 enc "" ->  rt=OK len=0
BASE64 enc "f" -> Zg== rt=OK len=1
BASE64 enc "fo" -> Zm8= rt=OK len=2
BASE64 enc "foo" -> Zm9v rt=OK len=3
BASE64 enc "foob" -> Zm9vYg== rt=OK len=4
BASE64 enc "fooba" -> Zm9vYmE= rt=OK len=5
BASE64 enc "foobar" -> Zm9vYmFy rt=OK len=6
BASE64 OK count=7 cap=96 n= 7
EOF
printf '%s\n' 'BASE64 enc "foobar" -> Zm9vYmFy rt=OK len=6' >"$OUT/exp_b64_enc"
printf '%s\n' 'BASE64 dec Zm9vYmFy len=6 hex=666f6f626172' >"$OUT/exp_b64_dec"
printf '%s\n' 'BASE64 REFUSED code= 101 input-too-long' >"$OUT/exp_b64_long"
printf '%s\n' 'BASE64 REFUSED code= 101 encoded-too-long' >"$OUT/exp_b64_enclong"
printf '%s\n' 'BASE64 REFUSED code= 108 malformed-encoding' >"$OUT/exp_b64_bad"
printf '%s\n' 'BASE64 REFUSED code= 108 malformed-length' >"$OUT/exp_b64_badlen"

LONG=$(head -c 129 /dev/zero | tr '\0' 'A')

# ---- bigint ----------------------------------------------------------------
echo "== bigint =="
if build_app bigint; then
  B="$OUT/bigint"
  check bi_default 0 "$OUT/exp_bi_default" 0 "$B"
  check bi_input   1 "$OUT/exp_bi_input"   1 "$B" input-overflow
  check bi_result  1 "$OUT/exp_bi_result"  1 "$B" result-overflow
  check bi_under   1 "$OUT/exp_bi_under"   1 "$B" sub-underflow
  check bi_mul     1 "$OUT/exp_bi_result"  1 "$B" mul-overflow
  check bi_digit   1 "$OUT/exp_bi_digit"   1 "$B" bad-digit
  check bi_mode    1 "$OUT/exp_bi_mode"    1 "$B" bogus
fi
finish_app

# ---- prng ------------------------------------------------------------------
echo "== prng =="
if build_app prng; then
  B="$OUT/prng"
  check pr_default 0 "$OUT/exp_pr_default" 0 "$B"
  check pr_seed1   0 "$OUT/exp_pr_seed1"   0 "$B" 1 8
  check pr_seedov  1 "$OUT/exp_pr_seedov"  1 "$B" 4294967296 4
  check pr_big     1 "$OUT/exp_pr_countbig" 1 "$B" 1 257
  check pr_syn     1 "$OUT/exp_pr_seedsyn" 1 "$B" 1x 4
  check pr_countov 1 "$OUT/exp_pr_countov" 1 "$B" 1 99999999999999999999999
  check pr_usage   1 "$OUT/exp_pr_usage"   1 "$B" 1
fi
finish_app

# ---- checksum --------------------------------------------------------------
echo "== checksum =="
if build_app checksum; then
  B="$OUT/checksum"
  check ck_default 0 "$OUT/exp_ck_default" 0 "$B"
  check ck_hello   0 "$OUT/exp_ck_hello"   0 "$B" text hello
  check ck_long    1 "$OUT/exp_ck_long"    1 "$B" oversized
  check ck_longarg 1 "$OUT/exp_ck_long"    1 "$B" text "$LONG"
  check ck_mode    1 "$OUT/exp_ck_mode"    1 "$B" bogus
fi
finish_app

# ---- base64 ----------------------------------------------------------------
echo "== base64 =="
if build_app base64; then
  B="$OUT/base64"
  check b64_default 0 "$OUT/exp_b64_default" 0 "$B"
  check b64_enc     0 "$OUT/exp_b64_enc"     0 "$B" enc foobar
  check b64_dec     0 "$OUT/exp_b64_dec"     0 "$B" dec Zm9vYmFy
  check b64_long    1 "$OUT/exp_b64_long"    1 "$B" oversized
  check b64_enclong 1 "$OUT/exp_b64_enclong" 1 "$B" encoversized
  check b64_bad     1 "$OUT/exp_b64_bad"     1 "$B" malformed 'Zm9*'
  check b64_badlen  1 "$OUT/exp_b64_badlen"  1 "$B" badlen Zm9vZ
fi
finish_app

printf 'MATH_2 %d %d %d %d\n' "$apps" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
