#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
PREFIX=$(mktemp -d "${TMPDIR:-/tmp}/cplus-sdk.XXXXXX")
trap 'rm -rf "$PREFIX"' EXIT
cmake --install "$ROOT/build" --prefix "$PREFIX" >/dev/null
test -x "$PREFIX/bin/c+"
test -f "$PREFIX/include/cplus/cplus.h"
test -f "$PREFIX/share/cplus/libc_contracts.json"
printf '#include <stdio.h>\nint main(void){printf("sdk\\n");return 0;}\n' > "$PREFIX/sample.c"
"$PREFIX/bin/c+" build "$PREFIX/sample.c" -o "$PREFIX/sample" >/dev/null 2>&1
[ "$("$PREFIX/sample")" = "sdk" ]
echo "sdk clean-install smoke: ok"
