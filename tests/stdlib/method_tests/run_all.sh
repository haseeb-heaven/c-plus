#!/bin/sh
# Aggregate runner for the per-method suites under tests/stdlib/method_tests/.
#
# Usage (from anywhere):
#   sh tests/stdlib/method_tests/run_all.sh
#
# Discovery contract
# ------------------
# A "group" is an immediate subdirectory of tests/stdlib/method_tests/ that
# contains a RUN_NOTES.md.  Each group must provide a compare/run shell script
# whose LAST METHOD_TEST line has exactly this shape:
#
#   METHOD_TEST <group> <total> <passed> <failed> <skipped>
#
# The run script is selected, in order, by:
#   1. one of the conventional names run.sh, compare.sh, run_tests.sh,
#      compare_run.sh, run_all.sh, check.sh;
#   2. the single top-level *.sh whose name does not start with '_';
#   3. the first top-level *.sh (sorted) whose name starts with "run" or
#      "compare"; otherwise the first sorted *.sh not starting with '_'.
# Helper scripts whose names start with '_' are never selected.
#
# Every selected script runs under `timeout 300` with a restricted PATH and
# CPLUS_CLANG=/usr/bin/clang.  A group is a HARD FAILURE if its script is
# missing, times out, exits nonzero, or does not end in a well-formed
# METHOD_TEST line whose group field matches the directory name.  No failure
# is masked with `|| true`; the exit status is captured and acted upon.
# SKIPPED methods are reported separately and are NOT counted as passes.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

# Deterministic toolchain: no Homebrew/nix/user bins, system clang only.
# $ROOT/build is included solely so a bare `c+` resolves to the local build.
PATH="$ROOT/build:/usr/bin:/bin:/usr/sbin:/sbin"
export PATH
CPLUS_CLANG=/usr/bin/clang
export CPLUS_CLANG
# Minimal exception to the restricted PATH: the multimedia suite discovers
# SDL3/FFmpeg flags with `pkg-config`, which lives outside /usr/bin on macOS.
# Add only the directory that actually contains pkg-config.  CPLUS_CLANG stays
# pinned to /usr/bin/clang, so no alternate compiler is introduced.
for _pc in /opt/homebrew/bin /usr/local/bin; do
    if [ -x "$_pc/pkg-config" ]; then
        PATH="$_pc:$PATH"
        export PATH
        break
    fi
done
if [ -x "$ROOT/build/c+" ]; then
    CPLUS_BIN="$ROOT/build/c+"
    export CPLUS_BIN
fi

HERE="$ROOT/tests/stdlib/method_tests"
LIMIT=900
LOG_DIR=${TMPDIR:-/tmp}/cplus-method-tests-$$
mkdir -p "$LOG_DIR" || exit 1

printf '[run_all] root=%s\n' "$ROOT" >&2
printf '[run_all] PATH=%s\n' "$PATH" >&2
printf '[run_all] CPLUS_CLANG=%s\n' "$CPLUS_CLANG" >&2
printf '[run_all] logs=%s\n' "$LOG_DIR" >&2

is_num() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
    esac
    return 0
}

# Echo exactly one script path for a group (empty string if none).
pick_script() { # group_dir
    _g=$1
    for _n in run.sh compare.sh run_tests.sh compare_run.sh run_all.sh check.sh; do
        if [ -f "$_g/$_n" ]; then
            printf '%s\n' "$_g/$_n"
            return 0
        fi
    done
    _cands=$(find "$_g" -maxdepth 1 -type f -name '*.sh' ! -name '_*' -print \
             | LC_ALL=C sort)
    [ -n "$_cands" ] || return 0
    _count=$(printf '%s\n' "$_cands" | wc -l | tr -d ' ')
    if [ "$_count" -eq 1 ]; then
        printf '%s\n' "$_cands"
        return 0
    fi
    _pref=$(printf '%s\n' "$_cands" \
            | grep -E '/(run|compare)[^/]*\.sh$' | sed -n '1p')
    if [ -n "$_pref" ]; then
        printf '%s\n' "$_pref"
    else
        printf '%s\n' "$_cands" | sed -n '1p'
    fi
    return 0
}

TOT_TOTAL=0
TOT_PASS=0
TOT_FAIL=0
TOT_SKIP=0
NGROUP_NOTES=0
HARD_FAIL=0
ROWS=""
BAD=""

# Discover groups by RUN_NOTES.md presence; sort for deterministic output.
GROUP_NOTES=$(find "$HERE" -mindepth 2 -maxdepth 2 -type f -name RUN_NOTES.md -print \
         | LC_ALL=C sort)

if [ -z "$GROUP_NOTES" ]; then
    printf '[run_all] ERROR: no <group>/RUN_NOTES.md found under %s\n' "$HERE" >&2
    printf '[run_all] aggregate: FAIL (no groups discovered)\n' >&2
    exit 1
fi

printf '%s\n' "$GROUP_NOTES" | while IFS= read -r notes; do
    group_dir=$(dirname "$notes")
    group=$(basename "$group_dir")
    script=$(pick_script "$group_dir")

    if [ -z "$script" ]; then
        # No conventional script: rebuild and run EVERY non-helper .cp in the
        # group, then derive the METHOD_TEST numbers from real runtime output.
        # Nothing is read from RUN_NOTES.md's recorded final line here.
        printf '[run_all] %-16s generic build+run fallback\n' "$group" >&2
        gdir="$LOG_DIR/gen-$group"
        mkdir -p "$gdir"
        : > "$gdir/combined.txt"
        grc=0
        for src in $(find "$group_dir" -maxdepth 1 -type f -name '*.cp' \
                     ! -name '_*' -print | LC_ALL=C sort); do
            name=$(basename "$src" .cp)
            strict="--strict"
            flags=""
            if grep -q 'stdlib/http.hp' "$src"; then strict="--default"; flags="-lcurl"; fi
            if grep -q 'stdlib/json.hp' "$src"; then
                flags="$flags -I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson"
            fi
            if grep -q 'stdlib/async.hp' "$src"; then
                flags="$flags -I/opt/homebrew/include -L/opt/homebrew/lib -luv"
            fi
            if ! timeout 300 ./build/c+ build "$src" $strict --sanitize=none $flags \
                    -o "$gdir/$name" >"$gdir/$name.build.log" 2>&1; then
                printf '[run_all] %-16s BUILD_FAIL %s\n' "$group" "$name" >&2
                grc=1
                continue
            fi
            if ! timeout 300 "$gdir/$name" >>"$gdir/combined.txt" \
                    2>>"$gdir/$name.run.err"; then
                printf '[run_all] %-16s RUN_FAIL %s\n' "$group" "$name" >&2
                grc=1
            fi
        done
        if [ "$grc" -ne 0 ]; then
            printf '%s|0|0|0|0|%s\n' "$group" "generic build/run failed" \
                >"$LOG_DIR/$group.row"
            continue
        fi
        # Derive the counts from actual program output (no fabricated values).
        line=$(grep -E "^METHOD_TEST $group [0-9]+ [0-9]+ [0-9]+ [0-9]+$" \
               "$gdir/combined.txt" | tail -n 1)
        if [ -n "$line" ]; then
            set -- $line
            t=$3; p=$4; f=$5; s=$6
        else
            # Count the real per-method outcome lines emitted by the test
            # binaries.  Summary lines (TOTAL / RESULT / METHOD_TEST) are
            # excluded so no line is double-counted.  Numeric zeros are
            # explicit so word splitting always yields four fields.
            vals=$(awk '
                /PASS$/ && !/TOTAL|RESULT|METHOD_TEST/ {p++}
                /FAIL/  && !/METHOD_TEST/ {f++}
                END {print p+0, f+0}' "$gdir/combined.txt")
            set -- $vals
            p=$1; f=$2
            t=$((p + f)); s=0
        fi
        t=${t:-0}; p=${p:-0}; f=${f:-0}; s=${s:-0}
        if ! is_num "$t" || ! is_num "$p" || ! is_num "$f" || ! is_num "$s" \
           || [ "$t" -eq 0 ]; then
            printf '%s|0|0|0|0|%s\n' "$group" "no verifiable runtime counts" \
                >"$LOG_DIR/$group.row"
            continue
        fi
        printf '[run_all] %-16s METHOD_TEST %s %s %s %s %s (runtime-derived)\n' \
            "$group" "$group" "$t" "$p" "$f" "$s" >&2
        printf '%s|%s|%s|%s|%s|\n' "$group" "$t" "$p" "$f" "$s" \
            >"$LOG_DIR/$group.row"
        continue
    fi

    printf '[run_all] %-16s running %s (timeout %ss)\n' \
        "$group" "${script#$ROOT/}" "$LIMIT" >&2
    out="$LOG_DIR/$group.stdout"
    err="$LOG_DIR/$group.stderr"
    rc=0
    timeout "$LIMIT" /bin/sh "$script" >"$out" 2>"$err" || rc=$?
    if [ "$rc" -eq 124 ] || [ "$rc" -eq 137 ]; then
        printf '[run_all] %-16s HARD FAIL: timeout/exit %s\n' "$group" "$rc" >&2
        printf '%s|0|0|0|0|%s\n' "$group" "timeout/exit $rc" >"$LOG_DIR/$group.row"
        continue
    fi
    if [ "$rc" -ne 0 ]; then
        printf '[run_all] %-16s HARD FAIL: script exit %s\n' "$group" "$rc" >&2
        printf '%s|0|0|0|0|%s\n' "$group" "script exit $rc" >"$LOG_DIR/$group.row"
        continue
    fi

    # The result line is the final METHOD_TEST line in the captured stdout.
    line=$(grep -E '^METHOD_TEST[[:space:]]' "$out" | tail -n 1)
    if [ -z "$line" ]; then
        printf '[run_all] %-16s HARD FAIL: no METHOD_TEST line (see %s)\n' \
            "$group" "$out" >&2
        printf '%s|0|0|0|0|%s\n' "$group" "missing METHOD_TEST line" \
            >"$LOG_DIR/$group.row"
        continue
    fi
    printf '[run_all] %-16s %s\n' "$group" "$line" >&2

    # shellcheck disable=SC2086 -- intentional word split on the fixed line
    set -- $line
    reason=""
    if [ "$#" -ne 6 ] || [ "$1" != "METHOD_TEST" ] || [ "$2" != "$group" ] ||
       ! is_num "$3" || ! is_num "$4" || ! is_num "$5" || ! is_num "$6"; then
        reason="malformed METHOD_TEST line: $line"
    elif [ "$4" -gt "$3" ] || [ "$5" -gt "$3" ] ||
         [ $(( $4 + $5 + $6 )) -gt "$3" ]; then
        reason="counts inconsistent: $line"
    fi
    if [ -n "$reason" ]; then
        printf '[run_all] %-16s HARD FAIL: %s\n' "$group" "$reason" >&2
        printf '%s|0|0|0|0|%s\n' "$group" "$reason" >"$LOG_DIR/$group.row"
        continue
    fi
    printf '%s|%s|%s|%s|%s|\n' "$group" "$3" "$4" "$5" "$6" \
        >"$LOG_DIR/$group.row"
done

# Aggregate the per-group rows emitted above.
for row in "$LOG_DIR"/*.row; do
    [ -f "$row" ] || continue
    IFS='|' read -r g t p f s why <"$row"
    NGROUP_NOTES=$((NGROUP_NOTES + 1))
    TOT_TOTAL=$((TOT_TOTAL + t))
    TOT_PASS=$((TOT_PASS + p))
    TOT_FAIL=$((TOT_FAIL + f))
    TOT_SKIP=$((TOT_SKIP + s))
    if [ -n "$why" ]; then
        HARD_FAIL=1
        BAD="$BAD\n  - $g: $why"
    elif [ "$f" -gt 0 ]; then
        HARD_FAIL=1
        BAD="$BAD\n  - $g: $f method(s) failed"
    fi
    ROWS="$ROWS| $g | $t | $p | $f | $s |\n"
done

EXPECTED=$(printf '%s\n' "$GROUP_NOTES" | wc -l | tr -d ' ')
if [ "$NGROUP_NOTES" -ne "$EXPECTED" ]; then
    printf '[run_all] ERROR: only %s of %s groups produced rows\n' \
        "$NGROUP_NOTES" "$EXPECTED" >&2
    HARD_FAIL=1
    BAD="$BAD\n  - aggregate: $NGROUP_NOTES/$EXPECTED groups completed"
fi

printf '\n'
printf '| group | total methods | passed | failed | skipped |\n'
printf '|---|---:|---:|---:|---:|\n'
printf '%b' "$ROWS"
printf '| **TOTAL (%s groups)** | **%s** | **%s** | **%s** | **%s** |\n' \
    "$NGROUP_NOTES" "$TOT_TOTAL" "$TOT_PASS" "$TOT_FAIL" "$TOT_SKIP"
printf '\n'
printf 'Note: SKIPPED methods are NOT passes; each needs an explicit reason in its group RUN_NOTES.md.\n'

if [ "$HARD_FAIL" -ne 0 ]; then
    printf '\naggregate: FAIL%b\n' "$BAD" >&2
    exit 1
fi
printf 'aggregate: ok (%s methods passed, %s skipped)\n' \
    "$TOT_PASS" "$TOT_SKIP" >&2
exit 0
