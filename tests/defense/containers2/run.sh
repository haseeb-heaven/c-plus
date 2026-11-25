#!/bin/sh
# CONTAINERS2 hardening suite.
#   - minimal reproducer for the large-count bounded-query defect
#   - 1200 deterministic forked scenarios over vector/string/collections/memory
# Builds with the deterministic C+ driver and runs each binary under timeout 60.
set -u
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
cd "$ROOT" || exit 1
CPLUS=${CPLUS_BIN:-./build/c+}
OUT="$HERE/out"
mkdir -p "$OUT"

rc=0

echo "[build] repro_large_count"
if ! "$CPLUS" build "$HERE/repro_large_count.cp" --strict --sanitize=none \
        -I. -o "$OUT/repro_large_count" >"$OUT/repro.build.log" 2>&1; then
    echo "FAIL repro_large_count BUILD"
    cat "$OUT/repro.build.log"
    rc=1
else
    set +e
    timeout 60 "$OUT/repro_large_count"
    rrc=$?
    set -e
    if [ "$rrc" -eq 0 ]; then
        echo "PASS repro_large_count"
    else
        echo "FAIL repro_large_count (exit $rrc)"
        rc=1
    fi
fi

echo "[build] test_containers2 (1200 scenarios)"
if ! "$CPLUS" build "$HERE/test_containers2.cp" --strict --sanitize=none \
        -I. -o "$OUT/test_containers2" >"$OUT/harness.build.log" 2>&1; then
    echo "FAIL test_containers2 BUILD"
    cat "$OUT/harness.build.log"
    rc=1
else
    set +e
    timeout 60 "$OUT/test_containers2" >"$OUT/latest.log" 2>&1
    hrc=$?
    set -e
    grep '^FAMILY\|^CONTAINERS2' "$OUT/latest.log" || true
    if [ "$hrc" -eq 0 ]; then
        echo "PASS test_containers2"
    else
        echo "FAIL test_containers2 (exit $hrc)"
        grep '^S .* 0 ' "$OUT/latest.log" | head -20 || true
        rc=1
    fi
fi

exit "$rc"
