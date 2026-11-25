#!/bin/sh
# Issue #47 (STRICT-only) regression fixtures.  DEFAULT stays permissive for
# calls whose callee is not visible in the translation unit; STRICT fails
# closed while a try-region owner would be skipped by an exception.
# Usage: tests/raii_shadowing/run_strict_throw.sh
set -u

if ! command -v timeout >/dev/null 2>&1; then
    timeout() { _t_secs=$1; shift; "$@"; }
fi

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
cd "$ROOT" || exit 2
D=tests/raii_shadowing
OUT=$D/out
mkdir -p "$OUT"
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
CASES="repro_throw_crosstu repro_throw_indirect"
pass=0; fail=0

for c in $CASES; do
    # STRICT must reject the call with the exact diagnostic.
    if timeout 120 "$CPLUS" check --strict "$D/$c.cp" >"$OUT/$c.strict.log" 2>&1; then
        printf 'STRICT_THROW FAIL %s accepted by --strict\n' "$c"
        fail=$((fail+1))
        continue
    fi
    want=$(cat "$D/$c.err")
    if ! grep -qF "$want" "$OUT/$c.strict.log"; then
        printf 'STRICT_THROW FAIL %s wrong diagnostic\n' "$c"
        fail=$((fail+1))
        continue
    fi
    # DEFAULT keeps accepting these unverifiable calls (no soundness claim).
    if ! timeout 120 "$CPLUS" check "$D/$c.cp" >"$OUT/$c.default.log" 2>&1; then
        printf 'STRICT_THROW FAIL %s rejected by DEFAULT\n' "$c"
        fail=$((fail+1))
        continue
    fi
    printf 'STRICT_THROW PASS %s\n' "$c"
    pass=$((pass+1))
done

printf 'RAII_STRICT_THROW %d %d %d\n' "$((pass+fail))" "$pass" "$fail"
[ "$fail" -eq 0 ]
