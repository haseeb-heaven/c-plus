#!/usr/bin/env bash
# Verify presence of compiled cplus_json_* / cplus_log_* artifacts with nm.
# These are auxiliary artifacts present in build/; the ADV1000 harness itself
# targets the header-only stdlib/json.hp and stdlib/log.hp APIs.
set -u
ROOT="${1:-$(cd "$(dirname "$0")/../../.." && pwd)}"
cd "$ROOT" || exit 2

echo "ROOT=$ROOT"
echo "== nm -g build/safe/w_safe_json.o =="
nm -g build/safe/w_safe_json.o 2>&1 | grep -E 'cplus_json_|yyjson_read_opts' | head -20
j1=$?
echo "== nm -g build/safe/w_safe_log.o =="
nm -g build/safe/w_safe_log.o 2>&1 | grep -E 'cplus_safe_log' | head -20
j2=$?
echo "== nm -g build/safe/libcplus_safecrt.a (json/log/stat) =="
nm -g build/safe/libcplus_safecrt.a 2>&1 | \
  grep -E ' T _cplus_(json_parse|json_free|json_get_int|json_get_str|safe_log|safe_log_set_level|safe_log_get_level|safe_stat_record)' | head -30
j3=$?
echo "== nm -g build/ext/libyyjson.a (parser symbols) =="
nm -g build/ext/libyyjson.a 2>&1 | \
  grep -E ' T _yyjson_(read_opts|val_write|doc_get_root)' | head -20
j4=$?

if [ "$j1" -eq 0 ] && [ "$j2" -eq 0 ] && [ "$j3" -eq 0 ] && [ "$j4" -eq 0 ]; then
  echo "NM_ARTIFACTS_OK=1"
  exit 0
fi
echo "NM_ARTIFACTS_OK=0"
exit 1
