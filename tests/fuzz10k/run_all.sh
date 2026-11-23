#!/bin/sh
# Aggregate runner for the 10,000-scenario-per-group fuzz campaign.
#
#   sh tests/fuzz10k/run_all.sh
#
# Discovery is dynamic: every immediate subdirectory of this directory that
# contains a run.sh is a "group".  Each runner is executed from the repository
# root under a hard `timeout -k 10 900` bound, with a fixed minimal PATH and
# CPLUS_CLANG pinned to /usr/bin/clang.  The authoritative result is the LAST
# stdout line beginning with the canonical 7-field record:
#
#   FUZZ10K <group> <scenarios> <crashes> <hangs> <corruptions> <refusals>
#
# The five numeric fields are all unsigned integers.  "refusals" are expected
# negative-control outcomes and do NOT fail a group; crashes, hangs and
# corruptions are real failures.  Auxiliary trailer lines (FUZZ10K_DIGEST,
# FUZZ10K_DISTINCT) and prose lines such as "FUZZ10K <group> HANG detected ..."
# are rejected because their third field is not numeric.  A runner that prints
# a prefix (e.g. "final=FUZZ10K ...") is handled by stripping up to the last
# FUZZ10K token while preserving the exact canonical record.
#
# Exit status: 0 = every discovered group completed with a well-formed final
#                 line, runner rc 0, scenarios >= 10000, and
#                 crashes = hangs = corruptions = 0;
#             1 = at least one real failure (nonzero runner rc, timeout,
#                 missing/malformed final line, scenarios < 10000, or a real
#                 crash / hang / corruption count);
#             2 = no groups discovered (campaign not present yet) -- explicitly
#                 NOT a pass.
#
# The runner never masks a failing subgroup: it records each failure, continues
# to the next group, and returns 1 at the end. No failure is ever negated away.
# Full per-group stdout/stderr and the exact final line are kept under
# build/fuzz10k/ for audit (gitignored via build/).

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd) || exit 1
cd "$ROOT" || exit 1

# Fixed, minimal environment.  /opt/homebrew/bin and /usr/local/bin are included
# only so pkg-config (needed by a few groups) resolves; nothing from TMPDIR, the
# user's PATH or the invoking shell leaks in.
RESTRICTED_PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin
PATH=$RESTRICTED_PATH
export PATH
CPLUS_CLANG=/usr/bin/clang
export CPLUS_CLANG
LC_ALL=C
export LC_ALL

TIMEOUT_SECONDS=900
KILL_AFTER=10
MIN_SCENARIOS=10000

OUT="$ROOT/build/fuzz10k"
mkdir -p "$OUT" || exit 1

if ! command -v timeout >/dev/null 2>&1; then
    echo "fuzz10k-agg: error: 'timeout' utility not found; refusing unbounded run" >&2
    exit 1
fi

is_uint() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
        *) return 0 ;;
    esac
}

# Discover groups: immediate subdirectories containing a run.sh.
groups=
group_count=0
for _runner in "$HERE"/*/run.sh; do
    [ -f "$_runner" ] || continue
    _dir=$(dirname "$_runner")
    _group=$(basename "$_dir")
    groups="$groups $_group"
    group_count=$((group_count + 1))
done

if [ "$group_count" -eq 0 ]; then
    echo "fuzz10k-agg: no <group>/run.sh found under $HERE" >&2
    echo "fuzz10k-agg: campaign is incomplete; zero groups is NOT a pass" >&2
    exit 2
fi

hard_fail=0
total_scenarios=0
total_crashes=0
total_hangs=0
total_corruptions=0
total_refusals=0

printf '\n'
printf 'fuzz10k aggregate: %s group(s), timeout=%ss, PATH=%s, CPLUS_CLANG=%s\n' \
    "$group_count" "$TIMEOUT_SECONDS" "$RESTRICTED_PATH" "$CPLUS_CLANG"
printf '\n'
printf '%-12s %10s %8s %6s %11s %9s %s\n' \
    'group' 'scenarios' 'crashes' 'hangs' 'corruptions' 'refusals' 'status'
printf '%-12s %10s %8s %6s %11s %9s %s\n' \
    '------------' '----------' '--------' '------' '-----------' '---------' '------'

for _group in $groups; do
    _dir="$HERE/$_group"
    _runner="$_dir/run.sh"

    _log="$OUT/${_group}.log"
    _final="$OUT/${_group}.final"
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

    # Authoritative line = last well-formed canonical FUZZ10K record.  Strip a
    # possible "key=" prefix while preserving the exact record.
    _line=$(grep -E 'FUZZ10K [A-Za-z_]+ [0-9]+ ' "$_log" | tail -n 1)
    case "$_line" in
        *FUZZ10K\ *) _line="FUZZ10K${_line##*FUZZ10K}" ;;
    esac
    printf '%s\n' "$_line" >"$_final"

    if [ -z "$_line" ]; then
        printf '%-12s %10s %8s %6s %11s %9s %s\n' \
            "$_group" 'n/a' 'n/a' 'n/a' 'n/a' 'n/a' 'NO_FINAL_LINE'
        echo "fuzz10k-agg: $_group: runner rc=$_rc, no FUZZ10K final line (tail below)" >&2
        tail -n 5 "$_log" | sed 's/^/  | /' >&2
        hard_fail=1
        continue
    fi

    set -- $_line
    if [ "$#" -ne 7 ] || [ "$1" != 'FUZZ10K' ] \
            || ! is_uint "$3" || ! is_uint "$4" || ! is_uint "$5" \
            || ! is_uint "$6" || ! is_uint "$7"; then
        printf '%-12s %10s %8s %6s %11s %9s %s\n' \
            "$_group" 'n/a' 'n/a' 'n/a' 'n/a' 'n/a' 'MALFORMED'
        echo "fuzz10k-agg: $_group: malformed final line: $_line" >&2
        hard_fail=1
        continue
    fi

    _scenarios=$3
    _crashes=$4
    _hangs=$5
    _corruptions=$6
    _refusals=$7

    total_scenarios=$((total_scenarios + _scenarios))
    total_crashes=$((total_crashes + _crashes))
    total_hangs=$((total_hangs + _hangs))
    total_corruptions=$((total_corruptions + _corruptions))
    total_refusals=$((total_refusals + _refusals))

    _status=
    if [ "$_rc" -ne 0 ]; then
        case "$_rc" in
            124|137) _status='TIMEOUT' ;;
            *)       _status="RC=$_rc" ;;
        esac
        echo "fuzz10k-agg: $_group: runner rc=$_rc" >&2
        hard_fail=1
    fi
    if [ "$_crashes" -ne 0 ]; then
        _status="${_status:+${_status}+}CRASH"
        echo "fuzz10k-agg: $_group: crashes=$_crashes" >&2
        hard_fail=1
    fi
    if [ "$_hangs" -ne 0 ]; then
        _status="${_status:+${_status}+}HANG"
        echo "fuzz10k-agg: $_group: hangs=$_hangs" >&2
        hard_fail=1
    fi
    if [ "$_corruptions" -ne 0 ]; then
        _status="${_status:+${_status}+}CORRUPT"
        echo "fuzz10k-agg: $_group: corruptions=$_corruptions" >&2
        hard_fail=1
    fi
    if [ "$_scenarios" -lt "$MIN_SCENARIOS" ]; then
        _status="${_status:+${_status}+}LOW_N"
        echo "fuzz10k-agg: $_group: scenarios=$_scenarios (expected >= $MIN_SCENARIOS)" >&2
        hard_fail=1
    fi
    if [ -z "$_status" ]; then
        _status='PASS'
    fi

    printf '%-12s %10s %8s %6s %11s %9s %s\n' \
        "$_group" "$_scenarios" "$_crashes" "$_hangs" "$_corruptions" \
        "$_refusals" "$_status"
done

printf '%-12s %10s %8s %6s %11s %9s %s\n' \
    'TOTAL' "$total_scenarios" "$total_crashes" "$total_hangs" \
    "$total_corruptions" "$total_refusals" "-"

printf '\n'
printf 'fuzz10k-agg: groups=%s scenarios=%s (target >=%s) crashes=%s hangs=%s corruptions=%s refusals=%s hard_fail=%s\n' \
    "$group_count" "$total_scenarios" "$((group_count * MIN_SCENARIOS))" \
    "$total_crashes" "$total_hangs" "$total_corruptions" "$total_refusals" \
    "$hard_fail"
printf 'fuzz10k-agg: logs: %s/<group>.log ; final lines: %s/<group>.final\n' \
    "$OUT" "$OUT"

if [ "$hard_fail" -ne 0 ]; then
    exit 1
fi
exit 0
