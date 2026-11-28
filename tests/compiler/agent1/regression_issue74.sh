#!/bin/sh
# Focused regression for issue #74: the sanitizer capability probe must never
# pass user-controlled flags/paths through /bin/sh, and lowered artifacts must
# use atomically created unique names.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue74.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
WORK=${TMPDIR:-/tmp}/cplus-issue74-$$
mkdir -p "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }

printf 'int main(void){return 0;}\n' > "$WORK/probe.c"

# A single quote inside a pass-through flag used to escape the shell's
# '...' quoting in Driver.cpp's system() call and execute `touch`.
marker="$WORK/PWNED"
payload="-DX=a';touch $marker;echo '"
if ! "$CPLUS" build --sanitize=undefined --no-werror "$WORK/probe.c" \
        -o "$WORK/probe.bin" "$payload" >"$WORK/build.log" 2>&1; then
  cat "$WORK/build.log" >&2
  fail "sanitizer probe build failed with a quoted pass-through flag"
fi
[ ! -e "$marker" ] || fail "command injection created $marker"

# A directory name containing a quote must also be handled as plain data.
qdir="$WORK/bob's project"
mkdir -p "$qdir" || exit 1
cp "$WORK/probe.c" "$qdir/probe.c"
if ! "$CPLUS" build --sanitize=undefined --no-werror "$qdir/probe.c" \
        -o "$WORK/quoted.bin" >"$WORK/quoted.log" 2>&1; then
  cat "$WORK/quoted.log" >&2
  fail "build failed for a source path containing a single quote"
fi
"$WORK/quoted.bin" || fail "quoted-path binary did not run"

# Lowered temporary sources must use random suffixes, not pid/index names.
CPLUS_KEEP_TEMP=1 "$CPLUS" build --sanitize=none --no-werror "$WORK/probe.c" \
    -o "$WORK/keep.bin" >"$WORK/keep.log" 2>&1 ||
    fail "keep-temp build failed"
for f in "$WORK"/.cplus-tmp-*.c; do
  [ -e "$f" ] || continue
  base=${f##*/}
  stem=${base#.cplus-tmp-}
  stem=${stem%.c}
  case "$stem" in
    *-*) fail "predictable temp name survived: $base" ;;
  esac
  rm -f "$f"
done

echo "issue #74 regression: PASS"
