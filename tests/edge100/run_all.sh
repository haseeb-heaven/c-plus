#!/bin/sh
# Aggregate runner for the EDGE100 edge-case campaign.
#
#   sh tests/edge100/run_all.sh
#
# Runs every expected area runner (`tests/edge100/<area>/run.sh`) from the
# repository root under a hard per-area bound of 900 seconds, with a fixed
# minimal PATH and CPLUS_CLANG pinned to /usr/bin/clang.  The authoritative
# result for an area is the LAST stdout line of the form:
#
#   EDGE100 <area> <apis> <cases> <passed> <failed>
#
# where <apis> is the number of APIs enumerated for the area, <cases> the
# number of edge cases executed, <passed> the number that passed and <failed>
# the number that failed.  A runner may prefix the line (e.g. "final=EDGE100
# ..."); the canonical record is recovered by stripping everything before the
# last "EDGE100 ".  Prose lines without five numeric trailing fields are
# rejected.
#
# Exit status:
#   0 = every expected area was discovered, its runner exited 0, emitted a
#       well-formed EDGE100 line for the correct area, and reported failed=0;
#   1 = at least one real failure (missing runner, non-zero runner rc, missing
#       or malformed final line, area mismatch, or failed>0);
#   2 = no area runner at all was found (campaign not present yet).
#
# This runner never masks a failing subgroup with a succeeding fallback and
# never suppresses a failure: it records each failure, continues to the next
# area, and returns non-zero at the end.  Full per-area stdout/stderr and the
# exact canonical final line are retained under build/edge100/ for audit.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd) || exit 1
cd "$ROOT" || exit 1

# Fixed, minimal environment.  /opt/homebrew/bin and /usr/local/bin are
# included only so that pkg-config (needed by the HTTP/network and multimedia
# areas) resolves; nothing from TMPDIR, the user's PATH or the invoking shell
# leaks in.
RESTRICTED_PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin
PATH=$RESTRICTED_PATH
export PATH
CPLUS_CLANG=/usr/bin/clang
export CPLUS_CLANG
LC_ALL=C
export LC_ALL

TIMEOUT_SECONDS=900
KILL_AFTER=10

# The nine committed EDGE100 areas.  A missing one is a real coverage gap,
# never a silent skip.
AREAS="alloc containers io netthread safecrt multimedia adapters examples runtime"

OUT="$ROOT/build/edge100"
mkdir -p "$OUT" || exit 1

if ! command -v timeout >/dev/null 2>&1; then
    echo "edge100-agg: error: 'timeout' utility not found; refusing unbounded run" >&2
    exit 1
fi

is_uint() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
        *) return 0 ;;
    esac
}

printf '\n'
printf 'edge100 aggregate: timeout=%ss PATH=%s CPLUS_CLANG=%s\n' \
    "$TIMEOUT_SECONDS" "$RESTRICTED_PATH" "$CPLUS_CLANG"
printf '\n'
printf '%-12s %8s %8s %8s %8s %10s\n' \
    'area' 'apis' 'cases' 'passed' 'failed' 'status'
printf '%-12s %8s %8s %8s %8s %10s\n' \
    '------------' '--------' '--------' '--------' '--------' '----------'

hard_fail=0
found_count=0
total_apis=0
total_cases=0
total_passed=0
total_failed=0

for _area in $AREAS; do
    _dir="$HERE/$_area"
    _runner="$_dir/run.sh"

    if [ ! -f "$_runner" ]; then
        # RESULTS.md may exist without an executable runner yet; that is a
        # missing artifact, reported as PENDING and counted as a real failure.
        printf '%-12s %8s %8s %8s %8s %10s\n' \
            "$_area" 'n/a' 'n/a' 'n/a' 'n/a' 'PENDING'
        echo "edge100-agg: $_area: no run.sh found (PENDING)" >&2
        if [ -f "$_dir/RESULTS.md" ]; then
            echo "edge100-agg: $_area: RESULTS.md exists but run.sh is missing" >&2
        fi
        hard_fail=1
        continue
    fi
    found_count=$((found_count + 1))

    _log="$OUT/${_area}.log"
    _final="$OUT/${_area}.final"
    _rc=0
    if [ -x "$_runner" ]; then
        if ( cd "$ROOT" && timeout -k "$KILL_AFTER" "$TIMEOUT_SECONDS" "$_runner" ) \
                >"$_log" 2>&1; then
            _rc=0
        else
            _rc=$?
        fi
    else
        if ( cd "$ROOT" && timeout -k "$KILL_AFTER" "$TIMEOUT_SECONDS" sh "$_runner" ) \
                >"$_log" 2>&1; then
            _rc=0
        else
            _rc=$?
        fi
    fi

    # Authoritative line: last well-formed EDGE100 <area> record.  Strip any
    # prefix so "final=EDGE100 ..." still yields the canonical record.
    _line=$(grep -E 'EDGE100 [A-Za-z_]+ [0-9]+ [0-9]+ [0-9]+ [0-9]+' "$_log" | tail -n 1)
    case "$_line" in
        *EDGE100\ *) _line="EDGE100${_line##*EDGE100}" ;;
    esac
    printf '%s\n' "$_line" >"$_final"

    if [ -z "$_line" ]; then
        printf '%-12s %8s %8s %8s %8s %10s\n' \
            "$_area" 'n/a' 'n/a' 'n/a' 'n/a' 'NO_FINAL_LINE'
        echo "edge100-agg: $_area: runner rc=$_rc, no EDGE100 final line (tail below)" >&2
        tail -n 5 "$_log" | sed 's/^/  | /' >&2
        hard_fail=1
        continue
    fi

    set -- $_line
    if [ "$#" -ne 6 ] || [ "$1" != 'EDGE100' ] || [ "$2" != "$_area" ] \
            || ! is_uint "$3" || ! is_uint "$4" \
            || ! is_uint "$5" || ! is_uint "$6"; then
        printf '%-12s %8s %8s %8s %8s %10s\n' \
            "$_area" 'n/a' 'n/a' 'n/a' 'n/a' 'MALFORMED'
        echo "edge100-agg: $_area: malformed final line: $_line" >&2
        hard_fail=1
        continue
    fi

    _apis=$3
    _cases=$4
    _passed=$5
    _failed=$6

    total_apis=$((total_apis + _apis))
    total_cases=$((total_cases + _cases))
    total_passed=$((total_passed + _passed))
    total_failed=$((total_failed + _failed))

    _ok=yes
    if [ "$_rc" -ne 0 ]; then
        echo "edge100-agg: $_area: runner rc=$_rc" >&2
        _ok=no
    fi
    if [ "$_failed" -ne 0 ]; then
        echo "edge100-agg: $_area: failed=$_failed" >&2
        _ok=no
    fi
    if [ "$_passed" -gt "$_cases" ]; then
        echo "edge100-agg: $_area: passed=$_passed > cases=$_cases" >&2
        _ok=no
    fi
    if [ "$_ok" = no ]; then
        hard_fail=1
    fi

    if [ "$_ok" = yes ]; then
        _status=OK
    else
        _status=FAIL
    fi
    printf '%-12s %8s %8s %8s %8s %10s\n' \
        "$_area" "$_apis" "$_cases" "$_passed" "$_failed" "$_status"
done

printf '%-12s %8s %8s %8s %8s %10s\n' \
    'TOTAL' "$total_apis" "$total_cases" "$total_passed" "$total_failed" \
    "$([ "$hard_fail" -eq 0 ] && printf OK || printf FAIL)"

printf '\n'
printf 'edge100-agg: areas_found=%s/%s apis=%s cases=%s passed=%s failed=%s hard_fail=%s\n' \
    "$found_count" "$(printf '%s\n' $AREAS | wc -l | tr -d ' ')" \
    "$total_apis" "$total_cases" "$total_passed" "$total_failed" "$hard_fail"
printf 'edge100-agg: logs: %s/<area>.log ; final lines: %s/<area>.final\n' \
    "$OUT" "$OUT"

if [ "$found_count" -eq 0 ]; then
    echo "edge100-agg: no area runners found under $HERE" >&2
    echo "edge100-agg: campaign is incomplete; zero areas is NOT a pass" >&2
    exit 2
fi

if [ "$hard_fail" -ne 0 ]; then
    exit 1
fi
exit 0
