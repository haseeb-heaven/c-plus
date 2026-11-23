#!/usr/bin/env bash
# tests/abi/run_legacy_labs.sh
#
# Unified runner for the new ABI labs (Task 4).  It discovers, at run time,
# every lab under:
#
#   tests/abi/legacy_c
#   tests/abi/c_to_cp
#   tests/abi/cp_to_c
#   tests/abi/precompiled
#   tools/abi_compare
#
# and executes each under a bounded wall-clock timeout, capturing stdout,
# stderr and the exact exit code.  Every lab is classified as exactly one of:
#
#   PASS           ran and exited 0
#   FAIL           ran and exited nonzero
#   BUILD_FAILURE  the compile/build step exited nonzero
#   TIMEOUT        the bounded run exceeded its limit (rc 124 or 137)
#   SKIPPED        intentionally not run (self-reported or no runnable target)
#   BLOCKED        a required prerequisite is missing (compiler, build/c+, ...)
#
# Rules honoured:
#   * bounded timeouts on every build and every run;
#   * NO `|| true` masking - no nonzero exit is ever hidden;
#   * continue after every failure;
#   * full stdout/stderr of every lab kept verbatim under build/abi-labs/logs;
#   * machine-readable JSON under build/abi-labs/ (summary.json + results.jsonl);
#   * terminal table printed on stdout;
#   * exit nonzero when a *required* failure occurs (FAIL, BUILD_FAILURE,
#     TIMEOUT, or an empty/no-match selected group); SKIPPED/BLOCKED are visible
#     but do not by themselves flip the exit status;
#   * `--only NAME` and `--group NAME` filters (repeatable, comma-separated,
#     substring matching); `--list` shows discovery without running.
#
# Usage:
#   bash tests/abi/run_legacy_labs.sh [--only NAME]... [--group NAME]...
#   bash tests/abi/run_legacy_labs.sh --list
#   bash tests/abi/run_legacy_labs.sh --only lc01 --group legacy_c
#
# Environment overrides:
#   CPLUS_CLANG              host C compiler (default /usr/bin/clang)
#   ABI_LAB_BUILD_TIMEOUT    build timeout in seconds (default 300)
#   ABI_LAB_RUN_TIMEOUT      run timeout in seconds   (default 120)

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd) || exit 2
cd "$ROOT" || exit 2

OUT="$ROOT/build/abi-labs"
LOG_DIR="$OUT/logs"
ARTIFACT_DIR="$OUT/artifacts"
STATUS_DIR="$OUT/status"
SUMMARY_JSON="$OUT/summary.json"
RESULTS_JSONL="$OUT/results.jsonl"
COMMANDS_LOG="$OUT/commands.log"

# Deterministic toolchain, mirroring tests/run_all_evidence.sh.
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:/usr/local/bin:$PATH
export CPLUS_CLANG=${CPLUS_CLANG:-/usr/bin/clang}
CC="$CPLUS_CLANG"

BUILD_TIMEOUT=${ABI_LAB_BUILD_TIMEOUT:-300}
RUN_TIMEOUT=${ABI_LAB_RUN_TIMEOUT:-120}
KILL_AFTER=5

TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
    TIMEOUT_BIN=$(command -v timeout)
fi

BRANCH="unknown"
if command -v git >/dev/null 2>&1; then
    BRANCH=$(git -C "$ROOT" branch --show-current 2>/dev/null)
    [ -n "$BRANCH" ] || BRANCH="detached-or-unknown"
fi
STARTED_UTC=$(date -u '+%Y-%m-%dT%H:%M:%SZ')

# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------
ONLY=()
GROUP_FILTER=()
LIST_ONLY=0

# add_filter <only|group> <comma-separated-names>
add_filter() {
    local which=$1 value=$2 old_ifs=$IFS part
    old_ifs=$IFS
    IFS=','
    for part in $value; do
        part=${part#"${part%%[! ]*}"}
        part=${part%"${part##*[! ]}"}
        [ -n "$part" ] || continue
        if [ "$which" = "only" ]; then
            ONLY+=("$part")
        else
            GROUP_FILTER+=("$part")
        fi
    done
    IFS=$old_ifs
}

usage() {
    sed -n '2,40p' "$0" | sed 's/^# \{0,1\}//'
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --only)
            shift
            [ "$#" -gt 0 ] || { echo "error: --only needs a NAME" >&2; usage >&2; exit 2; }
            add_filter only "$1"
            shift
            ;;
        --only=*)
            add_filter only "${1#--only=}"
            shift
            ;;
        --group)
            shift
            [ "$#" -gt 0 ] || { echo "error: --group needs a NAME" >&2; usage >&2; exit 2; }
            add_filter group "$1"
            shift
            ;;
        --group=*)
            add_filter group "${1#--group=}"
            shift
            ;;
        --list)
            LIST_ONLY=1
            shift
            ;;
        --timeout)
            shift
            [ "$#" -gt 0 ] || { echo "error: --timeout needs SECONDS" >&2; exit 2; }
            RUN_TIMEOUT=$1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "error: unknown option: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

# Fresh evidence directory.   Only generated artifacts live here.
rm -rf "$OUT"
mkdir -p "$LOG_DIR" "$ARTIFACT_DIR" "$STATUS_DIR" || exit 2
: > "$RESULTS_JSONL" || exit 2
: > "$COMMANDS_LOG" || exit 2

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------
json_escape() {
    printf '%s' "$1" \
        | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/	/\\t/g' \
        | tr -d '\r\n'
}

safe_name() {
    printf '%s' "$1" | tr '/ .:' '____' | tr -cd 'A-Za-z0-9_-'
}

# Lab registry (parallel arrays).
declare -a LAB_NAME=() LAB_GROUP=() LAB_KIND=() LAB_TARGET=() LAB_CWD=() LAB_DESC=()
# Per-group discovery counts.
declare -a GROUP_NAMES=(legacy_c c_to_cp cp_to_c precompiled abi_compare)
declare -A GROUP_DISCOVERED=()
# Deduplicate by target path: the same script/source must never become two labs.
declare -A SEEN_TARGET=()

add_lab() {
    # add_lab <name> <group> <kind> <target> <cwd> <description>
    local key=$4
    if [ -n "${SEEN_TARGET[$key]:-}" ]; then
        return 0
    fi
    SEEN_TARGET[$key]=1
    LAB_NAME+=("$1")
    LAB_GROUP+=("$2")
    LAB_KIND+=("$3")
    LAB_TARGET+=("$4")
    LAB_CWD+=("$5")
    LAB_DESC+=("$6")
    GROUP_DISCOVERED[$2]=$(( ${GROUP_DISCOVERED[$2]:-0} + 1 ))
}

source_kind() {
    case "$1" in
        *.cp)  echo "src_cp" ;;
        *.cpp|*.cc|*.cxx) echo "src_cpp" ;;
        *.c)   echo "src_c" ;;
        *)     echo "" ;;
    esac
}

add_source_lab() {
    # add_source_lab <name> <group> <path> <cwd>
    local name=$1 group=$2 path=$3 cwd=$4 kind
    kind=$(source_kind "$path")
    [ -n "$kind" ] || return 0
    add_lab "$name" "$group" "$kind" "$path" "$cwd" "compile+run $(basename "$path") [$kind]"
}

script_kind() {
    # Echo a kind token for a runner file, or nothing if it is not a runner.
    case "$1" in
        *.sh)  echo "script_sh" ;;
        *.py)  echo "script_py" ;;
        *)     if [ -x "$1" ]; then echo "script_exec"; else echo ""; fi ;;
    esac
}

add_script_lab() {
    # add_script_lab <name> <group> <path> <cwd>
    local name=$1 group=$2 path=$3 cwd=$4 kind
    kind=$(script_kind "$path")
    case "$kind" in
        script_sh)   add_lab "$name" "$group" "$kind" "$path" "$cwd" "sh $(basename "$path")" ;;
        script_py)   add_lab "$name" "$group" "$kind" "$path" "$cwd" "python3 $(basename "$path")" ;;
        script_exec) add_lab "$name" "$group" "$kind" "$path" "$cwd" "$(basename "$path") (executable)" ;;
        *)           : ;;
    esac
}

SKIP_DIRS="artifacts build out output logs log __pycache__ .git tmp temp results status"

DISCOVERY_FOUND_RUNNER=0

discover_lab_dir() {
    # discover_lab_dir <group> <dir> <dir-basename>
    local group=$1 dir=$2 base=$3
    local f nb found=0
    DISCOVERY_FOUND_RUNNER=0

    if [ -f "$dir/Makefile" ]; then
        add_lab "$group/$base" "$group" "make" "$dir" "$dir" "make -C $base"
        found=1
    elif [ -f "$dir/run.sh" ]; then
        add_script_lab "$group/$base" "$group" "$dir/run.sh" "$dir"
        found=1
    else
        for f in "$dir"/run_*.sh "$dir"/run_*.py; do
            [ -f "$f" ] || continue
            nb=$(basename "$f")
            nb=${nb#run_}
            nb=${nb%.sh}
            nb=${nb%.py}
            add_script_lab "$group/$base/$nb" "$group" "$f" "$dir"
            found=1
        done
    fi
    if [ "$found" -eq 0 ]; then
        for f in "$dir"/*.c "$dir"/*.cp "$dir"/*.cpp; do
            [ -f "$f" ] || continue
            nb=$(basename "$f")
            case "$nb" in
                _*) continue ;;   # scratch / helper probes
            esac
            add_source_lab "$group/$base/${nb%.*}" "$group" "$f" "$dir"
        done
    else
        DISCOVERY_FOUND_RUNNER=1
    fi
}

discover_group() {
    # discover_group <group> <root-path>
    local group=$1 root=$2
    local f base d has_runner=0

    [ -n "$root" ] || return 0

    # tools/abi_compare may be a single executable/script file.
    if [ -f "$root" ]; then
        add_script_lab "$group/tool" "$group" "$root" "$ROOT/tools"
        return 0
    fi
    [ -d "$root" ] || return 0

    # Group-level runners.  When one exists it is authoritative: loose source
    # files in the same directory are its BUILD INPUTS, never standalone labs.
    for f in "$root"/run.sh "$root"/run_all.sh "$root"/run_tests.sh "$root"/Makefile; do
        if [ -f "$f" ]; then
            if [ "$(basename "$f")" = "Makefile" ]; then
                add_lab "$group" "$group" "make" "$root" "$root" "make -C $group"
            else
                add_script_lab "$group" "$group" "$f" "$root"
            fi
            has_runner=1
            break
        fi
    done
    for f in "$root"/run_*.sh "$root"/run_*.py; do
        [ -f "$f" ] || continue
        base=$(basename "$f")
        base=${base#run_}
        base=${base%.sh}
        base=${base%.py}
        add_script_lab "$group/$base" "$group" "$f" "$root"
        has_runner=1
    done

    # Lab subdirectories (each may carry its own runner).
    for d in "$root"/*/; do
        [ -d "$d" ] || continue
        base=$(basename "$d")
        case " $SKIP_DIRS " in
            *" $base "*) continue ;;
        esac
        discover_lab_dir "$group" "$d" "$base"
        [ "$DISCOVERY_FOUND_RUNNER" -eq 1 ] && has_runner=1
    done

    # Loose source files in the group root are labs only when NO runner was
    # discovered for this group (e.g. tests/abi/legacy_c, one main per file).
    if [ "$has_runner" -eq 0 ]; then
        for f in "$root"/*.c "$root"/*.cp "$root"/*.cpp; do
            [ -f "$f" ] || continue
            base=$(basename "$f")
            case "$base" in
                _*) continue ;;   # scratch / helper probes
            esac
            add_source_lab "$group/${base%.*}" "$group" "$f" "$root"
        done
    fi
}

group_root() {
    case "$1" in
        abi_compare)
            if [ -d "$ROOT/tools/abi_compare" ]; then
                echo "$ROOT/tools/abi_compare"
            elif [ -f "$ROOT/tools/abi_compare" ]; then
                echo "$ROOT/tools/abi_compare"
            elif [ -f "$ROOT/tools/abi_compare.sh" ]; then
                echo "$ROOT/tools/abi_compare.sh"
            else
                echo ""
            fi
            ;;
        *)
            echo "$ROOT/tests/abi/$1"
            ;;
    esac
}

for g in "${GROUP_NAMES[@]}"; do
    gr=$(group_root "$g")
    GROUP_DISCOVERED[$g]=0
    discover_group "$g" "$gr"
done

# ---------------------------------------------------------------------------
# Filtering
# ---------------------------------------------------------------------------
contains() {
    # contains <needle> <array-name>
    local needle=$1 arr=$2 item
    eval "for item in \"\${$arr[@]}\"; do
        [ \"\$item\" = \"\$needle\" ] && return 0
    done"
    return 1
}

match_token() {
    # match_token <value> <token>  : exact or substring, case-insensitive-ish
    local value=$1 token=$2
    [ -z "$token" ] && return 1
    case "$value" in
        *"$token"*) return 0 ;;
    esac
    # also match a trailing path component exactly
    case "$value" in
        */"$token") return 0 ;;
    esac
    return 1
}

match_any() {
    # match_any <value> <array-name>
    local value=$1 arr=$2 tok
    [ "$arr" = "ONLY" ] && [ "${#ONLY[@]}" -eq 0 ] && return 0
    [ "$arr" = "GROUP_FILTER" ] && [ "${#GROUP_FILTER[@]}" -eq 0 ] && return 0
    eval "for tok in \"\${$arr[@]}\"; do
        match_token \"\$value\" \"\$tok\" && return 0
    done"
    return 1
}

declare -a SEL=()
for i in "${!LAB_NAME[@]}"; do
    if match_any "${LAB_NAME[$i]}" ONLY && match_any "${LAB_GROUP[$i]}" GROUP_FILTER; then
        SEL+=("$i")
    fi
done

# ---------------------------------------------------------------------------
# --list mode
# ---------------------------------------------------------------------------
if [ "$LIST_ONLY" -eq 1 ]; then
    printf '%-40s %-14s %-12s %s\n' LAB GROUP KIND TARGET
    for i in "${SEL[@]}"; do
        printf '%-40s %-14s %-12s %s\n' \
            "${LAB_NAME[$i]}" "${LAB_GROUP[$i]}" "${LAB_KIND[$i]}" "${LAB_TARGET[$i]}"
    done
    printf 'discovered %d lab(s); selected %d lab(s)\n' "${#LAB_NAME[@]}" "${#SEL[@]}"
    for g in "${GROUP_NAMES[@]}"; do
        printf 'group %-14s discovered=%d path=%s\n' \
            "$g" "${GROUP_DISCOVERED[$g]}" "$(group_root "$g")"
    done
    exit 0
fi

# ---------------------------------------------------------------------------
# Result registry
# ---------------------------------------------------------------------------
declare -a RESULT_NAME=() RESULT_GROUP=() RESULT_STATUS=() RESULT_RC=() \
          RESULT_DURATION=() RESULT_KIND=() RESULT_CMD=() RESULT_STDOUT=() \
          RESULT_STDERR=() RESULT_REASON=()

COUNT_PASS=0; COUNT_FAIL=0; COUNT_BUILD=0; COUNT_TIMEOUT=0
COUNT_SKIPPED=0; COUNT_BLOCKED=0; REQUIRED_FAILURES=0

record_result() {
    # record_result <name> <group> <status> <rc> <duration> <kind> <cmd> <out> <err> <reason>
    RESULT_NAME+=("$1"); RESULT_GROUP+=("$2"); RESULT_STATUS+=("$3")
    RESULT_RC+=("$4"); RESULT_DURATION+=("$5"); RESULT_KIND+=("$6")
    RESULT_CMD+=("$7"); RESULT_STDOUT+=("$8"); RESULT_STDERR+=("$9")
    RESULT_REASON+=("${10}")
    printf '%s|%s|%s|%s|%s|%s|%s|%s|%s\n' \
        "$1" "$2" "$3" "${4:-n/a}" "$5" "$6" "$7" "$8" "$9" >> "$RESULTS_JSONL"
    case "$3" in
        PASS)           COUNT_PASS=$((COUNT_PASS + 1)) ;;
        FAIL)           COUNT_FAIL=$((COUNT_FAIL + 1)); REQUIRED_FAILURES=$((REQUIRED_FAILURES + 1)) ;;
        BUILD_FAILURE)  COUNT_BUILD=$((COUNT_BUILD + 1)); REQUIRED_FAILURES=$((REQUIRED_FAILURES + 1)) ;;
        TIMEOUT)        COUNT_TIMEOUT=$((COUNT_TIMEOUT + 1)); REQUIRED_FAILURES=$((REQUIRED_FAILURES + 1)) ;;
        SKIPPED)        COUNT_SKIPPED=$((COUNT_SKIPPED + 1)) ;;
        BLOCKED)        COUNT_BLOCKED=$((COUNT_BLOCKED + 1)) ;;
    esac
}

classify_rc() {
    # sets STATUS and REASON from exit code $1
    case "$1" in
        0)      STATUS=PASS; REASON="exit 0" ;;
        124|137) STATUS=TIMEOUT; REASON="bounded run exceeded ${RUN_TIMEOUT}s (rc=$1)" ;;
        *)      STATUS=FAIL; REASON="exited nonzero (rc=$1)" ;;
    esac
}

INC_FLAGS=(
    -I"$ROOT/libc" -I"$ROOT/libc/include" -I"$ROOT/libc/install-include"
    -I"$ROOT/stdlib/safe" -I"$ROOT/third_party/safeclib/include"
    -I"$ROOT/third_party/intel-safestringlib/include" -I"$ROOT/tests/abi"
)
LINK_FLAGS=(-lm -pthread)
if [ -f "$ROOT/build/libc/libcplus_hardened.a" ]; then
    LINK_FLAGS=("$ROOT/build/libc/libcplus_hardened.a" "${LINK_FLAGS[@]}")
fi
if [ -f "$ROOT/build/safe/libcplus_safecrt.a" ]; then
    LINK_FLAGS=("$ROOT/build/safe/libcplus_safecrt.a" "${LINK_FLAGS[@]}")
fi

apply_status_file() {
    # apply_status_file <status-file>; honours a lab self-reported status.
    [ -f "$1" ] || return 0
    local s
    s=$(tr -d '[:space:]' < "$1" | tr 'ABCDEFGHIJKLMNOPQRSTUVWXYZ' 'abcdefghijklmnopqrstuvwxyz')
    case "$s" in
        pass)          STATUS=PASS; REASON="self-reported PASS via status file" ;;
        fail)          STATUS=FAIL; REASON="self-reported FAIL via status file" ;;
        build_failure) STATUS=BUILD_FAILURE; REASON="self-reported BUILD_FAILURE via status file" ;;
        timeout)       STATUS=TIMEOUT; REASON="self-reported TIMEOUT via status file" ;;
        skipped)       STATUS=SKIPPED; REASON="self-reported SKIPPED via status file" ;;
        blocked)       STATUS=BLOCKED; REASON="self-reported BLOCKED via status file" ;;
        *)             : ;;
    esac
}

execute_lab() {
    # execute_lab <registry-index>
    local i=$1
    local name=${LAB_NAME[$i]} group=${LAB_GROUP[$i]} kind=${LAB_KIND[$i]}
    local target=${LAB_TARGET[$i]} cwd=${LAB_CWD[$i]} desc=${LAB_DESC[$i]}
    local safe; safe=$(safe_name "$name")
    local stdout="$LOG_DIR/$safe.stdout" stderr="$LOG_DIR/$safe.stderr"
    local bstdout="$LOG_DIR/$safe.build.stdout" bstderr="$LOG_DIR/$safe.build.stderr"
    local status_file="$STATUS_DIR/$safe.status" bin="$ARTIFACT_DIR/$safe.bin"
    local STATUS=PASS REASON="" rc="" brc=0 start=0 end=0 duration=0

    : > "$stdout"; : > "$stderr"; : > "$bstdout"; : > "$bstderr"
    rm -f "$status_file" "$bin"

    export ROOT REPO_ROOT="$ROOT" CPLUS_CLANG="$CC"
    export ABI_LAB_NAME="$name" ABI_LAB_GROUP="$group" ABI_LAB_DIR="$cwd"
    export ABI_OUT_DIR="$OUT" ABI_LAB_STATUS_FILE="$status_file"

    if [ -z "$TIMEOUT_BIN" ]; then
        STATUS=BLOCKED; REASON="no bounded-timeout utility available"; rc="n/a"
        record_result "$name" "$group" "$STATUS" "$rc" 0 "$kind" "$desc" "$stdout" "$stderr" "$REASON"
        printf 'BLOCKED %-40s %s\n' "$name" "$REASON"
        return
    fi
    if [ ! -e "$target" ]; then
        STATUS=BLOCKED; REASON="target does not exist: $target"; rc="n/a"
        record_result "$name" "$group" "$STATUS" "$rc" 0 "$kind" "$desc" "$stdout" "$stderr" "$REASON"
        printf 'BLOCKED %-40s %s\n' "$name" "$REASON"
        return
    fi

    start=$(date +%s)
    case "$kind" in
        src_c|src_cpp|src_cp)
            if [ "$kind" = "src_cp" ]; then
                if [ ! -x "$ROOT/build/c+" ]; then
                    STATUS=BLOCKED; REASON="missing build/c+ driver for .cp source"
                else
                    "$ROOT/build/c+" build --sanitize=none "$target" -o "$bin" \
                        >"$bstdout" 2>"$bstderr"
                    brc=$?
                    if [ "$brc" -ne 0 ]; then
                        STATUS=BUILD_FAILURE; rc=$brc
                        REASON="c+ compile failed (rc=$brc); see $bstderr"
                    else
                        "$TIMEOUT_BIN" "$RUN_TIMEOUT" "$bin" >"$stdout" 2>"$stderr"
                        rc=$?
                        classify_rc "$rc"
                    fi
                fi
            else
                if [ "$kind" = "src_cpp" ]; then
                    local cxx=${CXX:-/usr/bin/clang++}
                    "$cxx" -O1 -std=c++17 -Wall -Wextra "${INC_FLAGS[@]}" \
                        "$target" "${LINK_FLAGS[@]}" -o "$bin" >"$bstdout" 2>"$bstderr"
                else
                    "$CC" -O1 -std=gnu11 -Wall -Wextra "${INC_FLAGS[@]}" \
                        "$target" "${LINK_FLAGS[@]}" -o "$bin" >"$bstdout" 2>"$bstderr"
                fi
                brc=$?
                if [ "$brc" -ne 0 ]; then
                    STATUS=BUILD_FAILURE; rc=$brc
                    REASON="compile failed (rc=$brc); see $bstderr"
                else
                    "$TIMEOUT_BIN" "$RUN_TIMEOUT" "$bin" >"$stdout" 2>"$stderr"
                    rc=$?
                    classify_rc "$rc"
                fi
            fi
            ;;
        script_sh)
            ( cd "$cwd" && "$TIMEOUT_BIN" "$RUN_TIMEOUT" /bin/sh "$target" ) \
                >"$stdout" 2>"$stderr"
            rc=$?
            classify_rc "$rc"
            ;;
        script_py)
            ( cd "$cwd" && "$TIMEOUT_BIN" "$RUN_TIMEOUT" python3 "$target" ) \
                >"$stdout" 2>"$stderr"
            rc=$?
            classify_rc "$rc"
            ;;
        script_exec)
            ( cd "$cwd" && "$TIMEOUT_BIN" "$RUN_TIMEOUT" "$target" ) \
                >"$stdout" 2>"$stderr"
            rc=$?
            classify_rc "$rc"
            ;;
        make)
            ( cd "$cwd" && "$TIMEOUT_BIN" "$BUILD_TIMEOUT" make ) \
                >"$bstdout" 2>"$bstderr"
            brc=$?
            if [ "$brc" -ne 0 ]; then
                STATUS=BUILD_FAILURE; rc=$brc
                REASON="make failed (rc=$brc); see $bstderr"
            else
                STATUS=PASS; rc=0; REASON="make succeeded"
            fi
            ;;
        *)
            STATUS=BLOCKED; REASON="unknown lab kind: $kind"; rc="n/a"
            ;;
    esac

    end=$(date +%s)
    duration=$((end - start))

    # A TIMEOUT from the outer bound always wins over a self-report.
    if [ "$STATUS" != "TIMEOUT" ]; then
        apply_status_file "$status_file"
    fi

    printf '%s\n' "$desc" >> "$COMMANDS_LOG"
    printf '  rc=%s status=%s stdout=%s stderr=%s\n' \
        "${rc:-n/a}" "$STATUS" "$stdout" "$stderr" >> "$COMMANDS_LOG"

    record_result "$name" "$group" "$STATUS" "$rc" "$duration" "$kind" \
        "$desc" "$stdout" "$stderr" "$REASON"
    printf '%-8s %-40s exit=%-5s %ss\n' "$STATUS" "$name" "${rc:-n/a}" "$duration"
}

# ---------------------------------------------------------------------------
# Empty-group / no-match handling (never silently green).
# ---------------------------------------------------------------------------
declare -a SELECTED_GROUPS=()
if [ "${#GROUP_FILTER[@]}" -gt 0 ]; then
    for g in "${GROUP_NAMES[@]}"; do
        if match_any "$g" GROUP_FILTER; then SELECTED_GROUPS+=("$g"); fi
    done
else
    SELECTED_GROUPS=("${GROUP_NAMES[@]}")
fi

if [ "${#SEL[@]}" -eq 0 ]; then
    record_result "all::NO_MATCH" "-" "SKIPPED" "n/a" 0 "filter" \
        "filters: only=${ONLY[*]:-<none>} group=${GROUP_FILTER[*]:-<none>}" \
        "/dev/null" "/dev/null" \
        "no lab matched the requested filters (only/group)"
    REQUIRED_FAILURES=$((REQUIRED_FAILURES + 1))
elif [ "${#GROUP_FILTER[@]}" -gt 0 ]; then
    for g in "${SELECTED_GROUPS[@]}"; do
        n=0
        for i in "${SEL[@]}"; do
            [ "${LAB_GROUP[$i]}" = "$g" ] && n=$((n + 1))
        done
        if [ "$n" -eq 0 ]; then
            record_result "$g::EMPTY" "$g" "SKIPPED" "n/a" 0 "group" \
                "group=$g (selected labs=0, discovered=${GROUP_DISCOVERED[$g]})" \
                "/dev/null" "/dev/null" \
                "selected group '$g' has no labs (empty or filtered out)"
            REQUIRED_FAILURES=$((REQUIRED_FAILURES + 1))
        fi
    done
else
    for g in "${GROUP_NAMES[@]}"; do
        if [ "${GROUP_DISCOVERED[$g]}" -eq 0 ]; then
            record_result "$g::EMPTY" "$g" "SKIPPED" "n/a" 0 "group" \
                "group=$g (discovered=0)" "/dev/null" "/dev/null" \
                "group '$g' is empty: no labs discovered under $(group_root "$g")"
            REQUIRED_FAILURES=$((REQUIRED_FAILURES + 1))
        fi
    done
fi

# ---------------------------------------------------------------------------
# Run
# ---------------------------------------------------------------------------
printf 'unified ABI lab runner: branch=%s started=%s\n' "$BRANCH" "$STARTED_UTC"
printf 'discovered=%d selected=%d  build_timeout=%ss run_timeout=%ss\n' \
    "${#LAB_NAME[@]}" "${#SEL[@]}" "$BUILD_TIMEOUT" "$RUN_TIMEOUT"
printf 'evidence: %s\n\n' "$OUT"
printf '%-8s %-40s %-9s %-7s\n' STATUS LAB EXIT DURATION
printf '%-8s %-40s %-9s %-7s\n' ------ --- ---- --------

for i in "${SEL[@]}"; do
    execute_lab "$i"
done

# ---------------------------------------------------------------------------
# JSON summary (pure shell; no Python in the correctness path)
# ---------------------------------------------------------------------------
{
    printf '{\n'
    printf '  "schema_version": 1,\n'
    printf '  "runner": "tests/abi/run_legacy_labs.sh",\n'
    printf '  "branch": "%s",\n' "$(json_escape "$BRANCH")"
    printf '  "started": "%s",\n' "$STARTED_UTC"
    printf '  "finished": "%s",\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf '  "filters": {"only": ['
    j=0
    for t in "${ONLY[@]}"; do
        [ "$j" -gt 0 ] && printf ', '
        printf '"%s"' "$(json_escape "$t")"
        j=$((j+1))
    done
    printf '], "group": ['
    j=0
    for t in "${GROUP_FILTER[@]}"; do
        [ "$j" -gt 0 ] && printf ', '
        printf '"%s"' "$(json_escape "$t")"
        j=$((j+1))
    done
    printf ']},\n'
    printf '  "timeouts": {"build_s": %s, "run_s": %s, "timeout_bin": "%s"},\n' \
        "$BUILD_TIMEOUT" "$RUN_TIMEOUT" "$(json_escape "$TIMEOUT_BIN")"
    printf '  "groups": {'
    j=0
    for g in "${GROUP_NAMES[@]}"; do
        [ "$j" -gt 0 ] && printf ', '
        printf '"%s": {"discovered": %d, "path": "%s"}' \
            "$(json_escape "$g")" "${GROUP_DISCOVERED[$g]}" "$(json_escape "$(group_root "$g")")"
        j=$((j+1))
    done
    printf '},\n'
    printf '  "counts": {"total": %d, "pass": %d, "fail": %d, "build_failure": %d, "timeout": %d, "skipped": %d, "blocked": %d},\n' \
        "${#RESULT_NAME[@]}" "$COUNT_PASS" "$COUNT_FAIL" "$COUNT_BUILD" \
        "$COUNT_TIMEOUT" "$COUNT_SKIPPED" "$COUNT_BLOCKED"
    printf '  "required_failures": %d,\n' "$REQUIRED_FAILURES"
    printf '  "results": [\n'
    for j in "${!RESULT_NAME[@]}"; do
        [ "$j" -gt 0 ] && printf ',\n'
        printf '    {"lab":"%s","group":"%s","status":"%s","exit":"%s",' \
            "$(json_escape "${RESULT_NAME[$j]}")" \
            "$(json_escape "${RESULT_GROUP[$j]}")" \
            "$(json_escape "${RESULT_STATUS[$j]}")" \
            "$(json_escape "${RESULT_RC[$j]}")"
        printf '"duration_s":%s,"kind":"%s","command":"%s",' \
            "${RESULT_DURATION[$j]}" "$(json_escape "${RESULT_KIND[$j]}")" \
            "$(json_escape "${RESULT_CMD[$j]}")"
        printf '"stdout_file":"%s","stderr_file":"%s","reason":"%s"}' \
            "$(json_escape "${RESULT_STDOUT[$j]}")" \
            "$(json_escape "${RESULT_STDERR[$j]}")" \
            "$(json_escape "${RESULT_REASON[$j]}")"
    done
    printf '\n  ]\n}\n'
} > "$SUMMARY_JSON"

# ---------------------------------------------------------------------------
# Final table + status
# ---------------------------------------------------------------------------
printf '\n'
printf '%-40s %-14s %-14s %-7s\n' LAB GROUP STATUS EXIT
printf '%-40s %-14s %-14s %-7s\n' --- ----- ------ ----
for j in "${!RESULT_NAME[@]}"; do
    printf '%-40s %-14s %-14s %-7s\n' \
        "${RESULT_NAME[$j]}" "${RESULT_GROUP[$j]}" \
        "${RESULT_STATUS[$j]}" "${RESULT_RC[$j]}"
done

printf '\nrun_legacy_labs: total=%d pass=%d fail=%d build_failure=%d timeout=%d skipped=%d blocked=%d\n' \
    "${#RESULT_NAME[@]}" "$COUNT_PASS" "$COUNT_FAIL" "$COUNT_BUILD" \
    "$COUNT_TIMEOUT" "$COUNT_SKIPPED" "$COUNT_BLOCKED"
printf 'required_failures=%d  summary=%s  logs=%s\n' \
    "$REQUIRED_FAILURES" "$SUMMARY_JSON" "$LOG_DIR"

if [ "$REQUIRED_FAILURES" -ne 0 ]; then
    exit 1
fi
exit 0
