#!/bin/sh
# Aggregate runner for every modern Nuklear/gfx GUI group under apps/legacy_examples/modern_gui/.
#
#   sh apps/legacy_examples/modern_gui/run_all.sh
#
# Discovery: every run.sh at depth 2 (<group>/run.sh) or depth 3
# (<group>/<subgroup>/run.sh) below apps/legacy_examples/modern_gui/ is a runner.  Each is
# invoked from the repository root under `timeout -k 10 300` with a restricted
# PATH and CPLUS_CLANG pinned to /usr/bin/clang.  Nothing from the caller's
# PATH leaks into the child (except the directories that actually provide the
# required tools, discovered BEFORE the PATH is narrowed).  On this host
# /opt/homebrew/bin is added only because it provides `pkg-config` for SDL3;
# CPLUS_CLANG is pinned to /usr/bin/clang explicitly, so Homebrew clang cannot
# shadow the backend.  SDL_VIDEODRIVER=dummy is forced so every Nuklear/SDL3
# frame is produced against the dummy video backend.
#
# Authoritative output: the LAST line emitted by a runner that begins with one
# of these four tokens (the "final report line"):
#
#   GUI_DASHBOARD  GUI_SETTINGS  GFX_BREAKOUT  GFX_INVADERS
#
# The exact line is captured verbatim, printed in the table, and saved to
# build/modern_gui/final_lines.txt.  Per-runner stdout+stderr is kept at
# build/modern_gui/<runner>.log (overwritten on every run).
#
# Status rules (a "real failure" makes this runner exit 1):
#   * child exit status != 0                              -> FAIL_EXIT
#   * no final report line at all                         -> NO_REPORT
#   * final line starts with an unrecognised token        -> BAD_TOKEN
#   * final line contains FAIL / MISMATCH / ERROR /
#     BUILD_FAIL, or fail=N / mismatched=N / errors=N with
#     N > 0, or status=FAIL                               -> FAIL
#   * final line contains BLOCKED / blocked=N (N > 0) but
#     no failure signal above                             -> BLOCKED (reported,
#     NOT a hard failure, matching the repository evidence convention)
#   * otherwise                                           -> PASS
#   * one of the four expected tokens never appeared       -> PENDING (hard
#     failure: a group has not landed or did not report)
#
# This runner NEVER masks a failing subgroup with a success override, and
# ALWAYS continues to the next runner after a failure.  It uses no Python and
# no `|| true`.  It does not rebuild examples itself; each group's run.sh owns
# its own build and evidence.
#
# Exit status:
#   0 = every discovered runner reported PASS (BLOCKED rows are allowed);
#   1 = at least one real failure (FAIL/FAIL_EXIT/NO_REPORT/BAD_TOKEN/PENDING);
#   2 = no runners discovered, or timeout/clang unavailable (cannot run).
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 1
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd) || exit 1
cd "$ROOT" || exit 1

# Resolve tool directories BEFORE narrowing PATH.  Only the directories that
# actually provide these tools are added back.  pkg-config may add
# /opt/homebrew/bin, but CPLUS_CLANG stays pinned to /usr/bin/clang.
EXTRA_PATH=
for tool in c+ rustc perl awk pkg-config; do
    _b=$(command -v "$tool" 2>/dev/null) || continue
    [ -n "$_b" ] || continue
    _d=$(CDPATH= cd -- "$(dirname -- "$_b")" && pwd) || continue
    case ":$_d:" in
        :/usr/bin:|:/bin:|:/usr/sbin:|:/sbin:) continue ;;
        *) ;;
    esac
    case ":$EXTRA_PATH:" in
        *":$_d:"*) ;;
        *) EXTRA_PATH="${EXTRA_PATH:+$EXTRA_PATH:}$_d" ;;
    esac
done

RESTRICTED_PATH=/usr/bin:/bin:/usr/sbin:/sbin
if [ -n "$EXTRA_PATH" ]; then
    RESTRICTED_PATH="$RESTRICTED_PATH:$EXTRA_PATH"
fi
PATH=$RESTRICTED_PATH
export PATH
CPLUS_CLANG=/usr/bin/clang
export CPLUS_CLANG
SDL_VIDEODRIVER=dummy
export SDL_VIDEODRIVER
LC_ALL=C
export LC_ALL

TIMEOUT=timeout
KILL_AFTER=10
TIMEOUT_SECONDS=300

OUT="$ROOT/build/modern_gui"
if ! mkdir -p "$OUT"; then
    echo "gui-agg: error: cannot create $OUT" >&2
    exit 2
fi
if ! command -v "$TIMEOUT" >/dev/null 2>&1; then
    echo "gui-agg: error: 'timeout' not found; refusing unbounded run" >&2
    exit 2
fi
if [ ! -x "$CPLUS_CLANG" ]; then
    echo "gui-agg: error: '$CPLUS_CLANG' not found" >&2
    exit 2
fi

TOKENS='GUI_DASHBOARD GUI_SETTINGS GFX_BREAKOUT GFX_INVADERS'

# Discover <group>/run.sh (depth 2) and <group>/<subgroup>/run.sh (depth 3).
RUNNERS=$(find "$HERE" -mindepth 2 -maxdepth 3 -type f -name run.sh 2>/dev/null | sort)
if [ -z "$RUNNERS" ]; then
    echo "gui-agg: no <group>/run.sh or <group>/*/run.sh found under $HERE" >&2
    echo "gui-agg: zero runners is NOT a pass" >&2
    exit 2
fi

FINAL_LINES="$OUT/final_lines.txt"
: > "$FINAL_LINES"

printf '\n'
printf 'modern-gui aggregate: timeout=%ss (kill after %ss), PATH=%s, CPLUS_CLANG=%s, SDL_VIDEODRIVER=%s\n' \
    "$TIMEOUT_SECONDS" "$KILL_AFTER" "$RESTRICTED_PATH" "$CPLUS_CLANG" "$SDL_VIDEODRIVER"
printf '\n'
printf '%-22s %-16s %-5s %-12s %s\n' \
    'RUNNER' 'TOKEN' 'EXIT' 'STATUS' 'FINAL LINE (verbatim)'
printf '%-22s %-16s %-5s %-12s %s\n' \
    '------' '-----' '----' '------' '------------------'

hard_fail=0
runner_count=0
reported_count=0
pass_count=0
blocked_count=0
fail_count=0
seen_tokens=' '

# classify <exit-code> <final-line>
# Reads the final line on stdin (never through `awk -v`, which would mangle
# backslashes).  Prints one status token on stdout.
classify() {
    _ec=$1
    printf '%s\n' "$2" | awk -v ec="$_ec" '
        BEGIN { status = "PASS" }
        { line = $0 }
        END {
            if (ec != 0) { print "FAIL_EXIT"; exit }
            if (line == "") { print "NO_REPORT"; exit }
            n = split(line, f, /[[:space:]]+/)
            fail = 0
            block = 0
            # Positional contract used by the group runners:
            #   TOKEN <total> <passed> <failed>
            if (n >= 4 && f[2] ~ /^[0-9]+$/ && f[3] ~ /^[0-9]+$/ && f[4] ~ /^[0-9]+$/) {
                if (f[4] + 0 > 0) fail = 1
            }
            for (i = 1; i <= n; i++) {
                t = f[i]
                if (t == "FAIL" || t == "FAILED" || t == "MISMATCH" ||
                    t == "ERROR" || t == "BUILD_FAIL" || t == "BUILD_FAILURE") fail = 1
                if (t == "BLOCKED" || t == "BLOCK") block = 1
                if (t ~ /^(fail|failed|mismatched|error|errors|build_fail|build_failures)=[0-9]+$/) {
                    split(t, kv, "=")
                    if (kv[2] + 0 > 0) fail = 1
                }
                if (t ~ /^blocked=[0-9]+$/) {
                    split(t, kv, "=")
                    if (kv[2] + 0 > 0) block = 1
                }
                if (t ~ /^status=/) {
                    split(t, kv, "=")
                    if (kv[2] == "FAIL" || kv[2] == "FAILED" ||
                        kv[2] == "ERROR" || kv[2] == "MISMATCH") fail = 1
                }
            }
            if (fail) { print "FAIL"; exit }
            if (block) { print "BLOCKED"; exit }
            print "PASS"
        }
    '
}

for _runner in $RUNNERS; do
    _label=${_runner#"$HERE"/}
    _label=${_label%/run.sh}
    _safe=$(printf '%s' "$_label" | tr '/' '_')
    _log="$OUT/$_safe.log"

    runner_count=$((runner_count + 1))

    _rc=0
    if [ -x "$_runner" ]; then
        ( cd "$ROOT" && "$TIMEOUT" -k "$KILL_AFTER" "$TIMEOUT_SECONDS" "$_runner" ) \
            > "$_log" 2>&1
        _rc=$?
    else
        ( cd "$ROOT" && "$TIMEOUT" -k "$KILL_AFTER" "$TIMEOUT_SECONDS" sh "$_runner" ) \
            > "$_log" 2>&1
        _rc=$?
    fi

    # Last line starting with one of the four recognised tokens, verbatim.
    _final=$(awk '/^(GUI_DASHBOARD|GUI_SETTINGS|GFX_BREAKOUT|GFX_INVADERS)([[:space:]]|$)/ { last = $0 } END { if (last != "") print last }' "$_log")

    _token='-'
    case "$_final" in
        GUI_DASHBOARD*) _token=GUI_DASHBOARD ;;
        GUI_SETTINGS*)  _token=GUI_SETTINGS ;;
        GFX_BREAKOUT*)  _token=GFX_BREAKOUT ;;
        GFX_INVADERS*)  _token=GFX_INVADERS ;;
        *) _token=BAD_TOKEN ;;
    esac

    _status=$(classify "$_rc" "$_final")
    if [ "$_token" = BAD_TOKEN ]; then
        _status=BAD_TOKEN
    fi

    # Coverage counts a token as "reported" even when that runner failed; a
    # PENDING row is reserved for a token that never appeared at all.
    if [ "$_token" != BAD_TOKEN ]; then
        case "$seen_tokens" in
            *" $_token "*) ;;
            *) seen_tokens="$seen_tokens$_token " ;;
        esac
    fi
    if [ -n "$_final" ]; then
        reported_count=$((reported_count + 1))
        printf '%s\n' "$_final" >> "$FINAL_LINES"
    fi

    printf '%-22s %-16s %-5s %-12s %s\n' \
        "$_label" "$_token" "$_rc" "$_status" "$_final"

    case "$_status" in
        PASS)
            pass_count=$((pass_count + 1))
            ;;
        BLOCKED)
            blocked_count=$((blocked_count + 1))
            echo "gui-agg: $_label: BLOCKED (reported, not a hard failure): $_final" >&2
            ;;
        *)
            fail_count=$((fail_count + 1))
            hard_fail=1
            echo "gui-agg: $_label: $_status (rc=$_rc): $_final" >&2
            if [ -z "$_final" ]; then
                tail -n 5 "$_log" | sed 's/^/  | /' >&2
            fi
            ;;
    esac
done

# Coverage: every expected token must have appeared.  A missing token means a
# group has not landed or did not report; report it as PENDING, never fabricate
# a passing row, and fail the aggregate.
for _t in $TOKENS; do
    case "$seen_tokens" in
        *" $_t "*) continue ;;
        *) ;;
    esac
    printf '%-22s %-16s %-5s %-12s %s\n' \
        "(missing:$_t)" "$_t" '-' 'PENDING' \
        "PENDING - no runner reported $_t"
    echo "gui-agg: PENDING $_t - rerun after that group's run.sh lands" >&2
    hard_fail=1
done

printf '\n'
printf 'gui-agg: runners=%s reported=%s pass=%s blocked=%s fail=%s hard_fail=%s\n' \
    "$runner_count" "$reported_count" "$pass_count" "$blocked_count" "$fail_count" "$hard_fail"
printf 'gui-agg: expected tokens: %s\n' "$TOKENS"
printf 'gui-agg: logs: %s/<runner>.log ; final lines: %s\n' "$OUT" "$FINAL_LINES"

if [ "$hard_fail" -ne 0 ]; then
    exit 1
fi
exit 0
