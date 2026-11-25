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

# gcc/g++ long-form std, two-token -x, extension-less sources and multi-TU.
cat > "$TMP/helper" <<'SRC'
int helper(void) { return 7; }
SRC
cat > "$TMP/main.cpp" <<'SRC'
int helper(void);
int main(void) { return helper() == 7 ? 0 : 1; }
SRC
"$CPLUS" --std=c11 -w "$TMP/helper" "$TMP/main.cpp" -o "$TMP/multi1"
"$CPLUS" -std=c11 -x c "$TMP/helper" "$TMP/main.cpp" -o "$TMP/multi2"
"$CPLUS" -x c++ -w "$TMP/helper" "$TMP/main.cpp" -o "$TMP/multi3"
for exe in "$TMP/multi1" "$TMP/multi2" "$TMP/multi3"; do
  "$exe" || { echo "FAIL: $exe multi-TU"; exit 1; }
done

# Default-output naming with gcc-style invocation.
(cd "$TMP" && "$CPLUS" hello.c && [ -x ./hello ]) || { echo "FAIL: default output"; exit 1; }

echo "cli compatibility tests: ok"
