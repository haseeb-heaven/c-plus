#!/bin/sh
# P1 "Cross-translation-unit analysis" reproducer driver.
#
# Invokes the repository compiler (build/c+) with `check` and `build` on each
# individual translation unit, plus explicit multi-TU `check` and link
# attempts.  Every command, its exact stdout/stderr, and its exit code are
# captured to results/<step>.log and echoed to stdout.
#
# Driver limitation (see NOTES.md): `c+ check/producer.cp consumer.cp` iterates
# TUs independently with a fresh DiagnosticEngine per TU and clears external
# class state between them.  It does not perform cross-TU semantic analysis;
# the only thing shared between TUs here is the C backend/linker at `build`.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../../../.." && pwd)
CPLUS_BIN=${CPLUS_BIN:-"$ROOT/build/c+"}
RESULTS="$HERE/results"
# Pin header resolution to THIS checkout.  Without CPLUS_ROOT the driver walks
# up from its compiled-in source dir and picks ../c-plus-core/stdlib/string.hp,
# whose types are named String/StringView; the reproducers below use the
# lowercase string/string_view API from this checkout's stdlib/string.hp.
CPLUS_ROOT=${CPLUS_ROOT:-"$ROOT"}
export CPLUS_ROOT
mkdir -p "$RESULTS"

run() {
    name=$1
    shift
    log="$RESULTS/$name.log"
    printf '$ %s\n' "$*" > "$log"
    "$@" >> "$log" 2>&1
    rc=$?
    printf '\n[exit code: %d]\n' "$rc" >> "$log"
    cat "$log"
    printf '\n'
}

cd "$HERE" || exit 1
printf 'compiler: %s\n' "$CPLUS_BIN"
printf 'cwd:      %s\n\n' "$HERE"

# Case A: borrowed string_view returned by a function in another TU.
run 01_check_owner_hp          "$CPLUS_BIN" check owner.hp
run 02_check_producer          "$CPLUS_BIN" check producer.cp
run 03_check_consumer          "$CPLUS_BIN" check consumer.cp
run 04_check_strict_producer   "$CPLUS_BIN" check --strict producer.cp
run 05_check_strict_consumer   "$CPLUS_BIN" check --strict consumer.cp
run 06_check_both_tus          "$CPLUS_BIN" check producer.cp consumer.cp
run 07_build_link_case_a       "$CPLUS_BIN" build producer.cp consumer.cp \
                                -o "$RESULTS/case_a"

# Case B: release in one TU, use through an extern pointer in another TU.
run 08_check_shared_hp         "$CPLUS_BIN" check shared.hp
run 09_check_dropper           "$CPLUS_BIN" check dropper.cp
run 10_check_user              "$CPLUS_BIN" check user.cp
run 11_check_strict_dropper    "$CPLUS_BIN" check --strict dropper.cp
run 12_check_strict_user       "$CPLUS_BIN" check --strict user.cp
run 13_check_both_tus_case_b   "$CPLUS_BIN" check dropper.cp user.cp
run 14_build_link_case_b       "$CPLUS_BIN" build dropper.cp user.cp \
                                -o "$RESULTS/case_b"
run 15_check_single_tu_control "$CPLUS_BIN" check single_tu_control.cp

# Case C: backend-only signal for a borrowed pointer to a stack local.
run 16_check_stack_producer    "$CPLUS_BIN" check stack_producer.cp
run 17_check_legacy_stack_prod "$CPLUS_BIN" check --legacy-c stack_producer.cp
run 18_check_stack_consumer    "$CPLUS_BIN" check stack_consumer.cp

printf 'All command outputs saved under %s\n' "$RESULTS"
