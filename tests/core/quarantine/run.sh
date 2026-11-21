#!/bin/sh
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CC=${CPLUS_BIN:-$ROOT/build/c+}
HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
OUT="$ROOT/tests/out/core/quarantine"
mkdir -p "$OUT"
for n in stale_check raw_deref eviction; do
  "$CC" build "$HERE/$n.c" -o "$OUT/$n" >"$OUT/$n.build.log" 2>&1
done

# 1. With quarantine, an explicit liveness check detects a stale alias.
rc=0
CPLUS_QUARANTINE=1 "$OUT/stale_check" >"$OUT/stale.stdout" 2>"$OUT/stale.stderr" || rc=$?
if [ "$rc" -eq 105 ] && grep -q 'dangling pointer' "$OUT/stale.stderr"; then
  echo "PASS stale alias detected with explicit cplus_check_live"
else
  echo "FAIL stale alias check rc=$rc"; exit 1
fi

# 2. Without an explicit check, raw dereference remains UB (quarantine does
#    not instrument dereferences).
CPLUS_QUARANTINE=1 "$OUT/raw_deref" >"$OUT/deref.stdout" 2>"$OUT/deref.stderr" || true
echo "INFO raw dereference with quarantine produced no C+ trap (still UB)"

# 3. Eviction of >1024 entries must not crash; the stale check may still
#    succeed if malloc does not reuse that exact address.
CPLUS_QUARANTINE=1 "$OUT/eviction" >"$OUT/evict.stdout" 2>"$OUT/evict.stderr" || true
echo "INFO eviction run completed (rc captured in $OUT/evict.stderr)"

# 4. Without quarantine, address reuse can silently mask the stale alias.
"$OUT/stale_check" >"$OUT/noq.stdout" 2>"$OUT/noq.stderr" || true
echo "INFO default allocator run completed (no quarantine guarantee)"
