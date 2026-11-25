#!/bin/sh
# Deterministic build-and-run harness for examples/expanded/apps_2.
#
#   sh examples/expanded/apps_2/run.sh
#
# Every compiler and program invocation is bounded by `timeout 30`. All inputs
# are fixed argv values or fixed fixture bytes generated here; no clock, no
# network, no interactive prompt and no user path is involved. A scenario
# passes only when the exit code matches, stderr is empty and stdout matches
# the expected bytes exactly (cmp). A time-out is never a pass.
#
# Final line: APPS_2 <apps> <passed> <failed> <refused>
#   passed  = apps whose every scenario passed
#   failed  = apps with at least one failing scenario
#   refused = hostile scenarios that were correctly refused (non-zero exit)
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
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
  extra=""
  if [ "$name" = "json_query" ]; then
    extra="-Ithird_party/yyjson -Lbuild/ext -lyyjson"
  fi
  # shellcheck disable=SC2086
  timeout -k 5 "$KILL_AFTER" "$CPLUS" build "$DIR/$name.cp" \
    --strict --sanitize=none -I. $extra -o "$OUT/$name" \
    >"$OUT/$name.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ]; then
    echo "BUILD-FAIL $name (rc=$brc)"
    tail -n 12 "$OUT/$name.build.log" | sed 's/^/           /'
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
  ok=1
  [ "$got_rc" -eq "$want_rc" ] || ok=0
  if [ -s "$OUT/$label.err" ]; then ok=0; fi
  if ! cmp -s "$OUT/$label.out" "$want_file"; then ok=0; fi
  if [ "$ok" -eq 1 ]; then
    echo "ok         $label"
    return 0
  fi
  echo "FAIL       $label (rc=$got_rc want=$want_rc)"
  diff -u "$want_file" "$OUT/$label.out" 2>/dev/null | head -n 20 \
    | sed 's/^/           /'
  head -n 5 "$OUT/$label.err" | sed 's/^/           stderr: /'
  app_status=0
  return 1
}

check() {
  label=$1
  want_rc=$2
  want_file=$3
  shift 3
  rm -f "$OUT/$label.out" "$OUT/$label.err"
  timeout -k 5 "$KILL_AFTER" "$@" \
    >"$OUT/$label.out" 2>"$OUT/$label.err" </dev/null
  verify "$label" "$want_rc" "$want_file" "$?"
}

# A hostile case: expected non-zero exit. Count it as refused only when the
# full verification passes (exit code, empty stderr, exact stdout bytes).
refuse() {
  label=$1
  want_file=$2
  shift 2
  if check "$label" 1 "$want_file" "$@"; then
    refused=$((refused + 1))
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

# ---- deterministic fixtures ----------------------------------------------
LONG=$(awk 'BEGIN { while (n++ < 257) printf "a" }')
DEEP=$(awk 'BEGIN { for (i = 0; i < 9; i++) printf "["; printf "0"; for (i = 0; i < 9; i++) printf "]" }')
FJ='{"user":{"name":"ada","age":36,"active":true},"items":[{"tag":"x"},{"tag":"y"}],"n":7}'
printf '10,20,30\n4,5,6\n7,8,9\n' >"$OUT/good.csv"
printf '10,20,30\n4,5\n7,8,9\n' >"$OUT/ragged.csv"
printf '10,abc,30\n' >"$OUT/nonnum.csv"
printf '9223372036854775807,0,0\n9223372036854775807,0,0\n' >"$OUT/overflow.csv"
printf '%s\n' '-3,0,0' '-6,0,0' >"$OUT/neg.csv"

# ---- vault ----------------------------------------------------------------
echo "== vault =="
if build_app vault; then
  V="$OUT/vault"
  printf '%s\n' \
    "VAULT add mail" \
    "VAULT add notes" \
    "VAULT get mail s3cr3t" \
    "VAULT list 2" \
    "VAULT entry 0 mail" \
    "VAULT entry 1 notes" \
    "VAULT delete mail" \
    "VAULT list 1" \
    "VAULT entry 0 notes" >"$OUT/exp_vault_flow"
  check vault_flow 0 "$OUT/exp_vault_flow" "$V" \
    add mail s3cr3t -- add notes opensesame -- get mail -- list \
    -- delete mail -- list

  printf '%s\n' \
    "VAULT add a" \
    "VAULT get a 1" \
    "VAULT list 1" \
    "VAULT entry 0 a" >"$OUT/exp_vault_secret"
  # 'list' must not leak the password: the expected bytes omit it.
  check vault_secret 0 "$OUT/exp_vault_secret" "$V" \
    add a 1 -- get a -- list

  printf '%s\n' "VAULT add a" "VAULT error duplicate" >"$OUT/exp_vault_dup"
  refuse vault_dup "$OUT/exp_vault_dup" "$V" add a 1 -- add a 2

  printf '%s\n' "VAULT error missing-key" >"$OUT/exp_vault_missing"
  refuse vault_missing "$OUT/exp_vault_missing" "$V" get nope

  printf '%s\n' "VAULT error empty-password" >"$OUT/exp_vault_empty"
  refuse vault_empty_pw "$OUT/exp_vault_empty" "$V" add a ''

  printf '%s\n' "VAULT error name-too-long" >"$OUT/exp_vault_long_name"
  refuse vault_long_name "$OUT/exp_vault_long_name" "$V" add "$LONG" pw

  printf '%s\n' "VAULT error password-too-long" >"$OUT/exp_vault_long_pw"
  refuse vault_long_pw "$OUT/exp_vault_long_pw" "$V" add svc "$LONG"

  printf '%s\n' "VAULT error usage" >"$OUT/exp_vault_usage"
  check vault_usage 2 "$OUT/exp_vault_usage" "$V"
fi
finish_app

# ---- json_query -----------------------------------------------------------
echo "== json_query =="
if build_app json_query; then
  J="$OUT/json_query"

  printf '%s\n' "JSON value user.name ada" >"$OUT/exp_jq_string"
  check jq_string 0 "$OUT/exp_jq_string" "$J" "$FJ" user.name

  printf '%s\n' "JSON value user.age 36" >"$OUT/exp_jq_int"
  check jq_int 0 "$OUT/exp_jq_int" "$J" "$FJ" user.age

  printf '%s\n' "JSON value user.active true" >"$OUT/exp_jq_bool"
  check jq_bool 0 "$OUT/exp_jq_bool" "$J" "$FJ" user.active

  printf '%s\n' "JSON value items.1.tag y" >"$OUT/exp_jq_array"
  check jq_array 0 "$OUT/exp_jq_array" "$J" "$FJ" items.1.tag

  printf '%s\n' "JSON value n 7" >"$OUT/exp_jq_root"
  check jq_root 0 "$OUT/exp_jq_root" "$J" "$FJ" n

  printf '%s\n' "JSON error parse" >"$OUT/exp_jq_malformed"
  refuse jq_malformed "$OUT/exp_jq_malformed" "$J" '{oops' x

  printf '%s\n' "JSON error parse" >"$OUT/exp_jq_truncated"
  refuse jq_truncated "$OUT/exp_jq_truncated" "$J" '{"a":1' a

  printf '%s\n' "JSON error depth" >"$OUT/exp_jq_depth"
  refuse jq_depth "$OUT/exp_jq_depth" "$J" "$DEEP" 0

  printf '%s\n' "JSON error missing-key" >"$OUT/exp_jq_missing"
  refuse jq_missing "$OUT/exp_jq_missing" "$J" "$FJ" user.missing

  printf '%s\n' "JSON error bad-index" >"$OUT/exp_jq_index"
  refuse jq_index "$OUT/exp_jq_index" "$J" "$FJ" items.9.tag

  printf '%s\n' "JSON error bad-path" >"$OUT/exp_jq_path"
  refuse jq_path "$OUT/exp_jq_path" "$J" "$FJ" n.x

  printf '%s\n' "JSON error usage" >"$OUT/exp_jq_usage"
  check jq_usage 2 "$OUT/exp_jq_usage" "$J"
fi
finish_app

# ---- csv_tool -------------------------------------------------------------
echo "== csv_tool =="
if build_app csv_tool; then
  C="$OUT/csv_tool"

  printf '%s\n' "CSV sum 0 21" "CSV rows 3" >"$OUT/exp_csv_sum"
  check csv_sum 0 "$OUT/exp_csv_sum" "$C" "$OUT/good.csv" 0 sum

  printf '%s\n' "CSV avg 2 15" "CSV rows 3" >"$OUT/exp_csv_avg"
  check csv_avg 0 "$OUT/exp_csv_avg" "$C" "$OUT/good.csv" 2 avg

  printf '%s\n' "CSV avg 0 -4" "CSV rows 2" >"$OUT/exp_csv_neg"
  check csv_neg_avg 0 "$OUT/exp_csv_neg" "$C" "$OUT/neg.csv" 0 avg

  printf '%s\n' "CSV error column-out-of-range" >"$OUT/exp_csv_col"
  refuse csv_col "$OUT/exp_csv_col" "$C" "$OUT/good.csv" 3 sum

  printf '%s\n' "CSV error malformed-row" >"$OUT/exp_csv_ragged"
  refuse csv_ragged "$OUT/exp_csv_ragged" "$C" "$OUT/ragged.csv" 0 sum

  printf '%s\n' "CSV error not-numeric" >"$OUT/exp_csv_nonnum"
  refuse csv_nonnum "$OUT/exp_csv_nonnum" "$C" "$OUT/nonnum.csv" 1 sum

  printf '%s\n' "CSV error overflow" >"$OUT/exp_csv_overflow"
  refuse csv_overflow "$OUT/exp_csv_overflow" "$C" "$OUT/overflow.csv" 0 sum

  printf '%s\n' "CSV error cannot-open" >"$OUT/exp_csv_missing"
  refuse csv_missing "$OUT/exp_csv_missing" "$C" "$OUT/does-not-exist.csv" 0 sum

  printf '%s\n' "CSV error usage" >"$OUT/exp_csv_usage"
  check csv_usage 2 "$OUT/exp_csv_usage" "$C"
fi
finish_app

# ---- matrix ---------------------------------------------------------------
echo "== matrix =="
if build_app matrix; then
  M="$OUT/matrix"

  printf '%s\n' \
    "MAT add 2 2" \
    "MAT 0 0 6" "MAT 0 1 8" "MAT 1 0 10" "MAT 1 1 12" >"$OUT/exp_m_add"
  check m_add 0 "$OUT/exp_m_add" "$M" \
    add 2 2 1,2,3,4 2 2 5,6,7,8

  printf '%s\n' \
    "MAT multiply 2 2" \
    "MAT 0 0 58" "MAT 0 1 64" "MAT 1 0 139" "MAT 1 1 154" >"$OUT/exp_m_mul"
  check m_mul 0 "$OUT/exp_m_mul" "$M" \
    multiply 2 3 1,2,3,4,5,6 3 2 7,8,9,10,11,12

  printf '%s\n' "MAT determinant 2 -2" >"$OUT/exp_m_det2"
  check m_det2 0 "$OUT/exp_m_det2" "$M" determinant 2 2 1,2,3,4

  printf '%s\n' "MAT determinant 4 1" >"$OUT/exp_m_det4"
  check m_det4 0 "$OUT/exp_m_det4" "$M" det 4 4 \
    1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1

  printf '%s\n' "MAT error dim-mismatch" >"$OUT/exp_m_dim_add"
  refuse m_dim_add "$OUT/exp_m_dim_add" "$M" \
    add 2 2 1,2,3,4 2 3 1,2,3,4,5,6

  printf '%s\n' "MAT error dim-mismatch" >"$OUT/exp_m_dim_mul"
  refuse m_dim_mul "$OUT/exp_m_dim_mul" "$M" \
    multiply 2 3 1,2,3,4,5,6 2 2 1,2,3,4

  printf '%s\n' "MAT error oversized-dims" >"$OUT/exp_m_oversized"
  refuse m_oversized "$OUT/exp_m_oversized" "$M" \
    add 5 2 1,2,3,4,5,6,7,8,9,10 5 2 1,2,3,4,5,6,7,8,9,10

  printf '%s\n' "MAT error bad-dims" >"$OUT/exp_m_bad_dims"
  refuse m_bad_dims "$OUT/exp_m_bad_dims" "$M" \
    add 0 2 1,2 1 2 3,4

  printf '%s\n' "MAT error bad-data" >"$OUT/exp_m_bad_data"
  refuse m_bad_data "$OUT/exp_m_bad_data" "$M" \
    add 2 2 1,2,3 2 2 5,6,7,8

  printf '%s\n' "MAT error not-square" >"$OUT/exp_m_not_square"
  refuse m_not_square "$OUT/exp_m_not_square" "$M" \
    determinant 2 3 1,2,3,4,5,6

  printf '%s\n' "MAT error overflow" >"$OUT/exp_m_overflow"
  refuse m_overflow "$OUT/exp_m_overflow" "$M" determinant 4 4 \
    1000000000,1000000000,1000000000,1000000000,\
1000000000,1000000000,1000000000,1000000000,\
1000000000,1000000000,1000000000,1000000000,\
1000000000,1000000000,1000000000,1000000000

  printf '%s\n' "MAT error usage" >"$OUT/exp_m_usage"
  check m_usage 2 "$OUT/exp_m_usage" "$M"
fi
finish_app

printf 'APPS_2 %d %d %d %d\n' "$apps" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
