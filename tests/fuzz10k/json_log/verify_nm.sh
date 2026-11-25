#!/usr/bin/env bash
# nm evidence for the compiled cplus_json_* / cplus_safe_log artifacts that
# compiled_probe.cp links against. All paths are relative to the repo root.
set -u
ROOT="${1:-$(cd "$(dirname "$0")/../../.." && pwd)}"
cd "$ROOT" || exit 2

echo "ROOT=$ROOT"
echo "== nm -g build/safe/w_safe_json.o =="
nm -g build/safe/w_safe_json.o 2>&1 | grep -E 'cplus_json_|yyjson_read_opts' | head -20
j1=$?
echo "== nm -g build/safe/libcplus_safecrt.a (json + safe log) =="
nm -g build/safe/libcplus_safecrt.a 2>&1 | \
  grep -E ' T _cplus_(json_parse|json_free|json_get_int|json_get_str|safe_log|safe_log_set_level|safe_log_get_level)' | head -30
j2=$?
echo "== nm -g build/ext/libyyjson.a (parser backend) =="
nm -g build/ext/libyyjson.a 2>&1 | \
  grep -E ' T _yyjson_(read_opts|val_write|doc_get_root)' | head -20
j3=$?

if [ "$j1" -eq 0 ] && [ "$j2" -eq 0 ] && [ "$j3" -eq 0 ]; then
  echo "NM_ARTIFACTS_OK=1"
  exit 0
fi
echo "NM_ARTIFACTS_OK=0"
exit 1
