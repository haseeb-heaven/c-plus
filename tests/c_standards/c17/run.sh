#!/bin/sh
# C17 / C18 standard-support evidence harness (subagent 4/10).
#
# Regenerates every raw log under artifacts/. No file outside this
# tests/c_standards/c17/ directory is created or modified.
#
#   cd <repo-root> && sh tests/c_standards/c17/run.sh
#
# The C+ driver writes lowered-C temp files BESIDE the input source, so all
# external_projects sources are first copied byte-for-byte into artifacts/
# (verified with cmp) before being handed to the driver. The originals under
# external_projects/ are only ever READ.

set -u

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/../../.." && pwd)
CP="$ROOT/build/c+"
CLANG=/usr/bin/clang
SRC_REL=tests/c_standards/c17/source.c
A_REL=tests/c_standards/c17/artifacts
A="$ROOT/$A_REL"
SRC="$ROOT/$SRC_REL"

cd "$ROOT" || exit 1
mkdir -p "$A/build"

EXPECTED='c17: alignof(int)=4 type=int counter=7'
SPELLINGS='c17 c18 iso9899:2017 iso9899:2018 gnu17'

# Record an exact command line for the audit trail.
cmd_log="$A/commands.txt"
: > "$cmd_log"
note() { printf '%s\n' "$*" >> "$cmd_log"; printf '%s\n' "$*"; }

slug() { printf '%s' "$1" | tr ':' '_'; }

# ---------------------------------------------------------------------------
# 1. Plain clang reference builds: -std=<val> -pedantic-errors
# ---------------------------------------------------------------------------
printf 'SECTION 1: plain clang -std=<val> -pedantic-errors\n' >> "$cmd_log"
for s in $SPELLINGS; do
    k=$(slug "$s")
    bin="$A/clang_$k"
    note "$CLANG -std=$s -pedantic-errors $SRC_REL -o $A_REL/clang_$k"
    "$CLANG" -std="$s" -pedantic-errors "$SRC_REL" -o "$bin" \
        > "$A/clang_$k.stdout" 2> "$A/clang_$k.stderr"
    echo $? > "$A/clang_$k.exit"
    if [ -x "$bin" ]; then
        note "timeout 30 $A_REL/clang_$k"
        timeout 30 "$bin" > "$A/clang_$k.run.stdout" 2> "$A/clang_$k.run.stderr"
        echo $? > "$A/clang_$k.run.exit"
    fi
done

# ---------------------------------------------------------------------------
# 2. C+ driver builds of the same source, identical -std spellings
# ---------------------------------------------------------------------------
printf 'SECTION 2: C+ driver -std=<val> --sanitize=none\n' >> "$cmd_log"
for s in $SPELLINGS; do
    k=$(slug "$s")
    bin="$A/cplus_$k"
    note "./build/c+ build $SRC_REL -std=$s --sanitize=none -o $A_REL/cplus_$k"
    ./build/c+ build "$SRC_REL" -std="$s" --sanitize=none -o "$bin" \
        > "$A/cplus_$k.stdout" 2> "$A/cplus_$k.stderr"
    echo $? > "$A/cplus_$k.exit"
    if [ -x "$bin" ]; then
        note "timeout 30 $A_REL/cplus_$k"
        timeout 30 "$bin" > "$A/cplus_$k.run.stdout" 2> "$A/cplus_$k.run.stderr"
        echo $? > "$A/cplus_$k.run.exit"
    fi
    # Prove the spelling reaches clang's backend (last -std wins).
    note "./build/c+ build $SRC_REL -std=$s --sanitize=none --verbose -o $A_REL/verbose_$k"
    ./build/c+ build "$SRC_REL" -std="$s" --sanitize=none --verbose \
        -o "$A/verbose_$k" > "$A/cplus_$k.verbose.stdout" 2> "$A/cplus_$k.verbose.stderr"
    grep '^  -std=' "$A/cplus_$k.verbose.stdout" > "$A/cplus_$k.effective_std" 2>/dev/null || true
done

# Negative controls: prove -std is honoured, not merely accepted.
note "$CLANG -std=c89 -pedantic-errors $SRC_REL (expected failure)"
"$CLANG" -std=c89 -pedantic-errors "$SRC_REL" -o "$A/neg_c89_plain" \
    > "$A/neg_c89_plain.stdout" 2> "$A/neg_c89_plain.stderr"
echo $? > "$A/neg_c89_plain.exit"

note "./build/c+ build $SRC_REL -std=c89 --sanitize=none (expected failure)"
./build/c+ build "$SRC_REL" -std=c89 --sanitize=none -o "$A/neg_c89_cplus" \
    > "$A/neg_c89_cplus.stdout" 2> "$A/neg_c89_cplus.stderr"
echo $? > "$A/neg_c89_cplus.exit"

note "./build/c+ build $SRC_REL -std=bogus --sanitize=none (expected failure)"
./build/c+ build "$SRC_REL" -std=bogus --sanitize=none -o "$A/neg_bogus_cplus" \
    > "$A/neg_bogus_cplus.stdout" 2> "$A/neg_bogus_cplus.stderr"
echo $? > "$A/neg_bogus_cplus.exit"

# ---------------------------------------------------------------------------
# 3. Real downloaded projects via the C+ driver with -std=c17.
#    Byte-identical copies are used because the driver writes temp files next
#    to its input; external_projects/ stays untouched.
# ---------------------------------------------------------------------------
printf 'SECTION 3: real projects via C+ --legacy-c -std=c17\n' >> "$cmd_log"
cmp external_projects/game_a/original/snake.c "$A/game_a_snake.c"
echo $? > "$A/copy_game_a.exit"
cmp external_projects/game_b/original/2048.c "$A/game_b_2048.c"
echo $? > "$A/copy_game_b.exit"
cmp external_projects/cli_app/original/xdump.c "$A/cli_app_xdump.c"
echo $? > "$A/copy_cli_app.exit"
cmp external_projects/cli_app/build/fixtures/sample.bin "$A/sample.bin"
echo $? > "$A/copy_fixture.exit"

for p in game_a_snake game_b_2048 cli_app_xdump; do
    # Document the C+ Safe-mode outcome too (safety diagnostics, not C17).
    note "./build/c+ build $A_REL/$p.c -std=c17 --sanitize=none (Safe mode)"
    ./build/c+ build "$A_REL/$p.c" -std=c17 --sanitize=none \
        -o "$A/safe_$p" > "$A/safe_$p.build.stdout" 2> "$A/safe_$p.build.stderr"
    echo $? > "$A/safe_$p.build.exit"

    note "./build/c+ build --legacy-c $A_REL/$p.c -std=c17 --sanitize=none -o $A_REL/$p"
    ./build/c+ build --legacy-c "$A_REL/$p.c" -std=c17 --sanitize=none \
        -o "$A/$p" > "$A/real_$p.build.stdout" 2> "$A/real_$p.build.stderr"
    echo $? > "$A/real_$p.build.exit"
    grep '^  -std=' "$A/real_$p.build.stderr" > /dev/null 2>&1 || true
done
# Effective std of the legacy-c path (verbose).
note "./build/c+ build --legacy-c $A_REL/game_b_2048.c -std=c17 --sanitize=none --verbose"
./build/c+ build --legacy-c "$A_REL/game_b_2048.c" -std=c17 --sanitize=none --verbose \
    -o "$A/verbose_legacy" > "$A/real_legacy.verbose.stdout" 2> "$A/real_legacy.verbose.stderr"
grep '^  -std=' "$A/real_legacy.verbose.stdout" > "$A/real_legacy.effective_std" 2>/dev/null || true

cmp_file="$A/comparisons.txt"
: > "$cmp_file"
compare() { # label, produced-file, recorded-original-file
    if cmp -s "$2" "$3"; then
        printf 'MATCH\t%s\t%s\t%s\n' "$1" "$2" "$3" >> "$cmp_file"
    else
        printf 'DIFFER\t%s\t%s\t%s\n' "$1" "$2" "$3" >> "$cmp_file"
    fi
}

# game_a: deterministic single 'q' quit.
note "printf 'q' | timeout 30 $A_REL/game_a_snake"
printf 'q' | timeout 30 "$A/game_a_snake" > "$A/real_game_a.stdout" 2> "$A/real_game_a.stderr"
echo $? > "$A/real_game_a.exit"
compare game_a_stdout "$A/real_game_a.stdout" external_projects/game_a/build/run.stdout

# game_b: deterministic self-test / version / help modes.
for case in test version help; do
    case "$case" in
        test) flag=test ;;
        version) flag=--version ;;
        help) flag=--help ;;
    esac
    note "timeout 30 $A_REL/game_b_2048 $flag"
    timeout 30 "$A/game_b_2048" "$flag" > "$A/real_game_b_$case.stdout" 2> "$A/real_game_b_$case.stderr"
    echo $? > "$A/real_game_b_$case.exit"
    compare "game_b_$case" "$A/real_game_b_$case.stdout" "external_projects/game_b/run_${case}_stdout.log"
done

# cli_app: deterministic cases. bad_flag is run as './build/xdump' so that
# argv[0] in getopt's diagnostic matches the recorded original exactly.
note "timeout 30 $A_REL/cli_app_xdump -v | -h | file cases"
timeout 30 "$A/cli_app_xdump" -v > "$A/real_xdump_version.stdout" 2> "$A/real_xdump_version.stderr"
echo $? > "$A/real_xdump_version.exit"
compare xdump_version "$A/real_xdump_version.stdout" external_projects/cli_app/build/runs/version.stdout

timeout 30 "$A/cli_app_xdump" -h > "$A/real_xdump_help.stdout" 2> "$A/real_xdump_help.stderr"
echo $? > "$A/real_xdump_help.exit"
compare xdump_help "$A/real_xdump_help.stdout" external_projects/cli_app/build/runs/help.stdout

timeout 30 "$A/cli_app_xdump" "$A/sample.bin" > "$A/real_xdump_file_default.stdout" 2> "$A/real_xdump_file_default.stderr"
echo $? > "$A/real_xdump_file_default.exit"
compare xdump_file_default "$A/real_xdump_file_default.stdout" external_projects/cli_app/build/runs/file_default.stdout

timeout 30 "$A/cli_app_xdump" -w 8 -g 4 "$A/sample.bin" > "$A/real_xdump_file_width8.stdout" 2> "$A/real_xdump_file_width8.stderr"
echo $? > "$A/real_xdump_file_width8.exit"
compare xdump_file_width8 "$A/real_xdump_file_width8.stdout" external_projects/cli_app/build/runs/file_width8.stdout

timeout 30 "$A/cli_app_xdump" -s 32 -n 16 "$A/sample.bin" > "$A/real_xdump_file_skip_n.stdout" 2> "$A/real_xdump_file_skip_n.stderr"
echo $? > "$A/real_xdump_file_skip_n.exit"
compare xdump_file_skip_n "$A/real_xdump_file_skip_n.stdout" external_projects/cli_app/build/runs/file_skip_n.stdout

timeout 30 "$A/cli_app_xdump" /definitely/not/a/file > "$A/real_xdump_missing_file.stdout" 2> "$A/real_xdump_missing_file.stderr"
echo $? > "$A/real_xdump_missing_file.exit"
compare xdump_missing_file "$A/real_xdump_missing_file.stderr" external_projects/cli_app/build/runs/missing_file.stderr

( cd "$A" && timeout 30 ./build/xdump -Z > xdump_bad_flag.stdout 2> xdump_bad_flag.stderr )
echo $? > "$A/real_xdump_bad_flag.exit"
compare xdump_bad_flag "$A/xdump_bad_flag.stderr" external_projects/cli_app/build/runs/bad_flag.stderr

# ---------------------------------------------------------------------------
# 4. Machine-readable summary
# ---------------------------------------------------------------------------
tsv="$A/results.tsv"
: > "$tsv"
printf 'spelling\treal_std_flags\tclang_exit\tclang_stderr_bytes\tcplus_exit\tcplus_stderr_bytes\tcplus_run_exit\tcplus_run_matches_expected\tclassification\n' >> "$tsv"
for s in $SPELLINGS; do
    k=$(slug "$s")
    ce=$(cat "$A/clang_$k.exit")
    cb=$(wc -c < "$A/clang_$k.stderr" | tr -d ' ')
    pe=$(cat "$A/cplus_$k.exit")
    pb=$(wc -c < "$A/cplus_$k.stderr" | tr -d ' ')
    re=$(cat "$A/cplus_$k.run.exit" 2>/dev/null || echo 99)
    if [ -f "$A/cplus_$k.run.stdout" ] && [ "$(cat "$A/cplus_$k.run.stdout")" = "$EXPECTED" ]; then
        rm=YES
    else
        rm=NO
    fi
    flags=$(tr '\n' ' ' < "$A/cplus_$k.effective_std" 2>/dev/null | sed 's/  */ /g')
    if [ "$ce" = 0 ] && [ "$cb" = 0 ] && [ "$pe" = 0 ] && [ "$pb" = 0 ] && [ "$re" = 0 ] && [ "$rm" = YES ]; then
        class=SUPPORTED
    else
        class=NOT_FULLY_SUPPORTED
    fi
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
        "$s" "$flags" "$ce" "$cb" "$pe" "$pb" "$re" "$rm" "$class" >> "$tsv"
done

printf '\n=== results.tsv ===\n'
cat "$tsv"
printf '\n=== comparisons.txt ===\n'
cat "$cmp_file"
printf '\nRaw logs: %s/\n' "$A_REL"
