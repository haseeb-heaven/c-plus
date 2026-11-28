#!/bin/sh
# Linker inputs must never be treated as C+ source inputs.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
T=$(mktemp -d)
# macOS GitHub runners do not ship GNU timeout. Keep the bounded form
# when available; otherwise run the command directly.
if command -v timeout >/dev/null 2>&1; then
  run_timeout() { timeout "$@"; }
else
  run_timeout() { shift; "$@"; }
fi

trap 'rm -rf "$T"' EXIT

cat > "$T/helper.c" <<'EOF'
int helper_value(void) { return 42; }
EOF
cat > "$T/main.c" <<'EOF'
#include <stdio.h>
int helper_value(void);
int main(void) { printf("LINK_INPUT_OK %d\n", helper_value()); return 0; }
EOF

CC=${CPLUS_CLANG:-/usr/bin/clang}
"$CC" -c "$T/helper.c" -o "$T/helper.o" || exit 1
ar rcs "$T/libhelper.a" "$T/helper.o" || exit 1

if ! run_timeout 30 "$CPLUS" build "$T/main.c" --no-sanitize --no-werror \
      -o "$T/out1" "$T/libhelper.a" >"$T/log1" 2>&1; then
    echo "link_input_filter: FAIL (positional archive)"; tail -8 "$T/log1"; exit 1
fi
[ "$("$T/out1")" = "LINK_INPUT_OK 42" ] || { echo "link_input_filter: FAIL output"; exit 1; }

if ! run_timeout 30 "$CPLUS" build "$T/main.c" --no-sanitize --no-werror \
      -L"$T" -lhelper -o "$T/out2" >"$T/log2" 2>&1; then
    echo "link_input_filter: FAIL (-L/-l form)"; tail -8 "$T/log2"; exit 1
fi
[ "$("$T/out2")" = "LINK_INPUT_OK 42" ] || { echo "link_input_filter: FAIL output2"; exit 1; }

echo "link_input_filter: PASS"
exit 0
