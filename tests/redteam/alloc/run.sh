#!/usr/bin/env bash
# Red-team (attacker) run for the checked allocation APIs.
# Creates/modifies ONLY files under tests/redteam/alloc/ (see .gitignore).
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
RES="$HERE/results"
mkdir -p "$RES"

SAFE_ARCHIVE="$ROOT/build/safe/libcplus_safecrt.a"
HARD_ARCHIVE="$ROOT/build/libc/libcplus_hardened.a"
CC=${CC:-/usr/bin/clang}
CFLAGS="-std=c11 -O1 -g -Wall -Wextra -fno-omit-frame-pointer"
NCASES=${NCASES:-6000}

echo "[redteam/alloc] root=$ROOT"
echo "[redteam/alloc] compiler=$($CC --version | head -1)"
echo "[redteam/alloc] cases=$NCASES"

if [[ ! -f "$SAFE_ARCHIVE" || ! -f "$HARD_ARCHIVE" ]]; then
    echo "[redteam/alloc] ERROR: missing archive" >&2
    exit 2
fi

# ---- nm preflight: required target symbols in BOTH archives ----------------
nm -gU "$SAFE_ARCHIVE" > "$RES/nm_safecrt.txt"
nm -gU "$HARD_ARCHIVE" > "$RES/nm_hardened.txt"

miss=0
echo "[redteam/alloc] verifying _s targets in safecrt archive:"
for s in malloc_s calloc_s realloc_s free_s free_checked_s aligned_alloc_s \
         cplus_safe_alloc_get_meta cplus_safe_ptr_is_live cplus_safe_ptr_was_freed \
         cplus_safe_ptr_size cplus_safe_ptr_elem_size cplus_safe_ptr_count \
         cplus_safe_query_length cplus_safe_ptr_check_generation \
         cplus_safe_remaining_capacity cplus_safe_alloc_live_count \
         cplus_safe_alloc_live_bytes cplus_safe_transfer_ownership \
         cplus_safe_alloc_fail_after cplus_safe_alloc_fail_reset \
         cplus_safe_alloc_seq; do
    if grep -Eq " [Tt] _?${s}\$" "$RES/nm_safecrt.txt"; then
        echo "  OK   $s"
    else
        echo "  MISS $s" >&2; miss=1
    fi
done
echo "[redteam/alloc] verifying hardened-archive counterparts:"
for s in cplus_malloc cplus_calloc cplus_realloc cplus_free cplus_free_checked \
         cplus_alloc_get_meta cplus_alloc_is_live cplus_alloc_size \
         cplus_alloc_live_count cplus_alloc_double_free_count \
         cplus_alloc_invalid_free_count cplus_alloc_fail_after \
         cplus_alloc_fail_reset; do
    if grep -Eq " [Tt] _?${s}\$" "$RES/nm_hardened.txt"; then
        echo "  OK   $s"
    else
        echo "  MISS $s" >&2; miss=1
    fi
done
if [[ $miss -ne 0 ]]; then
    echo "[redteam/alloc] ERROR: nm preflight failed" >&2
    exit 2
fi
echo "[redteam/alloc] nm preflight: all target symbols present in both archives"

# ---- generate + build ------------------------------------------------------
python3 "$HERE/gen_cases.py" "$NCASES" | tee "$RES/generator.stdout"

$CC $CFLAGS -I"$ROOT/stdlib/safe" \
    "$HERE/redteam_alloc.c" "$SAFE_ARCHIVE" "$HARD_ARCHIVE" \
    -lm -pthread -o "$RES/redteam_alloc" 2> "$RES/build.stderr" || {
        cat "$RES/build.stderr" >&2; exit 2; }
echo "[redteam/alloc] build ok (stderr bytes: $(wc -c < "$RES/build.stderr"))"

# ---- run -------------------------------------------------------------------
set +e
/usr/bin/timeout 900 "$RES/redteam_alloc" \
    "$RES/cases.manifest" "$RES/scenarios.log" 2> "$RES/harness.stderr"
RC=$?
set -e
echo "[redteam/alloc] harness exit code: $RC"
if [[ -s "$RES/harness.stderr" ]]; then
    echo "[redteam/alloc] harness stderr:" >&2
    cat "$RES/harness.stderr" >&2
fi

# ---- digest (integrity record) ---------------------------------------------
if command -v shasum >/dev/null 2>&1; then
    shasum -a 256 "$RES/scenarios.log" "$RES/cases.manifest" > "$RES/digest.sha256"
else
    openssl dgst -sha256 "$RES/scenarios.log" "$RES/cases.manifest" > "$RES/digest.sha256"
fi
echo "[redteam/alloc] digest:" && cat "$RES/digest.sha256"

# Show any non-clean verdicts verbatim for the record.
echo "[redteam/alloc] non-REFUSED verdict lines:"
awk '$0 ~ /verdict=(CRASH|CORRUPT|UNEXPECTED_OK|SELFERR)/' "$RES/scenarios.log" | head -200
echo "[redteam/alloc] done."
exit 0
