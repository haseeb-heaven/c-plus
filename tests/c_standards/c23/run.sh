#!/usr/bin/env bash
# C23 support test for the C+ compiler/driver (subagent 5/10).
#
# Reproduces every result documented in RESULTS.md. All artifacts are written
# under this directory (tests/c_standards/c23/logs/). Nothing outside this
# directory is created or modified, and no project source is edited.
#
# Usage:  bash tests/c_standards/c23/run.sh
set -u

HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$HERE/../../.." && pwd)"
CP="$ROOT/build/c+"
CLANG=/usr/bin/clang
LOGDIR="$HERE/logs"
PROBES="$LOGDIR/probes"
mkdir -p "$PROBES"

die() { echo "FATAL: $*" >&2; exit 2; }
[ -x "$CP" ]    || die "C+ driver not found at $CP"
[ -x "$CLANG" ] || die "clang not found at $CLANG"

# Run a command, capture exit/stdout/stderr into $1.* files, print a summary.
capture() {
    local tag="$1"; shift
    "$@" >"$LOGDIR/$tag.out" 2>"$LOGDIR/$tag.err"
    local ec=$?
    printf '  %-42s exit=%d\n' "$tag" "$ec"
    return $ec
}

hr() { echo; echo "======================================================================"; echo "$*"; echo "======================================================================"; }

# --------------------------------------------------------------------------
hr "0. HOST / TOOLCHAIN"
# --------------------------------------------------------------------------
echo "date:    $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
echo "uname:   $(uname -a)"
"$CLANG" --version | head -2
[ -f "$ROOT/build/libc/libcplus_hardened.a" ] && echo "libc archive: present" || echo "libc archive: MISSING"

# --------------------------------------------------------------------------
hr "1. FEATURE PROBE: which C23 features compile with plain clang -std=c2x"
# --------------------------------------------------------------------------
cat >"$PROBES/nullptr.c" <<'EOF'
#include <stdio.h>
int main(void) { int *p = nullptr; printf("%d\n", p == nullptr); return 0; }
EOF
cat >"$PROBES/typeof.c" <<'EOF'
#include <stdio.h>
int main(void) { typeof(42) x = 7; printf("%d\n", x); return 0; }
EOF
cat >"$PROBES/binary.c" <<'EOF'
#include <stdio.h>
int main(void) { printf("%d\n", 0b1010); return 0; }
EOF
cat >"$PROBES/elifdef.c" <<'EOF'
#include <stdio.h>
#define WANT 1
int main(void) {
#if 0
#elifdef WANT
  puts("elifdef-taken");
#else
  puts("elifdef-else");
#endif
  return 0;
}
EOF
cat >"$PROBES/stdver.c" <<'EOF'
#include <stdio.h>
int main(void) {
#ifndef __STDC_VERSION__
  puts("NO_STDC_VERSION");
#else
  printf("__STDC_VERSION__=%ld\n", (long)__STDC_VERSION__);
#endif
  return 0;
}
EOF

for f in nullptr typeof binary elifdef stdver; do
    capture "probe_plain_c2x_$f" "$CLANG" -std=c2x "$PROBES/$f.c" -o "$PROBES/$f.bin"
    ec=$?
    if [ $ec -eq 0 ]; then
        echo "    stdout: $("$PROBES/$f.bin" 2>&1)"
    fi
    echo "    stderr: $(head -1 "$LOGDIR/probe_plain_c2x_$f.err")"
done

# Confirm these are genuinely C23-only (default clang is C17): nullptr must fail.
echo "-- default mode (no -std) sanity check: nullptr must FAIL --"
capture "probe_plain_default_nullptr" "$CLANG" "$PROBES/nullptr.c" -o "$PROBES/nullptr_default.bin"
echo "    first diag: $(head -1 "$LOGDIR/probe_plain_default_nullptr.err")"

# --------------------------------------------------------------------------
hr "2. -std SPELLING ACCEPTANCE: plain clang (4 spellings, probe nullptr.c)"
# --------------------------------------------------------------------------
for std in c23 c2x gnu23 gnu2x; do
    capture "clang_std_$std" "$CLANG" -std=$std "$PROBES/nullptr.c" -o "$PROBES/sp_$std.bin"
    echo "    first diag: $(head -1 "$LOGDIR/clang_std_$std.err")"
done

# --------------------------------------------------------------------------
hr "3. source.c MATRIX: plain clang vs C+ driver x {c23,c2x,gnu23,gnu2x}"
# --------------------------------------------------------------------------
echo "-- plain clang (no C+-specific flags) --"
for std in c23 c2x gnu23 gnu2x; do
    capture "src_plain_$std" "$CLANG" "$HERE/source.c" -std=$std -o "$LOGDIR/src_plain_$std.bin"
    ec=$?
    if [ $ec -eq 0 ]; then
        timeout 30 "$LOGDIR/src_plain_$std.bin" >"$LOGDIR/src_plain_$std.run.out" 2>&1
        echo "    run_exit=$? stdout=[$(cat "$LOGDIR/src_plain_$std.run.out")]"
    else
        echo "    first diag: $(head -1 "$LOGDIR/src_plain_$std.err")"
    fi
done

echo "-- C+ driver (--sanitize=none) --"
for std in c23 c2x gnu23 gnu2x; do
    capture "src_cplus_$std" "$CP" build "$HERE/source.c" -std=$std --sanitize=none -o "$LOGDIR/src_cplus_$std.bin"
    ec=$?
    if [ $ec -eq 0 ]; then
        timeout 30 "$LOGDIR/src_cplus_$std.bin" >"$LOGDIR/src_cplus_$std.run.out" 2>&1
        echo "    run_exit=$? stdout=[$(cat "$LOGDIR/src_cplus_$std.run.out")]"
    else
        echo "    first diag: $(head -1 "$LOGDIR/src_cplus_$std.err")"
    fi
done

# --------------------------------------------------------------------------
hr "4. C+ FRONTEND: nullptr special-case rejection (any -std)"
# --------------------------------------------------------------------------
capture "cplus_nullptr_reject" "$CP" build "$PROBES/nullptr.c" -std=c2x --sanitize=none -o "$LOGDIR/nullptr.bin"
echo "--- exact C+ diagnostics for nullptr ---"
cat "$LOGDIR/cplus_nullptr_reject.err"

# --------------------------------------------------------------------------
hr "5. REAL PROJECTS via C+ driver (-std=c23 vs -std=c2x)"
# --------------------------------------------------------------------------
declare -a PROJ_SRC=(snake.c 2048.c xdump.c)
declare -a PROJ_TAG=(snake 2048 xdump)
declare -a PROJ_PATH=(
    "$ROOT/external_projects/game_a/original/snake.c"
    "$ROOT/external_projects/game_b/original/2048.c"
    "$ROOT/external_projects/cli_app/original/xdump.c"
)

echo "-- default (safe) mode: expect C+ safety diagnostics, no binary --"
for i in 0 1 2; do
    for std in c23 c2x; do
        tag="proj_${PROJ_TAG[$i]}_safe_$std"
        capture "$tag" "$CP" build "${PROJ_PATH[$i]}" -std=$std --sanitize=none -o "$LOGDIR/${tag}.bin"
        echo "    first diag: $(head -1 "$LOGDIR/$tag.err")"
    done
done

echo "-- --legacy-c mode: c2x builds, c23 rejected by clang backend --"
for i in 0 1 2; do
    for std in c23 c2x; do
        tag="proj_${PROJ_TAG[$i]}_legacy_$std"
        capture "$tag" "$CP" build "${PROJ_PATH[$i]}" --legacy-c -std=$std --sanitize=none -o "$LOGDIR/${tag}.bin"
        echo "    first diag: $(head -1 "$LOGDIR/$tag.err")"
    done
done

echo "-- run c2x legacy binaries under timeout 30, compare to recorded originals --"
# game_a: deterministic 'q' quit
printf 'q' | timeout 30 "$LOGDIR/proj_snake_legacy_c2x.bin" >"$LOGDIR/cmp_snake.out" 2>/dev/null
cmp -s "$LOGDIR/cmp_snake.out" "$ROOT/external_projects/game_a/build/run.stdout" \
    && echo "  game_a snake stdout: IDENTICAL to recorded original" \
    || echo "  game_a snake stdout: DIFFERENT"

# game_b: self-test, version, help
for a in test --version --help; do
    t="${a#--}"
    timeout 30 "$LOGDIR/proj_2048_legacy_c2x.bin" "$a" >"$LOGDIR/cmp_2048_$t.out" 2>/dev/null
    rec="$ROOT/external_projects/game_b/run_${t}_stdout.log"
    cmp -s "$LOGDIR/cmp_2048_$t.out" "$rec" \
        && echo "  game_b $a stdout: IDENTICAL to recorded original" \
        || echo "  game_b $a stdout: DIFFERENT"
done

# cli_app: deterministic cases; normalize argv[0] for the bad_flag message
FIX="$ROOT/external_projects/cli_app/build/fixtures/sample.bin"
for spec in "version|-v" "help|-h" "file_default|$FIX" "file_width8|-w 8 -g 4 $FIX" "file_skip_n|-s 32 -n 16 $FIX"; do
    t="${spec%%|*}"; args="${spec#*|}"
    # shellcheck disable=SC2086
    timeout 30 "$LOGDIR/proj_xdump_legacy_c2x.bin" $args >"$LOGDIR/cmp_xdump_$t.out" 2>/dev/null
    cmp -s "$LOGDIR/cmp_xdump_$t.out" "$ROOT/external_projects/cli_app/build/runs/$t.stdout" \
        && echo "  cli_app $t stdout: IDENTICAL to recorded original" \
        || echo "  cli_app $t stdout: DIFFERENT"
done
timeout 30 "$LOGDIR/proj_xdump_legacy_c2x.bin" </dev/null >/dev/null 2>/dev/null || true
timeout 30 "$LOGDIR/proj_xdump_legacy_c2x.bin" <"$FIX" >"$LOGDIR/cmp_xdump_stdin_default.out" 2>/dev/null
cmp -s "$LOGDIR/cmp_xdump_stdin_default.out" "$ROOT/external_projects/cli_app/build/runs/stdin_default.stdout" \
    && echo "  cli_app stdin_default stdout: IDENTICAL to recorded original" \
    || echo "  cli_app stdin_default stdout: DIFFERENT"
timeout 30 "$LOGDIR/proj_xdump_legacy_c2x.bin" -Z >"$LOGDIR/cmp_xdump_bad_flag.out" 2>"$LOGDIR/cmp_xdump_bad_flag.err"; echo "  cli_app bad_flag exit=$?"
sed 's#^.*: illegal option#PROG: illegal option#' "$LOGDIR/cmp_xdump_bad_flag.err" >"$LOGDIR/cmp_bad.norm"
sed 's#^.*: illegal option#PROG: illegal option#' "$ROOT/external_projects/cli_app/build/runs/bad_flag.stderr" >"$LOGDIR/rec_bad.norm"
cmp -s "$LOGDIR/cmp_bad.norm" "$LOGDIR/rec_bad.norm" \
    && echo "  cli_app bad_flag stderr: IDENTICAL after argv[0] normalization" \
    || echo "  cli_app bad_flag stderr: DIFFERENT"

# --------------------------------------------------------------------------
hr "6. C23 LIBRARY API INVENTORY (nm -g build/libc/libcplus_hardened.a)"
# --------------------------------------------------------------------------
ARCHIVE="$ROOT/build/libc/libcplus_hardened.a"
if [ -f "$ARCHIVE" ]; then
    nm -g "$ARCHIVE" 2>/dev/null | awk '{print $3}' | grep '^_cplus_' | sort -u >"$LOGDIR/c23_syms_all.txt"
    echo "total unique cplus_* symbols: $(wc -l <"$LOGDIR/c23_syms_all.txt")"
    echo "-- C23 API families present (unique symbols) --"
    for pat in nextup nextdown fadd fsub fmul fdiv hypot3 exp16 fma16 frexp16 logb16 timespec_getres mbrtoc8 c8rtomb; do
        printf '  %-18s' "$pat:"
        grep "$pat" "$LOGDIR/c23_syms_all.txt" | tr '\n' ' '
        echo
    done
else
    echo "archive missing; skipping"
fi

hr "DONE"
echo "Artifacts: $LOGDIR"
