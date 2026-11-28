#!/bin/sh
# Focused regression for issue #202: STRICT Safe mode must ignore untrusted
# CPLUS_INCLUDE, CPLUS_CLANG and CPLUS_ROOT environment path substitutions for
# runtime headers, the C backend and .hp header resolution. DEFAULT mode keeps
# the documented overrides, which is also how the fixtures below prove the
# spoofed paths really would be used without the STRICT gate.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue202.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
WORK=${TMPDIR:-/tmp}/cplus-issue202-$$
mkdir -p "$WORK/evil/include/cplus" "$WORK/evil/root" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }
ok() { printf '  ok   %s\n' "$1"; }

# The restricted PATH used by the harness; the compiler must find its backend
# there without help from CPLUS_CLANG.
ENV="env -u SDKROOT -u CPLUS_CLANG PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin"

printf 'int main(void){return 0;}\n' > "$WORK/probe.c"
printf '#include <cplus/cplus.h>\nint main(void){return 0;}\n' \
    > "$WORK/with_header.c"
printf '#error EVIL_INCLUDE_USED\n' \
    > "$WORK/evil/include/cplus/cplus.h"

printf 'class RootProbe { int x; int get(void){return this->x;} };\n' \
    > "$WORK/evil/root/rootprobe.hp"
printf 'int marker_only_to_please_cmake;\n' \
    > "$WORK/evil/root/CMakeLists.txt"
printf '#include "rootprobe.hp"\n' > "$WORK/with_root.c"
printf '#include "rootprobe.hp"\nint main(void){RootProbe p={1};return p.get()-1;}\n' \
    > "$WORK/with_root_main.c"

printf '#!/bin/sh\necho EVIL_CLANG_USED >&2\nexit 1\n' > "$WORK/evil/clang"
chmod +x "$WORK/evil/clang"

# --- CPLUS_INCLUDE ---------------------------------------------------------
if $ENV CPLUS_INCLUDE="$WORK/evil/include" "$CPLUS" check --strict \
    "$WORK/with_header.c" >"$WORK/log" 2>&1 &&
   ! grep -q EVIL_INCLUDE_USED "$WORK/log"; then
  ok "STRICT ignores CPLUS_INCLUDE"
else
  cat "$WORK/log" >&2
  fail "STRICT honoured CPLUS_INCLUDE"
fi
if ! $ENV CPLUS_INCLUDE="$WORK/evil/include" "$CPLUS" check --default \
    "$WORK/with_header.c" >"$WORK/log" 2>&1 &&
   grep -q EVIL_INCLUDE_USED "$WORK/log"; then
  ok "DEFAULT still honours CPLUS_INCLUDE (fixture is effective)"
else
  cat "$WORK/log" >&2
  fail "DEFAULT did not use the spoofed CPLUS_INCLUDE"
fi

# --- CPLUS_CLANG -----------------------------------------------------------
if $ENV CPLUS_CLANG="$WORK/evil/clang" "$CPLUS" build --strict \
    --sanitize=none "$WORK/probe.c" -o "$WORK/probe.bin" \
    >"$WORK/log" 2>&1 &&
   ! grep -q EVIL_CLANG_USED "$WORK/log"; then
  ok "STRICT ignores CPLUS_CLANG"
else
  cat "$WORK/log" >&2
  fail "STRICT honoured CPLUS_CLANG"
fi
if ! $ENV CPLUS_CLANG="$WORK/evil/clang" "$CPLUS" build --default \
    --sanitize=none "$WORK/probe.c" -o "$WORK/probe.bin" \
    >"$WORK/log" 2>&1 &&
   grep -q EVIL_CLANG_USED "$WORK/log"; then
  ok "DEFAULT still honours CPLUS_CLANG (fixture is effective)"
else
  cat "$WORK/log" >&2
  fail "DEFAULT did not use the spoofed CPLUS_CLANG"
fi

# --- CPLUS_ROOT ------------------------------------------------------------
# In STRICT, a .hp reachable only through the fake source root must NOT be
# resolved; the driver should report the missing header instead.
if ! $ENV CPLUS_ROOT="$WORK/evil/root" "$CPLUS" check --strict \
    "$WORK/with_root.c" >"$WORK/log" 2>&1 &&
   grep -qF "cannot find C+ header 'rootprobe.hp'" "$WORK/log"; then
  ok "STRICT ignores CPLUS_ROOT during .hp resolution"
else
  cat "$WORK/log" >&2
  fail "STRICT resolved a .hp through CPLUS_ROOT"
fi
if $ENV CPLUS_ROOT="$WORK/evil/root" "$CPLUS" build --default \
    --sanitize=none "$WORK/with_root_main.c" -o "$WORK/root.bin" \
    >"$WORK/log" 2>&1 && "$WORK/root.bin"; then
  ok "DEFAULT still honours CPLUS_ROOT (fixture is effective)"
else
  cat "$WORK/log" >&2
  fail "DEFAULT did not use the spoofed CPLUS_ROOT"
fi

echo "issue #202 STRICT path-hardening regression: PASS"
