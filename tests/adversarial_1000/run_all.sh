#!/bin/sh
# Aggregate runner for the 1000-scenario adversarial campaign.
#
#   sh tests/adversarial_1000/run_all.sh
#
# Discovery is dynamic: every immediate subdirectory of this directory that
# contains a NOTES.md is a "group".  Its runner is one of the conventional
# names below (or, as a last resort, the only *.sh file in that directory).
# Each runner is executed from the repository root under a hard 600 s bound,
# with a fixed minimal PATH and CPLUS_CLANG pinned to /usr/bin/clang.  The
# authoritative result is the LAST stdout line beginning with "ADV1000 ", in
# the form:
#
#   ADV1000 <group> <scenarios> <refused> <integrity_ok> <failures>
#
# where <integrity_ok> is either the NUMBER of scenarios whose integrity check
# passed, or a literal canary_ok / state_ok / sentinel_ok token.  A line with
# two counts, e.g. "... <canary_total> <state_total> <failures>", is accepted;
# every count must equal <scenarios> and the last field is the failure count.
#
# Exit status: 0 = every discovered group completed with scenarios=1000,
#                 failures=0 and an *_ok integrity token, and its runner
#                 exited 0;
#             1 = at least one real failure (bad runner rc, missing/malformed
#                 final line, wrong scenario count, non-zero failures, bad
#                 integrity token, or a NOTES.md group with no runner);
#             2 = no groups discovered (campaign not present yet).
#
# The runner NEVER uses `|| true` and NEVER suppresses a failing subgroup:
# it records each failure, continues to the next group, and returns 1 at the
# end.  Full per-group stdout/stderr and the exact final line are kept under
# build/adversarial_1000/ for audit.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd) || exit 1
cd "$ROOT" || exit 1

# Fixed, minimal environment.  /opt/homebrew/bin and /usr/local/bin are
# included only so that pkg-config (needed by the JSON/HTTP groups) resolves;
# nothing from TMPDIR, the user's PATH or the invoking shell leaks in.
RESTRICTED_PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin
PATH=$RESTRICTED_PATH
export PATH
CPLUS_CLANG=/usr/bin/clang
export CPLUS_CLANG
LC_ALL=C
export LC_ALL

TIMEOUT_SECONDS=600
KILL_AFTER=10

OUT="$ROOT/build/adversarial_1000"
mkdir -p "$OUT" || exit 1

if ! command -v timeout >/dev/null 2>&1; then
    echo "adv1000-agg: error: 'timeout' utility not found; refusing unbounded run" >&2
    exit 1
fi

# Return the runner path for a group directory, or "" when none exists.
find_runner() {
    _dir=$1
    for _cand in run.sh runner.sh run_all.sh run_1000.sh run_1000_scenarios.sh; do
        if [ -f "$_dir/$_cand" ]; then
            printf '%s' "$_dir/$_cand"
            return 0
        fi
    done
    # Fallback: the single shell script in the directory.  Only accept when
    # there is exactly one, so we never run an arbitrary helper by mistake.
    _count=0
    _found=
    for _shf in "$_dir"/*.sh; do
        [ -f "$_shf" ] || continue
        _count=$((_count + 1))
        _found=$_shf
    done
    if [ "$_count" -eq 1 ]; then
        printf '%s' "$_found"
    fi
    return 0
}

is_uint() {
    case "$1" in
        ''|*[!0-9]*) return 1 ;;
        *) return 0 ;;
    esac
}

# Discover groups: immediate subdirectories containing NOTES.md.
groups=
group_count=0
for _notes in "$HERE"/*/NOTES.md; do
    [ -f "$_notes" ] || continue
    _dir=$(dirname "$_notes")
    _group=$(basename "$_dir")
    groups="$groups $_group"
    group_count=$((group_count + 1))
done

if [ "$group_count" -eq 0 ]; then
    echo "adv1000-agg: no <group>/NOTES.md found under $HERE" >&2
    echo "adv1000-agg: campaign is incomplete; zero groups is NOT a pass" >&2
    exit 2
fi

hard_fail=0
total_scenarios=0
total_refused=0
total_failures=0

printf '\n'
printf 'adversarial-1000 aggregate: %s group(s), timeout=%ss, PATH=%s, CPLUS_CLANG=%s\n' \
    "$group_count" "$TIMEOUT_SECONDS" "$RESTRICTED_PATH" "$CPLUS_CLANG"
printf '\n'
printf '%-12s %10s %9s %14s %9s\n' \
    'group' 'scenarios' 'refused' 'integrity_ok' 'failures'
printf '%-12s %10s %9s %14s %9s\n' \
    '------------' '----------' '---------' '--------------' '---------'

for _group in $groups; do
    _dir="$HERE/$_group"
    _runner=$(find_runner "$_dir")

    if [ -z "$_runner" ]; then
        printf '%-12s %10s %9s %14s %9s\n' \
            "$_group" 'n/a' 'n/a' 'NO_RUNNER' 'n/a'
        echo "adv1000-agg: $_group: NOTES.md present but no runner found" >&2
        hard_fail=1
        continue
    fi

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

    # The authoritative line is the last line containing a well-formed
    # ADV1000 <group> <scenarios> record.  Some runners print it with a prefix
    # (e.g. json_log prints "final=ADV1000 ..."), so strip any prefix while
    # preserving the exact canonical record.  A prose line such as
    # "ADV1000 json_log run summary" is rejected by the numeric third field.
    _line=$(grep -E 'ADV1000 [A-Za-z_]+ [0-9]+ ' "$_log" | tail -n 1)
    case "$_line" in
        *ADV1000\ *) _line="ADV1000${_line##*ADV1000}" ;;
    esac
    printf '%s\n' "$_line" >"$_final"

    if [ -z "$_line" ]; then
        printf '%-12s %10s %9s %14s %9s\n' \
            "$_group" 'n/a' 'n/a' 'NO_FINAL_LINE' 'n/a'
        echo "adv1000-agg: $_group: runner rc=$_rc, no ADV1000 final line (tail below)" >&2
        tail -n 5 "$_log" | sed 's/^/  | /' >&2
        hard_fail=1
        continue
    fi

    set -- $_line
    if [ "$#" -lt 6 ] || [ "$1" != 'ADV1000' ] \
            || ! is_uint "$3" || ! is_uint "$4"; then
        printf '%-12s %10s %9s %14s %9s\n' \
            "$_group" 'n/a' 'n/a' 'MALFORMED' 'n/a'
        echo "adv1000-agg: $_group: malformed final line: $_line" >&2
        hard_fail=1
        continue
    fi

    _scenarios=$3
    _refused=$4
    shift 4

    # The fields between "refused" and the trailing failure count are the
    # integrity_ok fields.  Two encodings are in use across the groups:
    #   * a COUNT of scenarios whose integrity check passed (numeric), e.g.
    #     "1000"; some groups emit two counts (canary_total/state_total),
    #     which are displayed joined by '/'.  Every count must equal scenarios.
    #   * a literal token canary_ok / state_ok / sentinel_ok, which asserts
    #     integrity directly (no count).
    # Any other token is malformed and is a real failure.
    _integrity_ok=yes
    _integrity_display=
    _malformed=no
    while [ "$#" -gt 1 ]; do
        _ic=$1
        if is_uint "$_ic"; then
            if [ "$_ic" -ne "$_scenarios" ]; then
                _integrity_ok=no
            fi
        else
            case "$_ic" in
                *_ok|*_OK) : ;;
                *) _malformed=yes; break ;;
            esac
        fi
        if [ -n "$_integrity_display" ]; then
            _integrity_display="$_integrity_display/"
        fi
        _integrity_display="$_integrity_display$_ic"
        shift
    done
    _failures=$1
    if [ "$_malformed" = yes ] || ! is_uint "$_failures"; then
        printf '%-12s %10s %9s %14s %9s\n' \
            "$_group" 'n/a' 'n/a' 'MALFORMED' 'n/a'
        echo "adv1000-agg: $_group: malformed final line: $_line" >&2
        hard_fail=1
        continue
    fi

    total_scenarios=$((total_scenarios + _scenarios))
    total_refused=$((total_refused + _refused))
    total_failures=$((total_failures + _failures))

    _ok=yes
    if [ "$_rc" -ne 0 ]; then
        echo "adv1000-agg: $_group: runner rc=$_rc" >&2
        _ok=no
    fi
    if [ "$_scenarios" -ne 1000 ]; then
        echo "adv1000-agg: $_group: scenarios=$_scenarios (expected 1000)" >&2
        _ok=no
    fi
    if [ "$_integrity_ok" = no ]; then
        echo "adv1000-agg: $_group: integrity counts '$_integrity_display' != $_scenarios" >&2
        _ok=no
    fi
    if [ "$_failures" -ne 0 ]; then
        echo "adv1000-agg: $_group: failures=$_failures" >&2
        _ok=no
    fi

    if [ "$_ok" = no ]; then
        hard_fail=1
    fi
    if [ "$_integrity_ok" = no ]; then
        _shown_integrity="${_integrity_display}!"
    else
        _shown_integrity=$_integrity_display
    fi
    printf '%-12s %10s %9s %14s %9s\n' \
        "$_group" "$_scenarios" "$_refused" "$_shown_integrity" "$_failures"
done

printf '%-12s %10s %9s %14s %9s\n' \
    'TOTAL' "$total_scenarios" "$total_refused" '-' "$total_failures"

printf '\n'
printf 'adv1000-agg: groups=%s scenarios=%s (target 9000) refused=%s failures=%s hard_fail=%s\n' \
    "$group_count" "$total_scenarios" "$total_refused" "$total_failures" "$hard_fail"
printf 'adv1000-agg: logs: %s/<group>.log ; final lines: %s/<group>.final\n' \
    "$OUT" "$OUT"

if [ "$hard_fail" -ne 0 ]; then
    exit 1
fi
exit 0
