#!/bin/sh
# Aggregate runner for the Rust safe-library comparison examples.
#
# Discovers every benchmarks/rust_examples/<group>/compare.sh, runs each under
# `timeout 300` with a restricted PATH and CPLUS_CLANG=/usr/bin/clang, captures
# the final "RUST_EXAMPLES <group> <apis> <cases> <matched> <mismatched> <blocked>"
# line from each group, prints a table, and exits nonzero on any real mismatch
# or build/run failure. It continues after failures. BLOCKED cases are reported
# but are never counted as passes.
#
# Usage (from anywhere in the repo):
#   sh benchmarks/rust_examples/run_all.sh
#
# Optional environment overrides:
#   RUST_EXAMPLES_PATH    PATH used for child scripts (default: /usr/bin:/bin:$HOME/.cargo/bin)
#   RUST_EXAMPLES_LOGDIR  directory for per-group logs (default: a fresh mktemp -d)
#   TIMEOUT               timeout binary (default: timeout)
set -u

HERE=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
ROOT=$(CDPATH= cd -- "$HERE/../.." && pwd)
EXAMPLES="$HERE"

TIMEOUT=${TIMEOUT:-timeout}
CLANG=/usr/bin/clang                 # Apple clang 16, forced (not Homebrew clang)

# Build a restricted PATH: system dirs plus only the directories that actually
# provide the pinned toolchain binaries the child scripts need (rustc, python3,
# perl, timeout). This is deliberately much smaller than the caller's PATH and
# keeps Homebrew clang off the child's PATH so CPLUS_CLANG is authoritative.
EXTRA_PATH=""
for tool in rustc python3 python perl "$TIMEOUT"; do
    b=$(command -v "$tool" 2>/dev/null) || continue
    [ -n "$b" ] || continue
    d=$(CDPATH= cd -- "$(dirname -- "$b")" && pwd) || continue
    case ":$EXTRA_PATH:" in *":$d:"*) ;; *) EXTRA_PATH="${EXTRA_PATH:+$EXTRA_PATH:}$d" ;; esac
done
RESTRICTED_PATH=${RUST_EXAMPLES_PATH:-/usr/bin:/bin${EXTRA_PATH:+:$EXTRA_PATH}}
LOGDIR=${RUST_EXAMPLES_LOGDIR:-$(mktemp -d "${TMPDIR:-/tmp}/rust_examples.XXXXXX")}
mkdir -p "$LOGDIR" || { echo "cannot create log dir: $LOGDIR" >&2; exit 2; }

command -v "$TIMEOUT" >/dev/null 2>&1 || { echo "missing timeout: $TIMEOUT" >&2; exit 2; }
[ -x "$CLANG" ] || { echo "missing clang: $CLANG" >&2; exit 2; }
[ -d "$EXAMPLES" ] || { echo "missing examples dir: $EXAMPLES" >&2; exit 2; }

FINAL_LINES="$LOGDIR/final_lines.txt"
SUMMARY_TSV="$LOGDIR/summary.tsv"
: > "$FINAL_LINES"
: > "$SUMMARY_TSV"

echo "rust_examples aggregate runner"
echo "root:           $ROOT"
echo "examples:       $EXAMPLES"
echo "PATH (child):   $RESTRICTED_PATH"
echo "CPLUS_CLANG:    $CLANG"
echo "timeout:        $TIMEOUT 300"
echo "logs:           $LOGDIR"
echo

# Discover every <group>/compare.sh exactly two levels below the examples dir.
DISCOVERED=$(find "$EXAMPLES" -mindepth 2 -maxdepth 2 -type f -name compare.sh 2>/dev/null | sort)
if [ -z "$DISCOVERED" ]; then
    echo "NO compare.sh scripts discovered under $EXAMPLES" >&2
    exit 2
fi

run_group() {
    script=$1
    group=$(basename "$(dirname "$script")")
    log="$LOGDIR/$group.log"
    line="$LOGDIR/$group.final"

    # Restricted environment: deterministic PATH, Apple clang, and rustc via cargo bin.
    env PATH="$RESTRICTED_PATH" \
        CPLUS_CLANG="$CLANG" \
        CLANG="$CLANG" \
        "$TIMEOUT" 300 "$script" > "$log" 2>&1
    ec=$?

    # Extract the LAST exact RUST_EXAMPLES line (the script's final report).
    awk '/^RUST_EXAMPLES / { last = $0 } END { if (last != "") print last }' \
        "$log" > "$line"
    final=$(cat "$line")

    if [ -z "$final" ]; then
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$group" "-" "-" "-" "-" "-" "NO_REPORT" "$ec" >> "$SUMMARY_TSV"
        echo "[$group] NO RUST_EXAMPLES line (exit $ec) -- see $log" >&2
        return
    fi

    echo "$final" >> "$FINAL_LINES"
    # shellcheck disable=SC2086
    set -- $final
    nfields=$#
    lgroup=${2:-}; lapis=${3:-}; lcases=${4:-}; lmatched=${5:-}; lmismatched=${6:-}; lblocked=${7:-}

    status=PASS
    # The contract is exactly: RUST_EXAMPLES <group> <apis> <cases> <matched> <mismatched> <blocked>
    if [ "$nfields" -ne 7 ]; then
        status=BAD_REPORT
        echo "WARN: [$group] final line has $nfields fields; expected 7 -- $final" >&2
        [ -n "$lblocked" ] || lblocked="-"
    fi
    if [ "$lgroup" != "$group" ]; then
        status=GROUP_MISMATCH
    fi
    # A missing/extra-line case is handled above. Mismatch and nonzero exit are real failures.
    case "$lmismatched" in
        ''|*[!0-9]*) status=BAD_REPORT ;;
        *) [ "$lmismatched" -gt 0 ] 2>/dev/null && status=MISMATCH ;;
    esac
    if [ "$status" = PASS ] && [ "$ec" -ne 0 ]; then
        status=FAIL_EXIT
    fi
    # Blocks are reported but are explicitly NOT passes.
    if [ "$status" = PASS ] && [ "$lblocked" -gt 0 ] 2>/dev/null; then
        status=PASS_WITH_BLOCKS
    fi

    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
            "$group" "$lapis" "$lcases" "$lmatched" "$lmismatched" "$lblocked" "$status" "$ec" \
            >> "$SUMMARY_TSV"
    echo "[$group] $final (exit $ec, status $status)"
    # Informational only: groups differ on whether BLOCKED is counted inside
    # <cases>. Never treat this as a real mismatch (that is governed by the
    # <mismatched> field and the child's exit status).
    case "$lcases:$lmatched:$lmismatched:$lblocked" in
        *[!0-9:]* ) ;;
        *)
            part_sum=$((lmatched + lmismatched + lblocked))
            whole_sum=$((lmatched + lmismatched))
            if [ "$part_sum" -ne "$lcases" ] && [ "$whole_sum" -ne "$lcases" ]; then
                echo "WARN: [$group] final-line counts inconsistent: cases=$lcases but matched+mismatched+blocked=$part_sum (matched+mismatched=$whole_sum)" >&2
            fi
            ;;
    esac
}

echo "=== running groups (timeout 300 each) ==="
for script in $DISCOVERED; do
    run_group "$script"
done
echo

echo "=== captured final lines (verbatim) ==="
cat "$FINAL_LINES"
echo

echo "=== aggregate table ==="
printf '%-16s %5s %6s %8s %11s %8s %-14s %5s\n' \
    group apis cases matched mismatched blocked status exit
printf '%-16s %5s %6s %8s %11s %8s %-14s %5s\n' \
    ------ ---- ----- ------- ---------- ------- ------ ----

ta=0; tc=0; tm=0; tx=0; tb=0; tfail=0
while IFS="$(printf '\t')" read -r g a c m x b st ec; do
    [ -z "$g" ] && continue
    printf '%-16s %5s %6s %8s %11s %8s %-14s %5s\n' \
        "$g" "$a" "$c" "$m" "$x" "$b" "$st" "$ec"
    case "$a" in ''|*[!0-9]*) ;; *) ta=$((ta+a)) ;; esac
    case "$c" in ''|*[!0-9]*) ;; *) tc=$((tc+c)) ;; esac
    case "$m" in ''|*[!0-9]*) ;; *) tm=$((tm+m)) ;; esac
    case "$x" in ''|*[!0-9]*) ;; *) tx=$((tx+x)) ;; esac
    case "$b" in ''|*[!0-9]*) ;; *) tb=$((tb+b)) ;; esac
    case "$st" in PASS|PASS_WITH_BLOCKS) ;; *) tfail=$((tfail+1)) ;; esac
done < "$SUMMARY_TSV"
printf '%-16s %5s %6s %8s %11s %8s %-14s %5s\n' \
    TOTAL "$ta" "$tc" "$tm" "$tx" "$tb" "FAIL_GROUPS=$tfail" -

ngroups=$(printf '%s\n' "$DISCOVERED" | wc -l | tr -d ' ')
echo
echo "groups run: $ngroups | blocked (reported, NOT passes): $tb | real mismatches: $tx | failing groups: $tfail"
echo "note: PASS_WITH_BLOCKS is a partial result -- blocked cases are reported and are NOT counted as passes."
echo "full logs and raw output: $LOGDIR"

if [ "$tfail" -gt 0 ] || [ "$tx" -gt 0 ]; then
    echo "RESULT: FAIL" >&2
    exit 1
fi
echo "RESULT: PASS"
exit 0
