#!/bin/sh
# Deterministic regressions for issue #221 (SIGPIPE in web.hp/net.hp) and
# issue #222 (cross-TU STDLIB_META_TOMB in error.hp).
# Usage: tests/stdlib/issue221_222/run.sh
set -u
DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$DIR/results"
mkdir -p "$OUT"
fail=0

compile_run() {
    name=$1
    shift
    if ! "$CPLUS" build "$@" -o "$OUT/$name" >"$OUT/$name.build.log" 2>&1; then
        echo "FAIL $name: build error (see results/$name.build.log)"
        fail=$((fail + 1))
        return
    fi
    "$OUT/$name" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
    rc=$?
    if [ "$rc" -ne 0 ]; then
        echo "FAIL $name: exit $rc (see results/$name.stdout)"
        fail=$((fail + 1))
        return
    fi
    if [ -f "$DIR/$name.expected" ] &&
       ! diff -u "$DIR/$name.expected" "$OUT/$name.stdout" >"$OUT/$name.diff" 2>&1; then
        echo "FAIL $name: stdout mismatch (see results/$name.diff)"
        fail=$((fail + 1))
        return
    fi
    echo "PASS $name"
}

compile_run net_sigpipe "$DIR/net_sigpipe.cp"
# web.hp is a C-style header; a pre-existing C+ ownership false positive in its
# malloc convenience wrappers blocks the default policy (the repo's own web
# app also builds this header in a relaxed mode), so this C-style test uses
# --legacy-c, which still exercises the real web.hp functions.
if ! "$CPLUS" build "$DIR/web_sigpipe.cp" --legacy-c --no-werror \
     --sanitize=none -o "$OUT/web_sigpipe" >"$OUT/web_sigpipe.build.log" 2>&1; then
    echo "FAIL web_sigpipe: build error (see results/web_sigpipe.build.log)"
    fail=$((fail + 1))
else
    "$OUT/web_sigpipe" >"$OUT/web_sigpipe.stdout" 2>"$OUT/web_sigpipe.stderr"
    rc=$?
    if [ "$rc" -ne 0 ] || ! diff -u "$DIR/web_sigpipe.expected" \
         "$OUT/web_sigpipe.stdout" >"$OUT/web_sigpipe.diff" 2>&1; then
        echo "FAIL web_sigpipe: exit $rc (see results/web_sigpipe.stdout)"
        fail=$((fail + 1))
    else
        echo "PASS web_sigpipe"
    fi
fi
compile_run meta_tomb "$DIR/meta_tomb_main.cp" "$DIR/meta_tomb_a.cp" \
    "$DIR/meta_tomb_b.cp"

echo "TEST_ISSUE221_222 3 $((3 - fail)) $fail"
[ "$fail" -eq 0 ]
