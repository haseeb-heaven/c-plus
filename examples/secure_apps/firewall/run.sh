#!/bin/sh
# Build and run the deterministic input-validation firewall scenarios.
#
# A scenario passes only when:
#   * firewall.cp builds in --strict --sanitize=none mode from the repo root,
#   * the run completes under `timeout 30`,
#   * the exact expected verdict line is present (OK <sanitized> or
#     REFUSED <rule> <code>),
#   * a CONTRAST line is present showing what the unchecked C path would do,
#   * a second identical run produces byte-identical stdout (determinism).
#
# For a refused scenario the CONTRAST line must additionally carry
# `unchecked=accept` or `unchecked=corrupts`: runtime evidence that the naive
# path would have let the very same token through or overrun its buffer while
# the checked layer refused it.
#
# Final line: SECURE_FIREWALL <scenarios> <passed> <failed> <refused>
#   scenarios = 19 fixed scenarios (8 accepted + 11 hostile)
#   passed    = accepted scenarios that passed every check
#   refused   = hostile scenarios correctly refused
#   failed    = failed scenarios/checks
#   invariant: scenarios = passed + refused + failed
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
DIR=examples/secure_apps/firewall
BIN=$DIR/bin
mkdir -p "$BIN"

# Fixed, generated inputs (no clock, no randomness).
BIG=
i=0
while [ "$i" -lt 300 ]; do
    BIG="${BIG}A"
    i=$((i + 1))
done
TAB=$(printf '\t')

scenarios=0
passed=0
refused=0
failed=0

# run_to <outfile> <cmd...>; prints the exit code, output goes to <outfile>.
run_to() {
    out=$1
    shift
    timeout 30 "$@" >"$out" 2>&1
    echo $?
}

finish() {
    echo "SECURE_FIREWALL $scenarios $passed $failed $refused"
    if [ "$scenarios" -eq 19 ] && [ "$passed" -eq 8 ] &&
       [ "$refused" -eq 11 ] && [ "$failed" -eq 0 ]; then
        exit 0
    fi
    exit 1
}

# ---- build ---------------------------------------------------------------
buildlog=$BIN/build.log
if ! "$CPLUS" build "$DIR/firewall.cp" --strict --sanitize=none -I. \
        -o "$BIN/firewall" >"$buildlog" 2>&1; then
    echo "BUILD FAIL (see $buildlog)"
    cat "$buildlog"
    failed=$((failed + 19))
    finish
fi
echo "BUILD ok firewall"

# ---- helpers -------------------------------------------------------------
# check_ok <name> <policy> <input> <expected-sanitized>
check_ok() {
    name=$1
    policy=$2
    input=$3
    want=$4
    scenarios=$((scenarios + 1))
    out=$BIN/$name.out
    rep=$BIN/$name.repeat.out
    ok=1

    rc=$(run_to "$out" "$BIN/firewall" "$policy" "$input")
    if [ "$rc" -ne 0 ]; then
        echo "OK FAIL $name exit=$rc (want 0)"
        ok=0
    fi
    if ! grep -q '^CONTRAST ' "$out"; then
        echo "OK FAIL $name missing CONTRAST line"
        ok=0
    fi
    if ! grep -F -x -q "OK $want" "$out"; then
        echo "OK FAIL $name missing 'OK $want'"
        ok=0
    fi
    if ! grep -q '^FIREWALL verdict=OK ' "$out"; then
        echo "OK FAIL $name missing FIREWALL verdict=OK"
        ok=0
    fi
    rc2=$(run_to "$rep" "$BIN/firewall" "$policy" "$input")
    if [ "$rc2" -ne 0 ] || ! cmp -s "$out" "$rep"; then
        echo "OK FAIL $name non-deterministic output"
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        echo "OK pass $name policy=$policy input=<$input> -> $want"
        passed=$((passed + 1))
    else
        failed=$((failed + 1))
    fi
}

# check_refuse <name> <policy> <input> <expected-rule> <expected-code>
check_refuse() {
    name=$1
    policy=$2
    input=$3
    rule=$4
    code=$5
    scenarios=$((scenarios + 1))
    out=$BIN/$name.out
    rep=$BIN/$name.repeat.out
    ok=1

    rc=$(run_to "$out" "$BIN/firewall" "$policy" "$input")
    if [ "$rc" -ne 2 ]; then
        echo "REFUSE FAIL $name exit=$rc (want 2)"
        ok=0
    fi
    if ! grep -F -x -q "REFUSED $rule $code" "$out"; then
        echo "REFUSE FAIL $name missing 'REFUSED $rule $code'"
        ok=0
    fi
    # Runtime evidence: the unchecked path would accept or corrupt.
    if ! grep -q 'unchecked=accept\|unchecked=corrupts' "$out"; then
        echo "REFUSE FAIL $name missing unchecked-path evidence"
        ok=0
    fi
    if ! grep -q '^CONTRAST ' "$out"; then
        echo "REFUSE FAIL $name missing CONTRAST line"
        ok=0
    fi
    if ! grep -q "^FIREWALL verdict=REFUSED rule=$rule code=$code " "$out"; then
        echo "REFUSE FAIL $name missing matching FIREWALL verdict"
        ok=0
    fi
    rc2=$(run_to "$rep" "$BIN/firewall" "$policy" "$input")
    if [ "$rc2" -ne 2 ] || ! cmp -s "$out" "$rep"; then
        echo "REFUSE FAIL $name non-deterministic output"
        ok=0
    fi

    if [ "$ok" -eq 1 ]; then
        echo "REFUSE pass $name policy=$policy -> $rule $code (rc=$rc)"
        refused=$((refused + 1))
    else
        failed=$((failed + 1))
    fi
}

# ---- accepted scenarios (8) ---------------------------------------------
check_ok ok01 token alpha 'alpha'
check_ok ok02 token report 'report'
check_ok ok03 pathseg notes_2024 'notes_2024'
check_ok ok04 pathseg data-1 'data-1'
check_ok ok05 sqlsafe alice 'alice'
check_ok ok06 sqlsafe 'john doe' 'john doe'
check_ok ok07 text 'hello world' 'hello world'
check_ok ok08 text '  padded  ' 'padded'

# ---- hostile scenarios (11) ---------------------------------------------
check_refuse bad01 pathseg '../etc/passwd' no-traversal E_TRAVERSAL
check_refuse bad02 pathseg '..\windows' no-traversal E_TRAVERSAL
check_refuse bad03 pathseg 'report%00.txt' no-nul E_NUL
check_refuse bad04 token 'safe\0evil' no-nul E_NUL
check_refuse bad05 text "$BIG" max-len E_SIZE
check_refuse bad06 text "line${TAB}tabs" no-control E_CONTROL
check_refuse bad07 sqlsafe "' OR 1=1" no-sqli E_SQLI
check_refuse bad08 sqlsafe "admin'--" no-sqli E_SQLI
check_refuse bad09 token unknown allowlist E_NOTALLOWED
check_refuse bad10 pathseg 'bad/name' pattern E_NOTALLOWED
check_refuse bad11 token '' no-empty E_EMPTY

finish
