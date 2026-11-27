#!/bin/sh
# Focused regression for issue #204: backslash-spliced // comments must stay
# comments, and a stray apostrophe/quote in #if 0 must not swallow the rest of
# the translation unit. Runs a dependency-free CPlusLexer unit test and an
# end-to-end lowered-program fixture.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue204.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
LIB=${CPLUS_CORE_LIB:-$ROOT/build-local/libcplus_core.a}
CXX=${CXX:-/usr/bin/c++}
WORK=${TMPDIR:-/tmp}/cplus-issue204-$$
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }

if [ ! -f "$LIB" ]; then
  LIB="$ROOT/build/libcplus_core.a"
fi
[ -f "$LIB" ] || fail "libcplus_core.a not found (build the compiler first)"

$CXX -std=c++20 -I"$ROOT/compiler" \
    "$ROOT/tests/compiler/agent1/lexer_issue204.cpp" "$LIB" \
    -o "$WORK/lexer_test" >"$WORK/cxx.log" 2>&1 ||
    { cat "$WORK/cxx.log" >&2; fail "lexer unit test did not compile"; }
"$WORK/lexer_test" || fail "lexer unit checks failed"

cat > "$WORK/probe.c" <<'EOF'
#if 0
  This comment shouldn't break RAII!
  and a doubled apostrophe'' is disabled too
#endif
// A spliced comment must not become C+ code: \
class Fake { int ignored; };
#include <stdio.h>
class Real { int value; int get(void){ return this->value; } };
int main(void){
  Real r = {7};
  printf("%d\n", r.get());
  return 0;
}
EOF
ENV="env -u SDKROOT -u CPLUS_CLANG PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin"
$ENV "$CPLUS" build --strict --sanitize=none "$WORK/probe.c" \
    -o "$WORK/probe.bin" >"$WORK/build.log" 2>&1 ||
    { cat "$WORK/build.log" >&2; fail "end-to-end build failed"; }
got=$("$WORK/probe.bin" 2>/dev/null)
[ "$got" = "7" ] || fail "lowered program printed '$got', expected 7"

echo "issue #204 lexer/comment regression: PASS"
