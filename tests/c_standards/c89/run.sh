#!/bin/sh
# Reproducible C89/C90 support matrix for the C+ compiler driver.
#
# All artifacts are written inside this directory. No source files outside
# this directory are modified (the driver's own transient .cplus-tmp-*.c
# files are created and removed by the driver itself in the input directory).
#
# Usage:  sh tests/c_standards/c89/run.sh [log-file]
# Default log: tests/c_standards/c89/run.log

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../.." && pwd)
CPLUS="$ROOT/build/c+"
CLANG=/usr/bin/clang
LOG=${1:-"$HERE/run.log"}
PROJ="$ROOT/external_projects"

# Private temp namespace: several agents may run tests concurrently.
TMPDIR_PRIV=$(mktemp -d "$HERE/.c89tmp.XXXXXX") || exit 2
trap 'rm -rf "$TMPDIR_PRIV"' EXIT HUP INT TERM
TMPLOG="$TMPDIR_PRIV/matrix.log"
: > "$TMPLOG"

log() { printf '%s\n' "$*" >> "$TMPLOG"; }

# POSIX sh has no PIPESTATUS. Redirect to a private temp file, capture the
# exact status, then append the captured output to the private log.
CMD_EXIT=0
run() {
    log "\$ $*"
    timeout 30 "$@" > "$TMPDIR_PRIV/out" 2>&1
    CMD_EXIT=$?
    cat "$TMPDIR_PRIV/out" >> "$TMPLOG"
    log "[exit=$CMD_EXIT]"
    return 0
}

log "=== C89/C90 support matrix ==="
log "date: $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "driver: $CPLUS"
log "clang:  $CLANG ($($CLANG --version 2>&1 | head -1))"
log "root:   $ROOT"
log ""

cd "$HERE" || exit 2

# ---------------------------------------------------------------------------
# 1. Genuine C89 program, reference compiler
# ---------------------------------------------------------------------------
log "--- 1. Reference: plain clang on genuine C89 source ---"
run "$CLANG" -std=c89 -pedantic-errors hello89.c -o hello89.ref
log "RESULT reference clang -std=c89 -pedantic-errors hello89.c exit=$CMD_EXIT"

# ---------------------------------------------------------------------------
# 2. C+ driver: genuine C89 source, all C90 std spellings
# ---------------------------------------------------------------------------
log ""
log "--- 2. C+ driver: hello89.c, -std spelling variants ---"
for s in c89 c90 iso9899:1990 gnu89; do
    run "$CPLUS" build hello89.c "-std=$s" --sanitize=none -o "hello89.cplus.$s"
    log "RESULT spelling=$s source=hello89.c build_exit=$CMD_EXIT"
done

log ""
log "--- 3. C+ driver: hello89.c, -std spelling variants + -pedantic-errors ---"
for s in c89 c90 iso9899:1990 gnu89; do
    run "$CPLUS" build hello89.c "-std=$s" -pedantic-errors --sanitize=none -o "hello89.ped.$s"
    log "RESULT spelling=$s source=hello89.c pedantic=1 build_exit=$CMD_EXIT"
done

# ---------------------------------------------------------------------------
# 3. Negative control: prove -std is forwarded to the C backend
# ---------------------------------------------------------------------------
log ""
log "--- 4. Negative control: C99 mixed declaration in probe_c99.c ---"
run "$CLANG" -std=c89 -pedantic-errors -fsyntax-only probe_c99.c
log "RESULT reference probe_c99.c c89 pedantic exit=$CMD_EXIT (expected nonzero)"
run "$CPLUS" build probe_c99.c -std=c89 --sanitize=none -o probe.cplus.c89
log "RESULT c+ probe_c99.c -std=c89 (no pedantic) exit=$CMD_EXIT"
run "$CPLUS" build probe_c99.c -std=c89 -pedantic-errors --sanitize=none -o probe.cplus.ped
log "RESULT c+ probe_c99.c -std=c89 -pedantic-errors exit=$CMD_EXIT (expected nonzero)"

# ---------------------------------------------------------------------------
# 4. Run both binaries under timeout 30, compare stdout byte-for-byte
# ---------------------------------------------------------------------------
log ""
log "--- 5. Runtime comparison (timeout 30) ---"
run ./hello89.ref
log "RESULT run hello89.ref exit=$CMD_EXIT"
cp "$TMPDIR_PRIV/out" "$HERE/stdout.ref"

for s in c89 c90 iso9899:1990 gnu89; do
    if [ -x "hello89.cplus.$s" ]; then
        run "./hello89.cplus.$s"
        log "RESULT run hello89.cplus.$s exit=$CMD_EXIT"
        cp "$TMPDIR_PRIV/out" "$HERE/stdout.cplus.$s"
        if cmp -s stdout.ref "stdout.cplus.$s"; then
            log "RESULT stdout comparison spelling=$s IDENTICAL"
        else
            log "RESULT stdout comparison spelling=$s DIFFERENT"
        fi
        log "sha256 ref=$(shasum -a 256 stdout.ref | awk '{print $1}') cplus.$s=$(shasum -a 256 "stdout.cplus.$s" | awk '{print $1}')"
    fi
done

# ---------------------------------------------------------------------------
# 5. Real downloaded projects with -std=c89
# ---------------------------------------------------------------------------
log ""
log "--- 6. Real projects with -std=c89 ---"
for p in game_a/original/snake.c game_b/original/2048.c cli_app/original/xdump.c; do
    name=$(basename "$p" .c)
    log ">>> project: $p"

    run "$CLANG" -std=c89 -pedantic-errors -fsyntax-only "$PROJ/$p"
    log "RESULT clang_ref -std=c89 -pedantic-errors $p exit=$CMD_EXIT"

    run "$CPLUS" build "$PROJ/$p" -std=c89 --sanitize=none -o "$HERE/c89-$name.cplus"
    log "RESULT c+ default(Safe) -std=c89 $p exit=$CMD_EXIT"

    run "$CPLUS" build "$PROJ/$p" --legacy-c -std=c89 -pedantic-errors --sanitize=none -o "$HERE/c89-$name.legacy"
    log "RESULT c+ --legacy-c -std=c89 -pedantic-errors $p exit=$CMD_EXIT"
done

# ---------------------------------------------------------------------------
# 6. Driver spelling validation
# ---------------------------------------------------------------------------
log ""
log "--- 7. Unknown -std spelling ---"
run "$CPLUS" build hello89.c -std=bogus --sanitize=none -o hello89.bogus
log "RESULT -std=bogus exit=$CMD_EXIT (expected nonzero)"

log ""
log "=== end of matrix ==="
cp "$TMPLOG" "$LOG"
printf 'matrix written to %s\n' "$LOG"
