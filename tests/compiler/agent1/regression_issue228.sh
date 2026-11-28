#!/bin/sh
# Focused regression for issue #228: do/while state lives on the loop frame,
# and a `{` inside a parenthesised condition is not a loop body.  Verifies the
# exact lowered cleanup at `break`, the exact runtime drop order, and the
# compile-fail diagnostic for the same nested do/while shape.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue228.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
BUILD=${CPLUS_BUILD:-}
if [ -z "$BUILD" ]; then
  for candidate in "$ROOT/build-local" "$ROOT/build-verify" \
                   "$ROOT/build-final" "$ROOT/build"; do
    if [ -x "$candidate/c+" ]; then BUILD=$candidate; break; fi
  done
fi
[ -n "$BUILD" ] || BUILD=$ROOT/build
CPLUS=${CPLUS_BIN:-$BUILD/c+}
WORK=${TMPDIR:-/tmp}/cplus-issue228-$$
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM
fail() { echo "FAIL: $*" >&2; exit 1; }

POS="$ROOT/tests/cases/issue228_nested_do_while.cp"
NEG="$ROOT/tests/compile_fail/issue228_nested_do_while.cp"
ENV="env -u SDKROOT -u CPLUS_CLANG PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin"

# Lowered C: the inner `while` must drop only its own owner; owners in the
# enclosing `if` / `do` bodies must not be dropped by that `break`.  A compound
# literal in a `while` condition must not steal the frame either.
$ENV "$CPLUS" build "$POS" --no-werror --sanitize=none \
    --emit-c "$WORK/lowered.c" -o "$WORK/probe.bin" >"$WORK/build.log" 2>&1 ||
    { cat "$WORK/build.log" >&2; fail "positive fixture did not build"; }
grep -Fq '{ Box_drop(&innerWhileR); break; }' "$WORK/lowered.c" ||
    fail "break in nested while did not drop only its own owner"
if grep -F '{ Box_drop(&innerWhileR); break; }' "$WORK/lowered.c" |
   grep -Eq 'midR|outerR'; then
  fail "break in nested while dropped an enclosing owner"
fi
grep -Fq '{ Box_drop(&innerParenR); Box_drop(&bodyR); break; }' \
    "$WORK/lowered.c" || fail "compound literal in header stole the loop frame"

got=$("$WORK/probe.bin")
want=$(cat "$ROOT/tests/cases/issue228_nested_do_while.expected")
[ "$got" = "$want" ] ||
    fail "runtime drop order: got $(printf '%s' "$got" | tr '\n' ' ')"

# The same shape with an invalid ownership operation must keep its diagnostic.
if $ENV "$CPLUS" check "$NEG" >"$WORK/neg.log" 2>&1; then
  cat "$WORK/neg.log" >&2
  fail "compile-fail fixture was accepted"
fi
needle=$(cat "$ROOT/tests/compile_fail/issue228_nested_do_while.err")
grep -qF "$needle" "$WORK/neg.log" ||
    fail "compile-fail diagnostic missing '$needle'"

echo "issue #228 nested do/while regression: PASS"
