#!/bin/sh
# Deterministic runtime verification for apps/legacy_examples/secure_apps/vault/vault.cp.
#
# Builds once under --strict --sanitize=none -I., then runs a fixed set of
# argv-driven scenarios. Each individual program run is bounded by `timeout 30`.
# A scenario passes only when its exit status matches, every expected literal
# output line is present, every forbidden literal (in particular a secret that
# must not be logged) is absent, and the summary canary reads "ok".
#
# Final line: SECURE_VAULT <scenarios> <passed> <failed> <refused>
#   refused = total number of "VAULT refuse" lines emitted across all scenario
#             outputs (a runtime count, not a predicted number).
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
DIR=apps/legacy_examples/secure_apps/vault
BIN=$DIR/.run
mkdir -p "$BIN"

scenarios=0
passed=0
failed=0

# Build exactly as documented; no diagnostics are tolerated on stderr.
if ! "$CPLUS" build "$DIR/vault.cp" --strict --sanitize=none -I. \
        -o "$BIN/vault" >"$BIN/build.log" 2>&1; then
    echo "BUILD FAIL (see $BIN/build.log)"
    echo "SECURE_VAULT 0 0 1 0"
    exit 1
fi
echo "BUILD ok vault"

# Run one case under a 30-second bound; echo its exit code to stdout.
run_case() {
    rc_out=$1
    shift
    timeout 30 "$@" >"$rc_out" 2>&1
    echo $?
}

# Assertion helpers: first argument is the captured output file.
expect() {
    grep -F -q -- "$2" "$1" || { echo "  missing: $2"; return 1; }
    return 0
}
forbid() {
    if grep -F -q -- "$2" "$1"; then
        echo "  LEAK/unexpected: $2"
        return 1
    fi
    return 0
}
count_expect() {
    ce_got=$(grep -F -c -- "$3" "$1")
    if [ "$ce_got" -ne "$2" ]; then
        echo "  count want=$2 got=$ce_got for: $3"
        return 1
    fi
    return 0
}

# Generic runner: NAME EXPECTED_RC [argv ...]. Calls check_<NAME> for the
# scenario-specific assertions. Stops on nothing; accumulates pass/fail.
run_scenario() {
    rs_name=$1
    rs_want=$2
    shift 2
    scenarios=$((scenarios + 1))
    rs_out=$BIN/$rs_name.out
    rs_rc=$(run_case "$rs_out" "$BIN/vault" "$@")
    rs_ok=1
    if [ "$rs_rc" -ne "$rs_want" ]; then
        echo "FAIL $rs_name rc want=$rs_want got=$rs_rc"
        rs_ok=0
    fi
    if ! expect "$rs_out" "VAULT summary"; then rs_ok=0; fi
    if ! expect "$rs_out" "canary ok"; then rs_ok=0; fi
    # POSIX sh function names cannot contain '-'; scenario names can.
    rs_fn=$(printf '%s' "$rs_name" | tr '-' '_')
    if ! "check_$rs_fn" "$rs_out"; then rs_ok=0; fi
    if [ "$rs_ok" -eq 1 ]; then
        echo "PASS $rs_name"
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
}

# repeat_char <char> <count>: deterministic long-token generator.
repeat_char() {
    rp_c=$1
    rp_n=$2
    rp_s=""
    rp_i=0
    while [ "$rp_i" -lt "$rp_n" ]; do
        rp_s="$rp_s$rp_c"
        rp_i=$((rp_i + 1))
    done
    printf '%s' "$rp_s"
}

# ---- scenario-specific assertions ---------------------------------------

check_happy() {
    expect "$1" "VAULT ok add alpha" &&
    expect "$1" "VAULT ok add beta" &&
    expect "$1" "VAULT get alpha S3cr3t-A" &&
    expect "$1" "VAULT get beta S3cr3t-B" &&
    expect "$1" "VAULT list 2" &&
    expect "$1" "VAULT entry 0 alpha" &&
    expect "$1" "VAULT entry 1 beta" &&
    expect "$1" "VAULT peek 0 alpha" &&
    expect "$1" "VAULT ok delete alpha" &&
    expect "$1" "VAULT list 1" &&
    expect "$1" "VAULT audit bytes 155 cap 512" &&
    expect "$1" "VAULT summary ops 9 refused 0 audit-bytes 155 canary ok" &&
    forbid "$1" "VAULT refuse"
}

check_duplicate() {
    expect "$1" "VAULT ok add alpha" &&
    count_expect "$1" 1 "VAULT ok add alpha" &&
    expect "$1" "VAULT refuse duplicate-service code 108 cat invalid" &&
    forbid "$1" "S3cr3t-B" &&
    expect "$1" "refused 1"
}

check_empty_secret() {
    expect "$1" "VAULT refuse empty-secret code 108 cat invalid" &&
    expect "$1" "audit-bytes 31" &&
    forbid "$1" "VAULT ok add"
}

check_empty_name() {
    expect "$1" "VAULT refuse empty-name code 108 cat invalid" &&
    forbid "$1" "VAULT ok add"
}

check_oversized_secret() {
    expect "$1" "VAULT refuse oversized-secret code 101 cat range" &&
    forbid "$1" "$(repeat_char X 33)" &&
    forbid "$1" "VAULT ok add"
}

check_oversized_name() {
    expect "$1" "VAULT refuse oversized-name code 101 cat range" &&
    forbid "$1" "$(repeat_char N 17)" &&
    forbid "$1" "VAULT ok add"
}

check_missing() {
    count_expect "$1" 2 "VAULT refuse missing-service code 108 cat invalid" &&
    forbid "$1" "VAULT ok"
}

check_index() {
    expect "$1" "VAULT ok add alpha" &&
    count_expect "$1" 3 "VAULT refuse index-out-of-range code 101 cat range" &&
    forbid "$1" "S3cr3t-A"
}

check_debug() {
    expect "$1" "VAULT ok add alpha" &&
    expect "$1" "VAULT refuse secret-logging-forbidden code 108 cat invalid" &&
    forbid "$1" "S3cr3t-A"
}

check_usage() {
    expect "$1" "VAULT refuse usage code 108 cat invalid" &&
    expect "$1" "VAULT summary ops 0 refused 1 audit-bytes 0 canary ok"
}

check_unknown() {
    expect "$1" "VAULT refuse unknown-command code 108 cat invalid" &&
    expect "$1" "audit-bytes 0"
}

check_audit_overflow() {
    expect "$1" "VAULT refuse audit-overflow code 107 cat memory" &&
    count_expect "$1" 2 "VAULT refuse audit-overflow code 107 cat memory" &&
    expect "$1" "VAULT audit bytes 496 cap 512" &&
    count_expect "$1" 16 "VAULT audit-line" &&
    expect "$1" "VAULT summary ops 19 refused 2 audit-bytes 496 canary ok" &&
    forbid "$1" "OVERFLOW-SECRET" &&
    forbid "$1" "VAULT ok add c9"
}

# ---- scenarios -----------------------------------------------------------

run_scenario happy 0 \
    add alpha S3cr3t-A -- add beta S3cr3t-B -- get alpha -- list -- peek 0 \
    -- delete alpha -- list -- get beta -- audit

run_scenario duplicate 1 \
    add alpha S3cr3t-A -- add alpha S3cr3t-B

run_scenario empty-secret 1 add gamma ""

run_scenario empty-name 1 add "" sekrit

LONG_SECRET=$(repeat_char X 33)
run_scenario oversized-secret 1 add delta "$LONG_SECRET"

LONG_NAME=$(repeat_char N 17)
run_scenario oversized-name 1 add "$LONG_NAME" sekrit

run_scenario missing 1 get nope -- delete nope

run_scenario index 1 add alpha S3cr3t-A -- peek 5 -- peek -1 -- peek x

run_scenario debug 1 add alpha S3cr3t-A -- debug alpha

run_scenario usage 2

run_scenario unknown 1 frobnicate

# Audit-overflow: nine add/delete cycles (18 audited ops fill 16 records),
# then two more audited ops must be refused instead of silently dropped.
set --
cycle=1
while [ "$cycle" -le 9 ]; do
    set -- "$@" add "c$cycle" OVERFLOW-SECRET -- delete "c$cycle"
    if [ "$cycle" -lt 9 ]; then
        set -- "$@" --
    fi
    cycle=$((cycle + 1))
done
run_scenario audit-overflow 1 "$@" -- audit

# refused is a runtime count over every captured transcript.
refused=$(cat "$BIN"/*.out | grep -c '^VAULT refuse')

echo "SECURE_VAULT $scenarios $passed $failed $refused"
[ "$failed" -eq 0 ]
