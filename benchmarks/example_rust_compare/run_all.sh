#!/bin/sh
# Aggregate runner for the 49 example-vs-Rust comparisons.
#
#   sh benchmarks/example_rust_compare/run_all.sh
#
# Discovery: every immediate subdirectory of this directory that contains a
# compare.sh is a "group".  Each group's compare.sh is run from the repository
# root under `timeout -k 10 300`, with a fixed minimal PATH and CPLUS_CLANG
# pinned to /usr/bin/clang.  The authoritative output is every stdout line of
# the form:
#
#   EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>
#
# (each group normally emits one line per example).  Those lines are parsed
# and printed as a table with columns: example / language / result / reason.
# The language column is derived from the original source extension:
#   .c  -> "C/Rust"    .cp -> "C+/Rust"
#
# Exit status:
#   0 = no DIFFERENT results, no build-failure BLOCKED results, and every
#       discovered group ran cleanly;
#   1 = at least one real DIFFERENT, a build failure, a runner that crashed /
#       timed out / emitted no result lines, or a malformed result line;
#   2 = no groups discovered (campaign not present yet).
#
# A BLOCKED result whose reason is NOT a build failure (for example an example
# that needs a TTY or network access) is reported but does NOT make this runner
# fail.  This runner NEVER uses `|| true` and NEVER suppresses a failing
# subgroup: it records each problem, continues to the next group, and returns
# a non-zero status at the end.  Per-group logs are kept under
# build/example_rust_compare/ for audit.

set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd) || exit 1
cd "$ROOT" || exit 1

# Locate rustc BEFORE narrowing PATH so its directory can be kept reachable;
# the group compare.sh scripts invoke `rustc` by bare name.
RUSTC_DIR=
if command -v rustc >/dev/null 2>&1; then
    _rustc_path=$(command -v rustc)
    RUSTC_DIR=$(dirname "$_rustc_path")
fi

# Fixed, minimal environment.  /usr/bin carries timeout, clang, the POSIX
# utilities and the system linker; RUSTC_DIR is added only when rustc was
# found on the invoking PATH.  Nothing from the caller's PATH leaks in.
RESTRICTED_PATH=/usr/bin:/bin:/usr/sbin:/sbin
if [ -n "$RUSTC_DIR" ]; then
    RESTRICTED_PATH="$RESTRICTED_PATH:$RUSTC_DIR"
fi
PATH=$RESTRICTED_PATH
export PATH
CPLUS_CLANG=/usr/bin/clang
export CPLUS_CLANG
LC_ALL=C
export LC_ALL

TIMEOUT_SECONDS=300
KILL_AFTER=10

OUT="$ROOT/build/example_rust_compare"
mkdir -p "$OUT" || exit 1

if ! command -v timeout >/dev/null 2>&1; then
    echo "example-compare-agg: error: 'timeout' not found; refusing unbounded run" >&2
    exit 1
fi

# Derive the language pair from the original source file extension.
resolve_language() {
    _name=$(basename -- "$1")
    _name=${_name%.c}
    _name=${_name%.cp}
    for _ext in c cp; do
        for _d in demos examples/crt examples/apps examples/system examples/games; do
            [ -f "$ROOT/$_d/$_name.$_ext" ] || continue
            if [ "$_ext" = cp ]; then
                printf 'C+/Rust'
            else
                printf 'C/Rust'
            fi
            return 0
        done
    done
    printf '?/Rust'
}

# True when a BLOCKED reason represents a real build/compile failure.
is_build_failure() {
    case "$1" in
        *[Bb]uild*fail*|*[Cc]ompil*|*build\ [Ee]rror*|*[Ee]rror*build*|*rustc*fail*|*clang*fail*)
            return 0 ;;
        *)
            return 1 ;;
    esac
}

# Discover groups: immediate subdirectories containing compare.sh.
groups=
group_count=0
for _cs in "$HERE"/*/compare.sh; do
    [ -f "$_cs" ] || continue
    _dir=$(dirname "$_cs")
    _group=$(basename "$_dir")
    groups="$groups $_group"
    group_count=$((group_count + 1))
done

if [ "$group_count" -eq 0 ]; then
    echo "example-compare-agg: no <group>/compare.sh found under $HERE" >&2
    echo "example-compare-agg: campaign is incomplete; zero groups is NOT a pass" >&2
    exit 2
fi

hard_fail=0
line_count=0
match_count=0
different_count=0
blocked_count=0

printf '\n'
printf 'example-rust-compare aggregate: %s group(s), timeout=%ss, PATH=%s, CPLUS_CLANG=%s\n' \
    "$group_count" "$TIMEOUT_SECONDS" "$RESTRICTED_PATH" "$CPLUS_CLANG"
printf '\n'
printf '%-24s %-10s %-10s %s\n' 'EXAMPLE' 'LANGUAGE' 'RESULT' 'REASON'
printf '%-24s %-10s %-10s %s\n' '-------' '--------' '------' '------'

for _group in $groups; do
    _dir="$HERE/$_group"
    _runner="$_dir/compare.sh"
    _log="$OUT/$_group.log"
    _lines="$OUT/$_group.lines"

    _rc=0
    if [ -x "$_runner" ]; then
        ( cd "$ROOT" && timeout -k "$KILL_AFTER" "$TIMEOUT_SECONDS" "$_runner" ) \
            >"$_log" 2>&1
        _rc=$?
    else
        ( cd "$ROOT" && timeout -k "$KILL_AFTER" "$TIMEOUT_SECONDS" sh "$_runner" ) \
            >"$_log" 2>&1
        _rc=$?
    fi

    if [ "$_rc" -ne 0 ]; then
        echo "example-compare-agg: $_group: runner rc=$_rc" >&2
        hard_fail=1
    fi

    # An empty file is still created when grep matches nothing (the shell
    # truncates via the redirection before grep runs), so the count is safe.
    if grep '^EXAMPLE_COMPARE ' "$_log" >"$_lines" 2>/dev/null; then
        :
    fi
    _nlines=$(wc -l < "$_lines" | tr -d ' ')
    if [ "$_nlines" -eq 0 ]; then
        echo "example-compare-agg: $_group: no EXAMPLE_COMPARE lines (protocol violation)" >&2
        tail -n 5 "$_log" | sed 's/^/  | /' >&2
        hard_fail=1
        continue
    fi

    while IFS= read -r _line; do
        case "$_line" in
            EXAMPLE_COMPARE\ *) ;;
            *) continue ;;
        esac
        # Keep the canonical record starting at the first EXAMPLE_COMPARE.
        # Disable globbing so '*' or '?' inside a reason is never expanded.
        _rec="EXAMPLE_COMPARE${_line##*EXAMPLE_COMPARE}"
        set -f
        set -- $_rec
        set +f
        if [ "$#" -lt 3 ]; then
            echo "example-compare-agg: $_group: malformed line: $_line" >&2
            hard_fail=1
            continue
        fi
        _name=$2
        _result=$3
        shift 3
        if [ "$#" -gt 0 ]; then
            _reason=$*
        else
            _reason='(no reason)'
        fi
        # Some groups emit an aggregate summary such as
        # "EXAMPLE_COMPARE TOTAL MATCH=9 DIFFERENT=0 BLOCKED=0".  Such a line
        # is not an example result: its result token contains '=' and/or its
        # name is TOTAL, so skip it silently.
        case "$_result" in
            *=*) continue ;;
        esac
        if [ "$_name" = TOTAL ]; then
            continue
        fi
        _lang=$(resolve_language "$_name")

        printf '%-24s %-10s %-10s %s\n' "$_name" "$_lang" "$_result" "$_reason"
        line_count=$((line_count + 1))

        # "SETUP" is not an example: it means a group's harness could not
        # initialise, so the comparison never actually ran.
        if [ "$_name" = SETUP ]; then
            echo "example-compare-agg: $_group: SETUP blocked: $_reason" >&2
            hard_fail=1
        fi

        case "$_result" in
            MATCH)
                match_count=$((match_count + 1))
                ;;
            DIFFERENT)
                different_count=$((different_count + 1))
                hard_fail=1
                ;;
            BLOCKED)
                blocked_count=$((blocked_count + 1))
                if is_build_failure "$_reason"; then
                    echo "example-compare-agg: $_group/$_name: build failure: $_reason" >&2
                    hard_fail=1
                fi
                ;;
            *)
                echo "example-compare-agg: $_group/$_name: unknown result token: $_result" >&2
                hard_fail=1
                ;;
        esac
    done < "$_lines"
done

printf '\n'
printf 'example-compare-agg: groups=%s examples=%s match=%s different=%s blocked=%s hard_fail=%s\n' \
    "$group_count" "$line_count" "$match_count" "$different_count" \
    "$blocked_count" "$hard_fail"
printf 'example-compare-agg: expected 49 examples; logs: %s/<group>.log\n' "$OUT"

if [ "$hard_fail" -ne 0 ]; then
    exit 1
fi
exit 0
