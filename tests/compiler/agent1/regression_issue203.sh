#!/bin/sh
# Focused regression for issue #203: guarded cyclic .hp includes must not
# overflow the driver stack, and #include text inside comments, strings or
# disabled #if 0 branches must not be rewritten as a real header include.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue203.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
WORK=${TMPDIR:-/tmp}/cplus-issue203-$$
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { printf '  ok   %s\n' "$1"; }
ENV="env -u SDKROOT -u CPLUS_CLANG PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin"

cat > "$WORK/a.hp" <<'EOF'
#ifndef ISSUE203_REG_A
#define ISSUE203_REG_A
#include "b.hp"
class RegA { int x; int get(void){ return this->x; } };
#endif
EOF
cat > "$WORK/b.hp" <<'EOF'
#ifndef ISSUE203_REG_B
#define ISSUE203_REG_B
#include "a.hp"
class RegB { int y; int get(void){ return this->y; } };
#endif
EOF
cat > "$WORK/cycle.c" <<'EOF'
#include "a.hp"
#include <stdio.h>
int main(void){ RegA a={3}; RegB b={4}; printf("%d\n", a.get()+b.get()); return 0; }
EOF

# GNU timeout when present; otherwise rely on the shell's own kill.
if command -v timeout >/dev/null 2>&1; then TIMEOUT="timeout 30"; else TIMEOUT=""; fi
if $ENV $TIMEOUT "$CPLUS" build --strict --sanitize=none "$WORK/cycle.c" \
    -o "$WORK/cycle.bin" >"$WORK/log" 2>&1; then
  got=$("$WORK/cycle.bin" 2>/dev/null)
  [ "$got" = "7" ] || fail "cyclic headers produced '$got', expected 7"
  ok "guarded a.hp <-> b.hp cycle compiles and runs"
else
  cat "$WORK/log" >&2
  fail "guarded cyclic .hp build failed"
fi

cat > "$WORK/live.hp" <<'EOF'
#ifndef ISSUE203_REG_LIVE
#define ISSUE203_REG_LIVE
class RegLive { int v; int get(void){ return this->v; } };
#endif
EOF
cat > "$WORK/noise.c" <<'EOF'
/* #include "reg_missing_comment.hp" */
// #include "reg_missing_line.hp"
const char *msg = "#include \"reg_missing_string.hp\"";
#if 0
#include "reg_missing_if0.hp"
#endif
#if 1
#include "live.hp"
#endif
#include <stdio.h>
int main(void){
  RegLive v = {5};
  printf("%d%s\n", v.get(), msg);
  return 0;
}
EOF
if $ENV "$CPLUS" build --strict --sanitize=none "$WORK/noise.c" \
    -o "$WORK/noise.bin" >"$WORK/log" 2>&1; then
  got=$("$WORK/noise.bin" 2>/dev/null)
  [ "$got" = '5#include "reg_missing_string.hp"' ] ||
      fail "comment/string/#if 0 rewrite: got '$got'"
  ! grep -q 'cannot find C+ header' "$WORK/log" ||
      fail "a non-include #include text was treated as a header"
  ok "comments, strings and #if 0 are not rewritten; live #if 1 include works"
else
  cat "$WORK/log" >&2
  fail "noise build failed"
fi

# An unguarded cycle must fail, but never crash with SIGSEGV (139 on macOS).
cat > "$WORK/u1.hp" <<'EOF'
#include "u2.hp"
class RegU1 { int x; int get(void){ return this->x; } };
EOF
cat > "$WORK/u2.hp" <<'EOF'
#include "u1.hp"
class RegU2 { int y; int get(void){ return this->y; } };
EOF
printf '#include "u1.hp"\nint main(void){return 0;}\n' > "$WORK/unguarded.c"
set +e
$ENV $TIMEOUT "$CPLUS" check --strict "$WORK/unguarded.c" >"$WORK/log" 2>&1
rc=$?
set -e
[ "$rc" -ne 0 ] || fail "unguarded .hp cycle was accepted"
[ "$rc" -ne 139 ] || fail "unguarded .hp cycle crashed with SIGSEGV"
grep -q 'error:' "$WORK/log" || fail "unguarded .hp cycle produced no diagnostic"
ok "unguarded .hp cycle is rejected without a crash (rc=$rc)"

echo "issue #203 .hp preprocessing regression: PASS"
