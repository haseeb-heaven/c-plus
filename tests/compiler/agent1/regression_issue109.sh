#!/bin/sh
# Focused regression for issue #109: STRICT must reject leading-space -D
# overrides of reserved macros and non-IEEE -O levels, while keeping ordinary
# macros and safe optimization levels usable.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue109.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
WORK=${TMPDIR:-/tmp}/cplus-issue109-$$
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }
printf 'int main(void){return 0;}\n' > "$WORK/probe.c"

reject() {
  desc=$1; shift
  if "$CPLUS" check --strict "$WORK/probe.c" "$@" >"$WORK/log" 2>&1; then
    cat "$WORK/log" >&2
    fail "STRICT accepted $desc"
  fi
  printf '  ok   rejected %s\n' "$desc"
}

accept() {
  desc=$1; shift
  if ! "$CPLUS" check --strict "$WORK/probe.c" "$@" >"$WORK/log" 2>&1; then
    cat "$WORK/log" >&2
    fail "STRICT rejected safe $desc"
  fi
  printf '  ok   accepted %s\n' "$desc"
}

reject "-D ' CPLUS_STRICT=0'" -D ' CPLUS_STRICT=0'
reject "-D ' CPLUS=0'" -D ' CPLUS=0'
reject "-D ' __cplus_bounds_check(i,l,n)=(i)'" \
    -D ' __cplus_bounds_check(i,l,n)=(i)'
reject "-D' CPLUS_STRICT=0' (joined)" '-D CPLUS_STRICT=0'
reject "-U ' CPLUS'" -U ' CPLUS'
reject "-D '=1' (malformed)" -D '=1'
reject '-Ofast' -Ofast
reject '-- -Ofast' -- -Ofast
reject '-O4' -O4

accept '-O2' -O2
accept '-Os' -Os
accept '-D FOO=1' -D FOO=1
accept '-D FOO=a b (spaced value)' -D 'FOO=a b'

echo "issue #109 strict-flag regression: PASS"
