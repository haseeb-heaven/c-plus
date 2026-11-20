#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
TMP=$(mktemp -d "${TMPDIR:-/tmp}/cplus-cli.XXXXXX")
trap 'rm -rf "$TMP"' EXIT

cat > "$TMP/hello.c" <<'SRC'
#include <stdio.h>
int main(void) { printf("hi\n"); return 0; }
SRC

# gcc/g++ style: no subcommand, flags before or after the source file.
"$CPLUS" "$TMP/hello.c" -o "$TMP/a1"
"$CPLUS" -w -o "$TMP/a2" "$TMP/hello.c"
"$CPLUS" -O2 -g "$TMP/hello.c" -o "$TMP/a3"
for exe in "$TMP/a1" "$TMP/a2" "$TMP/a3"; do
  [ "$("$exe")" = "hi" ] || { echo "FAIL: $exe output"; exit 1; }
done

# gcc compile-only / preprocess / assembly modes.
"$CPLUS" -c "$TMP/hello.c" -o "$TMP/hello.o"
[ -f "$TMP/hello.o" ] || { echo "FAIL: -c"; exit 1; }
"$CPLUS" -E "$TMP/hello.c" -o "$TMP/hello.i"
grep -q 'hello' "$TMP/hello.i" || { echo "FAIL: -E"; exit 1; }
"$CPLUS" -S "$TMP/hello.c" -o "$TMP/hello.s"
[ -s "$TMP/hello.s" ] || { echo "FAIL: -S"; exit 1; }

echo "cli compatibility tests: ok"
