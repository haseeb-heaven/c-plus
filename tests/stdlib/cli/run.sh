#!/bin/sh
# tests/stdlib/cli/run.sh -- deterministic CLI framework regression.
#
#   sh tests/stdlib/cli/run.sh
#
# Builds test_cli.cp with the exact required invocation
#   --default --no-werror --sanitize=none -I. -- build/libc/libcplus_hardened.a
# then runs it and diffs stdout/stderr byte-for-byte against the golden
# output embedded below.  Build/run are bounded by `timeout`.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-./build/c+}
HERE="$ROOT/tests/stdlib/cli"
TMP=${TMPDIR:-/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode}
NAME=cli_test
BIN="$TMP/$NAME"
BUILDLOG="$TMP/$NAME.build.log"
STDOUT="$TMP/$NAME.stdout"
STDERR="$TMP/$NAME.stderr"
DIFF="$TMP/$NAME.diff"

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
    exit 2
fi
if [ ! -x "$CPLUS" ]; then
    echo "error: C+ compiler missing: $CPLUS (build it first)" >&2
    exit 2
fi

fail=0

echo "+ $CPLUS build $HERE/test_cli.cp --default --no-werror --sanitize=none -I. -o $BIN -- build/libc/libcplus_hardened.a"
timeout -k 5 60 "$CPLUS" build "$HERE/test_cli.cp" \
    --default --no-werror --sanitize=none -I. -o "$BIN" \
    -- build/libc/libcplus_hardened.a >"$BUILDLOG" 2>&1
brc=$?
if [ "$brc" -eq 124 ] || [ "$brc" -eq 137 ]; then
    echo "BUILD-TIMEOUT cli rc=$brc"
    exit 1
fi
if [ "$brc" -ne 0 ]; then
    echo "BUILD-FAIL cli rc=$brc (see $BUILDLOG)"
    tail -40 "$BUILDLOG" >&2
    exit 1
fi
echo "BUILD cli rc=0"

echo "+ $BIN"
timeout -k 5 30 "$BIN" >"$STDOUT" 2>"$STDERR"
rrc=$?
if [ "$rrc" -eq 124 ] || [ "$rrc" -eq 137 ]; then
    echo "TIMEOUT cli rc=$rrc"
    exit 1
fi
if [ "$rrc" -ne 0 ]; then
    echo "RUN-FAIL cli rc=$rrc"
    fail=1
fi

# Exact golden stdout.  Note the intentional blank line after "--verbose":
# cli_help() ends with a newline and print() adds its own line terminator.
cat >"$TMP/$NAME.expected" <<'GOLDEN'
CLI_TEST begin
CHECK init PASS
CHECK add_cmd PASS
CHECK add_opt PASS
CHECK dup_cmd PASS
CHECK dup_opt PASS
CHECK long_name PASS
CHECK long_help PASS
CHECK null_args PASS
CHECK bad_option_name PASS
CHECK uninit PASS
CHECK registry_full PASS
CHECK parse_ok PASS
PARSE command=add id=1 found=1 options=2 args=2
OPT 0 --verbose has_value=0 value=
OPT 1 --count has_value=1 value=3
ARG 0 10
ARG 1 20
CHECK dispatch PASS
CHECK dispatch_refusal PASS
CHECK help_exact PASS
HELP_BEGIN
Commands:
  add - add two numbers (id=1)
  greet - greet a name (id=2)
Options:
  --count=value
  --verbose

HELP_END
CHECK help_bounds PASS
CHECK unknown_option PASS
CHECK unknown_command PASS
CHECK no_command PASS
CHECK too_many_args PASS
CHECK bare_dash PASS
CHECK flag_value PASS
CHECK value_missing PASS
CHECK oversized_token PASS
CHECK null_token PASS
CHECK negative_argc PASS
CHECK error_strings PASS
CLI_TEST 27 27 0
GOLDEN

: >"$DIFF"
diff -u "$TMP/$NAME.expected" "$STDOUT" >>"$DIFF" 2>&1 || {
    echo "OUT-FAIL cli (see $DIFF)"
    head -n 20 "$DIFF" | sed 's/^/           /'
    fail=1
}
if [ -s "$STDERR" ]; then
    echo "STDERR-FAIL cli (unexpected stderr)"
    head -n 20 "$STDERR" | sed 's/^/           /'
    fail=1
fi

if [ "$fail" -ne 0 ]; then
    echo "CLI SUITE FAIL"
    exit 1
fi
echo "PASS cli exact-output 27/27 checks"
echo "CLI SUITE PASS"
exit 0
