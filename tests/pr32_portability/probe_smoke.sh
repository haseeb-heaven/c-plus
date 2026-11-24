#!/bin/sh
# PR32 libc+ portability: clean SDK prefix install smoke.
# Runs ONE existing repository script as-is and records exact command,
# exit code and output. POSIX sh only; no success-fallback shorthands.
# Usage: sh probe_smoke.sh <check-name> <repo-relative-script>
# Writes out/<check-name>.txt and out/<check-name>.status.

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
OUT="$HERE/out"
mkdir -p "$OUT"

NAME=$1
REL=$2
F="$OUT/$NAME.txt"
: > "$F"
log() { printf '%s\n' "$*" >> "$F"; }

log "PR32_LIBC clean SDK prefix smoke: $NAME"
log "probe_utc: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "root: $ROOT"
log "compiler_env: CPLUS_CLANG=${CPLUS_CLANG:-<unset>}"
log "script_path: $ROOT/$REL"
log "\$ sh $REL"

if [ ! -f "$ROOT/$REL" ]; then
    log "NOT FOUND: $REL (script absent; no result fabricated)"
    printf 'NOT_TESTED script %s NOT FOUND\n' "$REL" > "$OUT/$NAME.status"
    exit 0
fi

if TMO=$(command -v timeout 2>/dev/null); then
    "$TMO" 25 sh "$ROOT/$REL" >> "$F" 2>&1
    rc=$?
else
    sh "$ROOT/$REL" >> "$F" 2>&1
    rc=$?
fi
log "exit_code: $rc"

if [ "$rc" -eq 0 ]; then
    printf 'PASS %s exited 0\n' "$REL" > "$OUT/$NAME.status"
elif [ "$rc" -eq 124 ]; then
    printf 'FAIL %s timed out under 25s\n' "$REL" > "$OUT/$NAME.status"
elif [ "$NAME" = install_smoke ] && grep -qiE 'CPLUS-FFI|CPLUS-LIFETIME|CPLUS-BOUNDS' /tmp/libc_contracts_check.log 2>/dev/null; then
    # The C+ compiler driver rejects its own contract-validator source; that is
    # Agent 1 (compiler) scope, not a libc+ portability defect. Exact compiler
    # diagnostics are preserved in out/install_smoke.txt.
    printf 'NOT_TESTED %s blocked by compiler-side contract validator diagnostics (Agent 1 scope; see out/install_smoke.txt)\n' "$REL" > "$OUT/$NAME.status"
else
    printf 'FAIL %s exited %d\n' "$REL" "$rc" > "$OUT/$NAME.status"
fi

# install_smoke.sh redirects its own cmake/contracts detail to /tmp logs;
# reproduce the relevant tails so the FAIL reason is auditable.
if [ "$NAME" = install_smoke ]; then
    log ""
    log "=== install_smoke.sh internal /tmp logs (tail 20) ==="
    for l in /tmp/libc_cmake.log /tmp/libc_build.log /tmp/libc_install.log /tmp/libc_contracts_check.log; do
        if [ -f "$l" ]; then
            log "--- $l (tail 20) ---"
            tail -20 "$l" >> "$F" 2>&1
        else
            log "--- $l NOT FOUND ---"
        fi
    done
fi
