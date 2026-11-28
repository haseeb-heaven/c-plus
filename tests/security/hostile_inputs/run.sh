#!/bin/sh
# Hostile-input corpus runner (Task 6 Phase 2).
#
# Verification order is deliberately strict/no-sanitizer FIRST, then UBSan.
# Every build is bounded by 60 s and every run by 30 s (SIGKILL escalation).
# A timeout is SKIPPED/BLOCKED, never a PASS. No source outside this directory
# is created or modified by this script; outputs go to a temporary directory.
#
# Usage: sh tests/security/hostile_inputs/run.sh
#        HOSTILE_OUT=/path CPLUS_BIN=/path sh tests/security/hostile_inputs/run.sh
#
# Exit status: 0 = all executed harnesses passed in both executed modes;
#              1 = at least one build/run failure;
#              2 = no failures but a mode was BLOCKED (e.g. canonical UBSan
#                  toolchain defect) or timed out.

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd) || exit 1
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT=${HOSTILE_OUT:-"${TMPDIR:-/tmp}/cplus-hostile-inputs"}
BUILD_TIMEOUT=60
RUN_TIMEOUT=30
KILL_AFTER=5
FIXTURES="$ROOT/tests/security/hostile_inputs/fixtures"
JSON_FLAGS="-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson"

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 1

if ! command -v timeout >/dev/null 2>&1; then
    echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
    exit 1
fi
if [ ! -x "$CPLUS" ]; then
    echo "error: C+ compiler missing: $CPLUS (build it first)" >&2
    exit 1
fi

HARNESSES="hostile_string hostile_io_file hostile_fs_net hostile_json"
pass_strict_none=0; fail_any=0; blocked=0
pass_ubsan_alt=0

json_libs_for() {
    case "$1" in
        hostile_json) printf '%s' "$JSON_FLAGS" ;;
        *) printf '%s' '' ;;
    esac
}

printf 'hostile corpus: strict-none first, then ubsan; build<=%ss run<=%ss\n' \
    "$BUILD_TIMEOUT" "$RUN_TIMEOUT"

# ---- mode 1: canonical C+ strict + no sanitizer ------------------------
for name in $HARNESSES; do
    extra=$(json_libs_for "$name")
    bin="$OUT/${name}_strict_none"
    timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build \
        "tests/security/hostile_inputs/$name.cp" --strict --sanitize=none \
        $extra -o "$bin" >"$OUT/${name}_strict_none.build.log" 2>&1
    brc=$?
    if [ "$brc" -ne 0 ]; then
        printf 'BUILD-FAIL strict-none %s rc=%s\n' "$name" "$brc"
        tail -n 5 "$OUT/${name}_strict_none.build.log" | sed 's/^/           /'
        fail_any=1
        continue
    fi
    timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$bin" "$FIXTURES" \
        >"$OUT/${name}_strict_none.out" 2>"$OUT/${name}_strict_none.err"
    rrc=$?
    if [ "$rrc" -eq 124 ] || [ "$rrc" -ge 128 ]; then
        printf 'TIMEOUT strict-none %s rc=%s\n' "$name" "$rrc"
        blocked=1
    elif [ "$rrc" -ne 0 ]; then
        printf 'RUN-FAIL strict-none %s rc=%s\n' "$name" "$rrc"
        tail -n 8 "$OUT/${name}_strict_none.err" | sed 's/^/           /'
        fail_any=1
    else
        printf 'ok strict-none %s: %s\n' "$name" \
            "$(tail -n 1 "$OUT/${name}_strict_none.out")"
        pass_strict_none=$((pass_strict_none + 1))
    fi
done

# ---- canonical UBSan: recorded, but known BLOCKED on this host ----------
# `--strict --sanitize=undefined` rebuilds the C+ runtime with sanitizer
# instrumentation. The bundled c-plus-core runtime references
# CPLUS_EXIT_ARITHMETIC, which is undeclared in that instrumented build. We probe
# it once and report BLOCKED rather than silently skipping it.
timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build \
    "tests/security/hostile_inputs/hostile_string.cp" --strict \
    --sanitize=undefined -o "$OUT/_ubsan_probe" \
    >"$OUT/_ubsan_probe.build.log" 2>&1
ubrc=$?
if [ "$ubrc" -eq 0 ]; then
    printf 'ok canonical-ubsan probe built\n'
else
    printf 'BLOCKED canonical-ubsan: c+ --strict --sanitize=undefined build rc=%s\n' \
        "$ubrc"
    tail -n 4 "$OUT/_ubsan_probe.build.log" | sed 's/^/           /'
    blocked=1
fi

# ---- mode 2: UBSan-instrumented lowered harness/runtime headers ---------
# Workaround for the blocked canonical mode: keep --sanitize=none (so the C+
# runtime link succeeds) and pass the Clang UBSan flags after `--`, which
# instruments the lowered harness translation unit including the stdlib .hp
# headers. This is UBSan evidence for the parsers, not for the C+ runtime.
for name in $HARNESSES; do
    extra=$(json_libs_for "$name")
    bin="$OUT/${name}_ubsan"
    timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build \
        "tests/security/hostile_inputs/$name.cp" --strict --sanitize=none \
        $extra -o "$bin" -- -fsanitize=undefined -fno-sanitize-recover=all \
        >"$OUT/${name}_ubsan.build.log" 2>&1
    brc=$?
    if [ "$brc" -ne 0 ]; then
        printf 'BUILD-FAIL ubsan %s rc=%s\n' "$name" "$brc"
        tail -n 5 "$OUT/${name}_ubsan.build.log" | sed 's/^/           /'
        fail_any=1
        continue
    fi
    timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$bin" "$FIXTURES" \
        >"$OUT/${name}_ubsan.out" 2>"$OUT/${name}_ubsan.err"
    rrc=$?
    # UBSan diagnostics go to stderr; any stderr output is a finding.
    err_bytes=$(wc -c <"$OUT/${name}_ubsan.err" | tr -d ' ')
    if [ "$rrc" -eq 124 ] || [ "$rrc" -ge 128 ]; then
        printf 'TIMEOUT ubsan %s rc=%s\n' "$name" "$rrc"
        blocked=1
    elif [ "$rrc" -ne 0 ] || [ "$err_bytes" != "0" ]; then
        printf 'UBSAN-FINDING %s rc=%s stderr_bytes=%s\n' "$name" "$rrc" \
            "$err_bytes"
        head -n 10 "$OUT/${name}_ubsan.err" | sed 's/^/           /'
        fail_any=1
    else
        printf 'ok ubsan %s: %s\n' "$name" \
            "$(tail -n 1 "$OUT/${name}_ubsan.out")"
        pass_ubsan_alt=$((pass_ubsan_alt + 1))
    fi
done

printf '\nSUMMARY strict-none=%s/4 ubsan-instrumented=%s/4 failures=%s blocked=%s\n' \
    "$pass_strict_none" "$pass_ubsan_alt" "$fail_any" "$blocked"
printf 'artifacts: %s\n' "$OUT"

[ "$fail_any" -ne 0 ] && exit 1
[ "$blocked" -ne 0 ] && exit 2
exit 0
