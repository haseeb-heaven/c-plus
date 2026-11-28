#!/bin/sh
# Deterministic build-and-run harness for the five data/config utilities.
#
#   sh apps/legacy_examples/utilities/data_tools/run.sh
#
# Every compiler and program invocation is bounded by `timeout 30`. Fixtures
# are regenerated here from fixed bytes; no clock, network, interactive prompt
# or user path is involved. A scenario passes only when the exit code matches,
# stderr is empty and stdout matches the expected bytes exactly (`cmp`).
# Hostile scenarios are counted separately as "refused" only when they produce
# the exact expected refusal token with the expected nonzero exit code.
#
# Final line: UTIL_DATA <tools> <passed> <failed> <refused>
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

tools=5
passed=0
failed=0
refused=0
tool_status=1

# Long deterministic byte runs for oversized-input hostility.
LONG300=$(printf '%0300d' 0 | tr '0' 'x')
LONG2100=$(printf '%02100d' 0 | tr '0' 'x')
LONG600=$(printf '%0600d' 0 | tr '0' 'x')
LONG40=$(printf '%040d' 0 | tr '0' 'x')

build_tool() {
  name=$1
  shift
  timeout -k 5 "$KILL_AFTER" "$CPLUS" build "$DIR/$name.cp" \
    --strict --sanitize=none -I. "$@" -o "$OUT/$name" \
    >"$OUT/$name.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ]; then
    echo "BUILD-FAIL $name (rc=$brc)"
    tail -n 8 "$OUT/$name.build.log" | sed 's/^/           /'
    tool_status=0
    return 1
  fi
  echo "build ok   $name"
  return 0
}

# verify <label> <want_rc> <want_file> <got_rc> [hostile]
verify() {
  label=$1
  want_rc=$2
  want_file=$3
  got_rc=$4
  hostile=${5:-no}
  ok=1
  [ "$got_rc" -eq "$want_rc" ] || ok=0
  if [ -s "$OUT/$label.err" ]; then ok=0; fi
  if ! cmp -s "$OUT/$label.out" "$want_file"; then ok=0; fi
  if [ "$ok" -eq 1 ]; then
    echo "ok         $label"
    if [ "$hostile" = "hostile" ]; then
      refused=$((refused + 1))
    fi
  else
    echo "FAIL       $label (rc=$got_rc want=$want_rc)"
    diff -u "$want_file" "$OUT/$label.out" 2>/dev/null | head -n 20 \
      | sed 's/^/           /'
    head -n 5 "$OUT/$label.err" | sed 's/^/           stderr: /'
    tool_status=0
  fi
}

check() {
  label=$1
  want_rc=$2
  want_file=$3
  hostile=${4:-no}
  shift 4
  rm -f "$OUT/$label.out" "$OUT/$label.err"
  timeout -k 5 "$KILL_AFTER" "$@" \
    >"$OUT/$label.out" 2>"$OUT/$label.err" </dev/null
  verify "$label" "$want_rc" "$want_file" "$?" "$hostile"
}

finish_tool() {
  if [ "$tool_status" -eq 1 ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
  tool_status=1
}

# ---- deterministic fixtures ----------------------------------------------
printf 'name,value\nalpha,10\nbeta,20\ngamma,30\n' >"$OUT/good.csv"
printf 'name,value\na,-5\nb,10\n' >"$OUT/neg.csv"
printf 'name,value\na,9223372036854775807\nb,1\n' >"$OUT/over.csv"
printf 'name,value\na,12x\n' >"$OUT/nonnum.csv"
printf 'name,value\na,1,2\n' >"$OUT/badrow.csv"
{ printf 'name,value\n'; printf 'a,%s\n' "$LONG300"; } >"$OUT/long.csv"

cat >"$OUT/good.ini" <<'EOF'
# server config
[server]
host=localhost
port=8080

; client config
[client]
timeout=30
EOF
printf 'key=value\n' >"$OUT/nosec.ini"
printf '[a]\nk=1\nk=2\n' >"$OUT/dup.ini"
printf '[%s]\nk=1\n' "$LONG40" >"$OUT/longname.ini"
printf 'this is not an ini line\n' >"$OUT/badline.ini"

cat >"$OUT/good.log" <<'EOF'
2024-01-01T00:00:00Z INFO started
2024-01-01T00:00:01Z WARN disk low
2024-01-01T00:00:02Z INFO ready
2024-01-01T00:00:03Z ERROR failed
EOF
printf '2024-01-01T00:00:00Z BOGUS msg\n' >"$OUT/badlvl.log"
printf '2024/01/01T00:00:00Z INFO msg\n' >"$OUT/badts.log"
printf '2024-01-01T00:00:00Z INFO\n' >"$OUT/nomsg.log"
printf '2024-01-01T00:00:00Z INFO %s\n' "$LONG300" >"$OUT/long.log"

JSON_OBJ='{"name":"alice","age":30,"active":true,"tags":["a","b"]}'
JSON_ARR='[1,2,3]'
JSON_BAD='{"a":}'
JSON_TRUNC='{"a": 1'
JSON_DEEP='[[[[[[[[[1]]]]]]]]]'
JSON_LONG='{"k":"'"$LONG2100"'"}'

# ---- csvstat --------------------------------------------------------------
echo "== csvstat =="
if build_tool csvstat; then
  CS="$OUT/csvstat"
  printf '%s\n' \
    "CSV count 3" "CSV sum 60" "CSV min 10" "CSV max 30" >"$OUT/exp_csv_stats"
  check csv_stats 0 "$OUT/exp_csv_stats" normal "$CS" "$OUT/good.csv" stats

  printf '%s\n' "CSV sum 60" >"$OUT/exp_csv_sum"
  check csv_sum 0 "$OUT/exp_csv_sum" normal "$CS" "$OUT/good.csv" sum

  printf '%s\n' \
    "CSV count 2" "CSV sum 5" "CSV min -5" "CSV max 10" >"$OUT/exp_csv_neg"
  check csv_neg 0 "$OUT/exp_csv_neg" normal "$CS" "$OUT/neg.csv" stats

  printf '%s\n' "CSV error sum-overflow" >"$OUT/exp_csv_over"
  check csv_over 1 "$OUT/exp_csv_over" hostile "$CS" "$OUT/over.csv" stats

  printf '%s\n' "CSV error non-numeric" >"$OUT/exp_csv_nonnum"
  check csv_nonnum 1 "$OUT/exp_csv_nonnum" hostile "$CS" "$OUT/nonnum.csv" stats

  printf '%s\n' "CSV error bad-row" >"$OUT/exp_csv_badrow"
  check csv_badrow 1 "$OUT/exp_csv_badrow" hostile "$CS" "$OUT/badrow.csv" stats

  printf '%s\n' "CSV error line-too-long" >"$OUT/exp_csv_long"
  check csv_long 1 "$OUT/exp_csv_long" hostile "$CS" "$OUT/long.csv" stats
fi
finish_tool

# ---- iniparse -------------------------------------------------------------
echo "== iniparse =="
if build_tool iniparse; then
  IP="$OUT/iniparse"
  printf '%s\n' \
    "INI sections 2" "INI section 0 server" "INI section 1 client" \
    >"$OUT/exp_ini_sections"
  check ini_sections 0 "$OUT/exp_ini_sections" normal "$IP" "$OUT/good.ini" sections

  printf '%s\n' "INI value server port 8080" >"$OUT/exp_ini_get"
  check ini_get 0 "$OUT/exp_ini_get" normal "$IP" "$OUT/good.ini" get server port

  printf '%s\n' "INI keys client 1" "INI key 0 timeout" >"$OUT/exp_ini_keys"
  check ini_keys 0 "$OUT/exp_ini_keys" normal "$IP" "$OUT/good.ini" keys client

  printf '%s\n' "INI error no-section" >"$OUT/exp_ini_nosec"
  check ini_nosec 1 "$OUT/exp_ini_nosec" hostile "$IP" "$OUT/nosec.ini" sections

  printf '%s\n' "INI error duplicate-key" >"$OUT/exp_ini_dup"
  check ini_dup 1 "$OUT/exp_ini_dup" hostile "$IP" "$OUT/dup.ini" sections

  printf '%s\n' "INI error name-too-long" >"$OUT/exp_ini_long"
  check ini_long 1 "$OUT/exp_ini_long" hostile "$IP" "$OUT/longname.ini" sections

  printf '%s\n' "INI error bad-line" >"$OUT/exp_ini_badline"
  check ini_badline 1 "$OUT/exp_ini_badline" hostile "$IP" "$OUT/badline.ini" sections
fi
finish_tool

# ---- jsonfmt --------------------------------------------------------------
echo "== jsonfmt =="
if build_tool jsonfmt -Ithird_party/yyjson -Lbuild/ext -lyyjson; then
  JF="$OUT/jsonfmt"
  printf '%s\n' \
    "JSON root object" \
    "JSON {" \
    'JSON   "name": "alice",' \
    'JSON   "age": 30,' \
    'JSON   "active": true,' \
    'JSON   "tags": [' \
    'JSON     "a",' \
    'JSON     "b"' \
    'JSON   ]' \
    "JSON }" >"$OUT/exp_json_obj"
  check json_obj 0 "$OUT/exp_json_obj" normal "$JF" "$JSON_OBJ"

  printf '%s\n' \
    "JSON root array" \
    "JSON [" \
    "JSON   1," \
    "JSON   2," \
    "JSON   3" \
    "JSON ]" >"$OUT/exp_json_arr"
  check json_arr 0 "$OUT/exp_json_arr" normal "$JF" "$JSON_ARR"

  printf '%s\n' "JSON error parse" >"$OUT/exp_json_bad"
  check json_bad 1 "$OUT/exp_json_bad" hostile "$JF" "$JSON_BAD"

  printf '%s\n' "JSON error parse" >"$OUT/exp_json_trunc"
  check json_trunc 1 "$OUT/exp_json_trunc" hostile "$JF" "$JSON_TRUNC"

  printf '%s\n' "JSON error depth" >"$OUT/exp_json_deep"
  check json_deep 1 "$OUT/exp_json_deep" hostile "$JF" "$JSON_DEEP"

  printf '%s\n' "JSON error too-long" >"$OUT/exp_json_long"
  check json_long 1 "$OUT/exp_json_long" hostile "$JF" "$JSON_LONG"
fi
finish_tool

# ---- uriparse -------------------------------------------------------------
echo "== uriparse =="
if build_tool uriparse; then
  UP="$OUT/uriparse"
  printf '%s\n' \
    "URI scheme https" "URI host example.com" "URI port 8443" "URI path /a/b" \
    >"$OUT/exp_uri_full"
  check uri_full 0 "$OUT/exp_uri_full" normal "$UP" 'https://example.com:8443/a/b'

  printf '%s\n' \
    "URI scheme http" "URI host localhost" "URI port -1" "URI path -" \
    >"$OUT/exp_uri_min"
  check uri_min 0 "$OUT/exp_uri_min" normal "$UP" 'http://localhost'

  printf '%s\n' "URI error bad-port" >"$OUT/exp_uri_badport"
  check uri_badport 1 "$OUT/exp_uri_badport" hostile "$UP" 'http://localhost:70000/x'

  printf '%s\n' "URI error unsupported" >"$OUT/exp_uri_query"
  check uri_query 1 "$OUT/exp_uri_query" hostile "$UP" 'http://x.com/a?b=1'

  printf '%s\n' "URI error bad-scheme" >"$OUT/exp_uri_noscheme"
  check uri_noscheme 1 "$OUT/exp_uri_noscheme" hostile "$UP" 'example.com/path'

  URI_LONG="http://h.com/$LONG600"
  printf '%s\n' "URI error too-long" >"$OUT/exp_uri_long"
  check uri_long 1 "$OUT/exp_uri_long" hostile "$UP" "$URI_LONG"
fi
finish_tool

# ---- logparse -------------------------------------------------------------
echo "== logparse =="
if build_tool logparse; then
  LP="$OUT/logparse"
  printf '%s\n' \
    "LOG match 0 2024-01-01T00:00:00Z INFO started" \
    "LOG match 1 2024-01-01T00:00:02Z INFO ready" \
    "LOG count INFO 2" >"$OUT/exp_log_info"
  check log_info 0 "$OUT/exp_log_info" normal "$LP" "$OUT/good.log" INFO

  printf '%s\n' \
    "LOG match 0 2024-01-01T00:00:00Z INFO started" \
    "LOG match 1 2024-01-01T00:00:01Z WARN disk low" \
    "LOG match 2 2024-01-01T00:00:02Z INFO ready" \
    "LOG match 3 2024-01-01T00:00:03Z ERROR failed" \
    "LOG count all 4" >"$OUT/exp_log_all"
  check log_all 0 "$OUT/exp_log_all" normal "$LP" "$OUT/good.log" all

  printf '%s\n' "LOG error bad-level" >"$OUT/exp_log_badlvl"
  check log_badlvl 1 "$OUT/exp_log_badlvl" hostile "$LP" "$OUT/badlvl.log" all

  printf '%s\n' "LOG error bad-timestamp" >"$OUT/exp_log_badts"
  check log_badts 1 "$OUT/exp_log_badts" hostile "$LP" "$OUT/badts.log" all

  printf '%s\n' "LOG error malformed" >"$OUT/exp_log_nomsg"
  check log_nomsg 1 "$OUT/exp_log_nomsg" hostile "$LP" "$OUT/nomsg.log" all

  printf '%s\n' "LOG error line-too-long" >"$OUT/exp_log_long"
  check log_long 1 "$OUT/exp_log_long" hostile "$LP" "$OUT/long.log" all
fi
finish_tool

printf 'UTIL_DATA %d %d %d %d\n' "$tools" "$passed" "$failed" "$refused"
[ "$failed" -eq 0 ]
