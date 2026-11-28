#!/bin/sh
# Focused regression for issue #205: TempFile move-assignment must remove the
# file it overwrites, and writeFile must fail loudly on truncated/disk-full
# writes instead of returning true. Also smoke-tests that normal driver runs
# do not strand lowered .hp temp files.
#
# Usage: CPLUS_BIN=/path/to/c+ sh tests/compiler/agent1/regression_issue205.sh
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
CXX=${CXX:-/usr/bin/c++}
LLVM_CONFIG=${LLVM_CONFIG:-/opt/homebrew/opt/llvm/bin/llvm-config}
[ -x "$LLVM_CONFIG" ] || LLVM_CONFIG=llvm-config
WORK=${TMPDIR:-/tmp}/cplus-issue205-$$
mkdir -p "$WORK/src" "$WORK/tmp" || exit 1
trap 'rm -rf "$WORK"' EXIT INT TERM

fail() { echo "FAIL: $*" >&2; exit 1; }

$CXX -std=c++20 -I"$ROOT/compiler" -I"$($LLVM_CONFIG --includedir)" \
    "$ROOT/tests/compiler/agent1/unit_issue205.cpp" \
    $($LLVM_CONFIG --ldflags) $($LLVM_CONFIG --libs support) \
    $($LLVM_CONFIG --system-libs) -o "$WORK/unit205" \
    >"$WORK/cxx.log" 2>&1 ||
    { cat "$WORK/cxx.log" >&2; fail "unit test did not compile"; }
"$WORK/unit205" || fail "TempFile/writeFile unit checks failed"

cat > "$WORK/src/probe.hp" <<'EOF'
#ifndef ISSUE205_PROBE_HP
#define ISSUE205_PROBE_HP
class Probe205 { int v; int get(void){ return this->v; } };
#endif
EOF
cat > "$WORK/src/main.c" <<'EOF'
#include "probe.hp"
#include <stdio.h>
int main(void){ Probe205 p = {9}; printf("%d\n", p.get()); return 0; }
EOF
ENV="env -u SDKROOT -u CPLUS_CLANG PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin"
TMPDIR="$WORK/tmp" $ENV "$CPLUS" build --strict --sanitize=none \
    "$WORK/src/main.c" -o "$WORK/main.bin" >"$WORK/build.log" 2>&1 ||
    { cat "$WORK/build.log" >&2; fail "driver smoke build failed"; }
[ "$("$WORK/main.bin" 2>/dev/null)" = "9" ] ||
    fail "driver smoke program printed the wrong result"
for f in "$WORK/tmp"/cplus-hp-*.h; do
  [ -e "$f" ] && fail "lowered .hp temp file was stranded: $f"
done
for f in "$WORK"/src/.cplus-tmp-*.c; do
  [ -e "$f" ] && fail "lowered TU temp file was stranded: $f"
done

echo "issue #205 temp-file/write-error regression: PASS"
