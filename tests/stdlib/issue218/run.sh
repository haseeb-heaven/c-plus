#!/bin/sh
# Deterministic regression for issue #218 (arena half).
#
# tests/stdlib/run_stdlib_tests.sh only globs tests/stdlib/*.cp, so this
# sub-directory carries its own runner.  Use:
#   CPLUS_BIN=/path/to/c+ sh tests/stdlib/issue218/run.sh
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
HERE="$ROOT/tests/stdlib/issue218"
OUT=$(mktemp -d "${TMPDIR:-/tmp}/issue218.XXXXXX") || exit 1
trap 'rm -rf "$OUT"' EXIT HUP INT TERM

if [ ! -x "$CPLUS" ]; then
  printf 'issue218: compiler not found at %s\n' "$CPLUS" >&2
  exit 1
fi

cd "$OUT" || exit 1
if ! "$CPLUS" build "$HERE/issue218_arena.cp" --no-werror -o "$OUT/issue218" \
     >"$OUT/build.log" 2>&1; then
  printf 'issue218: compilation failed\n' >&2
  cat "$OUT/build.log" >&2
  exit 1
fi

"$OUT/issue218" >"$OUT/stdout" 2>"$OUT/stderr"
rc=$?
if [ "$rc" -ne 0 ]; then
  printf 'issue218: exited %s\n' "$rc" >&2
  cat "$OUT/stdout" "$OUT/stderr" >&2
  exit 1
fi
if ! diff -u "$HERE/expected" "$OUT/stdout"; then
  printf 'issue218: output mismatch\n' >&2
  exit 1
fi

printf 'issue218: PASS (%s)\n' "$(cat "$OUT/stdout")"
exit 0
