#!/bin/sh
# Regression: integer-to-pointer casts inside an Apple SDK system header are
# not user code. The C+ provenance check must stay silent for them in both
# DEFAULT and STRICT, while the very same header body included as a user
# header (via -I) must still be diagnosed.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/system_header_provenance.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
WORK=${TMPDIR:-/tmp}/cplus-sysheader-provenance-$$
mkdir -p "$WORK/user_include/libkern" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { printf '  ok   %s\n' "$1"; }

SDK=$(env -u SDKROOT PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  xcrun --sdk macosx --show-sdk-path 2>/dev/null)
HEADER=$SDK/usr/include/libkern/OSByteOrder.h
[ -f "$HEADER" ] || fail "Apple SDK header not found: $HEADER"
grep -qF '((uintptr_t)base + byteOffset)' "$HEADER" ||
  fail "fixture is not effective: $HEADER has no integer-to-pointer cast"

RUN="env -u SDKROOT CPLUS_CLANG=/usr/bin/clang PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin"
MSG='pointer created from integer value has unverifiable provenance'
cat > "$WORK/use_header.c" <<'EOF'
#include <libkern/OSByteOrder.h>
int main(void) { return (int)_OSReadInt16("ab", 0); }
EOF
cp "$HEADER" "$WORK/user_include/libkern/OSByteOrder.h"

# The SDK copy must be recognised as a system header: no CPLUS-FFI diagnostic
# and a successful check/build in both modes.
for mode in default strict; do
  if $RUN "$CPLUS" check --$mode "$WORK/use_header.c" \
      >"$WORK/check.$mode.log" 2>&1 &&
     ! grep -qF "$MSG" "$WORK/check.$mode.log"; then
    ok "check --$mode: no provenance diagnostic from SDK header"
  else
    cat "$WORK/check.$mode.log" >&2
    fail "check --$mode reported a system-header cast"
  fi
  if $RUN "$CPLUS" build --$mode "$WORK/use_header.c" \
      -o "$WORK/use_header.$mode" >"$WORK/build.$mode.log" 2>&1 &&
     ! grep -qF "$MSG" "$WORK/build.$mode.log"; then
    ok "build --$mode: system header compiles without warnings-as-errors"
  else
    cat "$WORK/build.$mode.log" >&2
    fail "build --$mode reported a system-header cast"
  fi
done

# Positive control: identical header body, but reached through -I so Clang
# treats it as user code. The check must keep flagging those casts.
for mode in default strict; do
  if ! $RUN "$CPLUS" check --$mode -I"$WORK/user_include" \
      "$WORK/use_header.c" >"$WORK/user.$mode.log" 2>&1 &&
     grep -qF "$MSG" "$WORK/user.$mode.log" &&
     grep -qF "$WORK/user_include/libkern/OSByteOrder.h" \
       "$WORK/user.$mode.log"; then
    ok "check --$mode: same cast in a user header is still diagnosed"
  else
    cat "$WORK/user.$mode.log" >&2
    fail "check --$mode stopped diagnosing the cast in user code"
  fi
done

echo "system-header provenance regression: PASS"
