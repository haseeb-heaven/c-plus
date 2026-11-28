#!/bin/sh
# Shared helpers for the expanded-examples adversarial drivers.
# Sourced (not executed) by cli.sh / games.sh / database.sh / engine.sh /
# safety.sh.  All artifacts (binaries, logs, fixtures) stay under this dir.
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd) || exit 1
cd "$ROOT" || exit 1

# Output locations are overridable so run_all.sh can give each concurrent
# invocation a private scratch tree. Direct category-script use keeps the
# canonical paths below.
BIN=${ADV_BIN:-$HERE/bin}
LOGS=${ADV_LOGS:-$HERE/logs}
FIX=${ADV_FIX:-$HERE/fixtures}
mkdir -p "$BIN" "$LOGS" "$FIX" || exit 1

CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
CPLUS_CLANG=${CPLUS_CLANG:-/usr/bin/clang}
export CPLUS_CLANG
LC_ALL=C
export LC_ALL

# Per-process counters (each category script prints its own subtotal).
ADV_CASES=0
ADV_REFUSED=0
ADV_CRASHED=0
ADV_UNEXPECTED_OK=0
ADV_BLOCKED=0

# ---------------------------------------------------------------------------
# Deterministic hostile fixtures, generated once, byte-exact.
# ---------------------------------------------------------------------------
make_fixtures() {
    python3 - <<'PY' > "$FIX/oversized_10k.txt"
import sys
sys.stdout.write('A' * 10000)
PY
    python3 - <<'PY' > "$FIX/oversized_10k_digits.txt"
import sys
sys.stdout.write('1' * 10000)
PY
    : > "$FIX/empty.txt"
    printf '   \t  \n' > "$FIX/whitespace.txt"
    # A truncated record file: a valid line, a header-only line, then a
    # line whose title field is cut short mid-record.
    printf '1|0|alpha\n2|0\n3|0|gam' > "$FIX/truncated.db"
    # Binary-ish hostile bytes (invalid UTF-8, no NUL).
    printf '\377\376\375' > "$FIX/invalid_utf8.txt"
    # Fixed-size file fixtures for file_tools (FT_CAP = 1024).
    python3 -c 'import sys; sys.stdout.write("A" * 1024)' > "$FIX/exact_1k.bin"
    python3 -c 'import sys; sys.stdout.write("A" * 1025)' > "$FIX/one_over_1k.bin"
    printf 'alpha beta\ngamma delta\n' > "$FIX/sample.txt"
}

# ---------------------------------------------------------------------------
# build_one <name> <repo-relative-source> [extra c+ args...]
# Builds into $BIN/<name>. Logs to $LOGS/<name>.build.log.
# ---------------------------------------------------------------------------
build_one() {
    _name=$1
    _src=$2
    shift 2
    _log="$LOGS/$_name.build.log"
    if timeout 120 "$CPLUS" build "$ROOT/$_src" --strict --sanitize=none \
        -I"$ROOT" -o "$BIN/$_name" "$@" >"$_log" 2>&1; then
        return 0
    fi
    echo "ADV_BUILD_FAIL $_name (see $_log)" >&2
    ADV_BLOCKED=$((ADV_BLOCKED + 1))
    return 1
}

# ---------------------------------------------------------------------------
# run_case <example> <case> <expect> <binary> [args...]
#
#   expect = REFUSE  : hostile input goes through the example's input path and
#                      the example's own contract requires a refusal.
#            SURVIVE : example has no hostile-input path (self-contained demo)
#                      or hostile input is explicitly ignored; the contract is
#                      "no crash + the example's refusal/sentinel token".
#
# Optional environment knobs:
#   ADV_STDIN_FILE      feed this fixture on stdin (default: /dev/null)
#   ADV_REQUIRE_TOKEN   literal string that must also appear (sentinel check)
#   ADV_SURVIVE_TOKEN   literal string proving a SURVIVE case handled the
#                       hostile scenario (default: REFUSED|REFUSE)
#
# Prints: ADV_EXPANDED <example> <case> REFUSED|CRASHED|UNEXPECTED_OK <exit>
# ---------------------------------------------------------------------------
run_case() {
    _example=$1
    _case=$2
    _expect=$3
    _bin=$4
    shift 4

    _out="$LOGS/$_example.$_case.out"
    _stdin=${ADV_STDIN_FILE:-/dev/null}
    timeout 120 "$_bin" "$@" <"$_stdin" >"$_out" 2>&1
    _rc=$?

    ADV_CASES=$((ADV_CASES + 1))
    _tag=
    # 124 = timeout, 137 = killed after timeout, >=128 = signal.
    if [ "$_rc" -ge 128 ] || [ "$_rc" -eq 124 ] || [ "$_rc" -eq 137 ]; then
        _tag=CRASHED
    elif [ "$_rc" -ne 0 ]; then
        _tag=REFUSED
    elif grep -Eqi 'REFUSED|REFUSE|CALC ERROR|TODO error|BAD_INPUT|usage:|(^| )FAIL( |$)' "$_out"; then
        _tag=REFUSED
    elif [ "$_expect" = SURVIVE ]; then
        _stok=${ADV_SURVIVE_TOKEN:-REFUS}
        if grep -q "$_stok" "$_out"; then
            _tag=REFUSED
        fi
    fi

    # Sentinel / secondary-token check. A refusal that also corrupts state is
    # not an acceptable refusal; downgrade it to UNEXPECTED_OK.
    if [ "$_tag" = REFUSED ] && [ -n "${ADV_REQUIRE_TOKEN:-}" ] &&
       ! grep -q "$ADV_REQUIRE_TOKEN" "$_out"; then
        _tag=UNEXPECTED_OK
    fi

    [ -n "$_tag" ] || _tag=UNEXPECTED_OK

    printf 'ADV_EXPANDED %s %s %s %d\n' "$_example" "$_case" "$_tag" "$_rc"
    case "$_tag" in
        REFUSED)        ADV_REFUSED=$((ADV_REFUSED + 1)) ;;
        CRASHED)        ADV_CRASHED=$((ADV_CRASHED + 1)) ;;
        UNEXPECTED_OK)  ADV_UNEXPECTED_OK=$((ADV_UNEXPECTED_OK + 1)) ;;
    esac

    unset ADV_STDIN_FILE ADV_REQUIRE_TOKEN ADV_SURVIVE_TOKEN
    return 0
}

# ---------------------------------------------------------------------------
# subtotal <category>  -- prints the four-counter summary for a category.
# exit is nonzero iff any CRASHED or UNEXPECTED_OK was observed.
# ---------------------------------------------------------------------------
subtotal() {
    _category=$1
    printf 'ADV_EXPANDED_SUBTOTAL %s cases=%d refused=%d crashed=%d unexpected_ok=%d blocked=%d\n' \
        "$_category" "$ADV_CASES" "$ADV_REFUSED" "$ADV_CRASHED" \
        "$ADV_UNEXPECTED_OK" "$ADV_BLOCKED"
    [ "$ADV_CRASHED" -eq 0 ] && [ "$ADV_UNEXPECTED_OK" -eq 0 ]
}
