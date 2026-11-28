#!/bin/sh
# Issue #241: assigning a fresh value to a released pointer must revive only
# that name.  Aliases still hold the freed address and must stay dangling,
# otherwise `p = NULL` launders `q = p` into a UAF / double free.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
D="$ROOT/tests/compiler/agent1"
OUT=${TMPDIR:-/tmp}/cplus-issue241-$$.log
trap 'rm -f "$OUT"' EXIT INT TERM
fail() { echo "FAIL: $*" >&2; exit 1; }

# Negative: both the dereference and the second free through the surviving
# alias must be rejected, with diagnostics naming the alias.
if "$CPLUS" check --strict --sanitize=none --no-werror \
      "$D/issue241_alias_uaf_null.c" >"$OUT" 2>&1; then
  cat "$OUT" >&2
  fail "dangling alias accepted after p = NULL"
fi
grep -qF "use-after-free" "$OUT" ||
  fail "missing use-after-free diagnostic: $(cat "$OUT")"
grep -qF "double free of 'q'" "$OUT" ||
  fail "missing double-free diagnostic: $(cat "$OUT")"

# Positive controls: the fix must still revive the name that was assigned a
# fresh value, whether it is the original owner or the alias itself.
for src in issue241_owner_fresh.c issue241_alias_fresh.c; do
  if ! "$CPLUS" check --strict --sanitize=none --no-werror \
        "$D/$src" >"$OUT" 2>&1; then
    cat "$OUT" >&2
    fail "freshly assigned name rejected in $src"
  fi
done

echo "issue #241 clearReleased alias regression: PASS"
exit 0
