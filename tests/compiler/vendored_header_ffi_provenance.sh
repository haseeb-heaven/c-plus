#!/bin/sh
# Regression: diagnostics from a vendored third_party C header are external
# FFI-boundary false positives. C+ does not own yyjson.h's body, so including
# it must compile cleanly in DEFAULT and STRICT. The very same constructs
# written in user code must still be diagnosed.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/vendored_header_ffi_provenance.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
WORK=${TMPDIR:-/tmp}/cplus-vendored-header-ffi-$$
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { printf '  ok   %s\n' "$1"; }

RUN="env -u SDKROOT CPLUS_CLANG=/usr/bin/clang PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin"
HEADER="$ROOT/third_party/yyjson/yyjson.h"
[ -f "$HEADER" ] || fail "vendored header not found: $HEADER"
grep -qF 'max - pad - mul' "$HEADER" ||
  fail "fixture is not effective: $HEADER has no known division-by-zero construct"

# (a) Header-only probe: including the vendored header must be clean.
cat > "$WORK/include_only.c" <<'EOF'
#include <yyjson.h>
int main(void) { return 0; }
EOF
for mode in default strict; do
  if $RUN "$CPLUS" build --$mode --sanitize=none \
      -I"$ROOT/third_party/yyjson" "$WORK/include_only.c" \
      -o "$WORK/include_only.$mode" >"$WORK/include.$mode.log" 2>&1 &&
     ! grep -qE 'error:|warning:' "$WORK/include.$mode.log"; then
    ok "build --$mode: vendored yyjson.h compiles without C+ diagnostics"
  else
    cat "$WORK/include.$mode.log" >&2
    fail "build --$mode reported vendored-header diagnostics"
  fi
done

# (b) Positive control: the same three constructs in user source stay visible.
cat > "$WORK/user_constructs.c" <<'EOF'
int user_div(int n) { return 8 / n; }
int user_subscript(unsigned char *p, int i) { return p[i]; }
static void user_deref(int *p) { *p = 1; }
void user_nullarg(void) { user_deref(0); }
EOF
MSGS='division by zero cannot be excluded
pointer parameter '\''p'\'' has unverifiable extent
null pointer argument 0 passed to '\''user_deref'\'''
for mode in default strict; do
  if $RUN "$CPLUS" build --$mode --sanitize=none "$WORK/user_constructs.c" \
      -o "$WORK/user.$mode" >"$WORK/user.$mode.log" 2>&1; then
    cat "$WORK/user.$mode.log" >&2
    fail "build --$mode accepted user-code safety violations"
  fi
  printf '%s\n' "$MSGS" | while IFS= read -r msg; do
    [ -n "$msg" ] || continue
    grep -qF "$msg" "$WORK/user.$mode.log" ||
      fail "build --$mode stopped diagnosing user code: $msg"
  done || fail "build --$mode: user-code diagnostics missing"
  ok "build --$mode: identical constructs in user code still diagnosed"
done

echo "vendored-header FFI provenance regression: PASS"
