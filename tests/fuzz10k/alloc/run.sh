#!/usr/bin/env bash
# Deterministic 10k-scenario fuzz run against the checked allocation APIs.
# Creates ONLY files under tests/fuzz10k/alloc/results/.
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
RES="$HERE/results"
mkdir -p "$RES"

SAFE_ARCHIVE="$ROOT/build/safe/libcplus_safecrt.a"
HARD_ARCHIVE="$ROOT/build/libc/libcplus_hardened.a"
CC=${CC:-/usr/bin/clang}
CFLAGS="-std=c11 -O1 -g -Wall -Wextra -pedantic"

echo "[fuzz10k/alloc] root=$ROOT"
echo "[fuzz10k/alloc] compiler=$($CC --version | head -1)"

# ---- nm preflight: required symbols must exist in BOTH archives -----------
if [[ ! -f "$SAFE_ARCHIVE" || ! -f "$HARD_ARCHIVE" ]]; then
    echo "[fuzz10k/alloc] ERROR: missing archive" >&2
    exit 2
fi
nm -gU "$SAFE_ARCHIVE" > "$RES/nm_safecrt.txt"
nm -gU "$HARD_ARCHIVE" > "$RES/nm_hardened.txt"

miss=0
for s in malloc_s calloc_s realloc_s free_s free_checked_s aligned_alloc_s \
         cplus_safe_ptr_is_live cplus_safe_ptr_size cplus_safe_ptr_was_freed \
         cplus_safe_alloc_get_meta cplus_safe_ptr_check_generation \
         cplus_safe_alloc_live_count cplus_safe_alloc_live_bytes \
         cplus_safe_alloc_seq cplus_safe_alloc_fail_after \
         cplus_safe_alloc_fail_reset; do
    grep -Eq " [Tt] _?${s}\$" "$RES/nm_safecrt.txt" || { echo "[fuzz10k/alloc] MISSING safecrt: $s" >&2; miss=1; }
done
for s in cplus_malloc cplus_free_checked cplus_alloc_is_live cplus_alloc_size \
         cplus_alloc_live_count cplus_alloc_double_free_count \
         cplus_alloc_fail_after cplus_alloc_fail_reset; do
    grep -Eq " [Tt] _?${s}\$" "$RES/nm_hardened.txt" || { echo "[fuzz10k/alloc] MISSING hardened: $s" >&2; miss=1; }
done
if [[ $miss -ne 0 ]]; then
    echo "[fuzz10k/alloc] ERROR: nm preflight failed" >&2
    exit 2
fi
echo "[fuzz10k/alloc] nm preflight: all required symbols present in both archives"

# ---- build ---------------------------------------------------------------
$CC $CFLAGS -I"$ROOT/libc/install-include" -I"$ROOT" \
    "$HERE/fuzz_alloc.c" "$SAFE_ARCHIVE" "$HARD_ARCHIVE" \
    -lm -pthread -o "$RES/fuzz_alloc" || exit 2
echo "[fuzz10k/alloc] build ok: $RES/fuzz_alloc"

set +e
/usr/bin/timeout 600 "$RES/fuzz_alloc" "$RES/scenarios.log" 10000
RC=$?
set -e

# ---- integrity record ----------------------------------------------------
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$RES/scenarios.log" > "$RES/scenarios.log.sha256"
else
    openssl dgst -sha256 "$RES/scenarios.log" > "$RES/scenarios.log.sha256"
fi
echo "[fuzz10k/alloc] log sha256: $(cat "$RES/scenarios.log.sha256")"
echo "[fuzz10k/alloc] harness exit code: $RC"
exit $RC
