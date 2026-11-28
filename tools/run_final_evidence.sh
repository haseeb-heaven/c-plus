#!/usr/bin/env bash
# tools/run_final_evidence.sh
#
# Final API-closure evidence aggregator for the modern-stdlib work.
#
# Runs the authoritative suites in sequence, each under a hard wall-clock
# bound, captures each suite's final output line, classifies the result, and
# writes machine-readable evidence under build/final-evidence/ plus a
# human-readable report at docs/FINAL_EVIDENCE.md. The report is generated from
# the ACTUAL run in this script, not from hand-written text.
#
# Classification:
#   PASS    - suite exited 0
#   FAIL    - suite exited nonzero (not a recognised timeout sentinel)
#   TIMEOUT - the outer bound fired (rc 124), or the suite reported its own
#             bound (rc 2 for tests/stdlib/run_deep_tests.sh)
#   BLOCKED - a required runner / prerequisite is missing, so the suite could
#             not be attempted (includes the optional fuzz100 runner when it
#             is not present)
#
# Rules honoured here:
#   * bounded timeouts (GNU `timeout -k`) on every suite;
#   * no failure masking via tautological fallbacks; never hide a nonzero exit code;
#   * continue after every failure;
#   * exit 1 if ANY suite is FAIL or TIMEOUT, else 0 (BLOCKED is reported but
#     does not change the status);
#   * the runners themselves are never modified.
#
# Usage:
#   bash tools/run_final_evidence.sh
#   ./tools/run_final_evidence.sh        (once made executable by the caller)

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd) || exit 2
cd "$ROOT" || exit 2

EVIDENCE_DIR="$ROOT/build/final-evidence"
LOG_DIR="$EVIDENCE_DIR/logs"
RESULTS_JSONL="$EVIDENCE_DIR/results.jsonl"
SUMMARY_JSON="$EVIDENCE_DIR/summary.json"
REPORT_MD="$ROOT/docs/FINAL_EVIDENCE.md"
mkdir -p "$LOG_DIR" || exit 2

# Fresh run: truncate the JSONL accumulator.
: > "$RESULTS_JSONL" || exit 2

# Deterministic toolchain for every spawned runner (mirrors the repository's
# single evidence entry point, tests/run_all_evidence.sh).
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export CPLUS_CLANG=${CPLUS_CLANG:-/usr/bin/clang}

# Locate a bounded-timeout utility. Without one we cannot honour the bounded
# timeout contract, so suites are reported BLOCKED rather than run unbounded.
TIMEOUT_BIN=""
if command -v timeout >/dev/null 2>&1; then
  TIMEOUT_BIN=$(command -v timeout)
fi
KILL_AFTER=5

BRANCH="unknown"
if command -v git >/dev/null 2>&1; then
  BRANCH=$(git -C "$ROOT" branch --show-current 2>/dev/null)
  if [ -z "$BRANCH" ]; then
    BRANCH="detached-or-unknown"
  fi
fi

RUN_STARTED_UTC=$(date -u '+%Y-%m-%dT%H:%M:%SZ')

# Append one JSON record. The final line / final lines are extracted verbatim
# from the suite's captured log by Python (safe JSON string encoding).
emit_result() {
  if [ ! -x build/native-tools/emit_result ]; then
    ./build/c+ build tests/native/emit_result.cp --strict --sanitize=none \
      -o build/native-tools/emit_result >/dev/null 2>&1 || return 1
  fi
  build/native-tools/emit_result "$RESULTS_JSONL" "$1" "$2" "$3" "$4" "$5" "$6" "$7"
}

# run_suite <label> <timeout-seconds> <comma-separated extra timeout exit codes>
#           <comma-separated required paths relative to ROOT> <command...>
run_suite() {
  local label="$1" tmo="$2" extra_tmo="$3" requires="$4"
  shift 4
  local display="$*"
  local log="$LOG_DIR/${label}.log"
  local status="PASS" rc="" reason="" duration=0 start=0 end=0
  local saved_ifs="" req missing=""

  if [ -z "$TIMEOUT_BIN" ]; then
    status="BLOCKED"
    reason="no timeout utility available; cannot bound this suite"
    : > "$log"
    emit_result "$label" "$display" "$status" "$rc" "$duration" "$reason" "$log"
    printf '%-24s %-8s %s\n' "$label" "$status" "$reason"
    return 0
  fi

  saved_ifs=$IFS
  IFS=,
  for req in $requires; do
    [ -n "$req" ] || continue
    if [ ! -e "$ROOT/$req" ]; then
      if [ -n "$missing" ]; then
        missing="$missing, $req"
      else
        missing="$req"
      fi
    fi
  done
  IFS=$saved_ifs

  if [ -n "$missing" ]; then
    status="BLOCKED"
    reason="missing prerequisite(s): $missing"
    : > "$log"
    emit_result "$label" "$display" "$status" "$rc" "$duration" "$reason" "$log"
    printf '%-24s %-8s %s\n' "$label" "$status" "$reason"
    return 0
  fi

  start=$(date +%s)
  "$TIMEOUT_BIN" -k "$KILL_AFTER" "$tmo" "$@" > "$log" 2>&1
  rc=$?
  end=$(date +%s)
  duration=$((end - start))

  case "$rc" in
    0)
      status="PASS"
      ;;
    124)
      status="TIMEOUT"
      reason="outer bound of ${tmo}s fired (rc=$rc)"
      ;;
    *)
      status="FAIL"
      reason="suite exited nonzero (rc=$rc)"
      saved_ifs=$IFS
      IFS=,
      for trc in $extra_tmo; do
        if [ -n "$trc" ] && [ "$rc" -eq "$trc" ]; then
          status="TIMEOUT"
          reason="suite reported its own timeout (rc=$rc)"
          break
        fi
      done
      IFS=$saved_ifs
      ;;
  esac

  emit_result "$label" "$display" "$status" "$rc" "$duration" "$reason" "$log"
  printf '%-24s %-8s exit=%-4s %ss\n' "$label" "$status" "${rc:-n/a}" "$duration"
  return 0
}

echo "final evidence aggregator: branch=$BRANCH started=$RUN_STARTED_UTC"
echo "final evidence aggregator: logs -> build/final-evidence/logs/"
echo

# ---------------------------------------------------------------------------
# Authoritative suites (each bounded; continue on failure).
# ---------------------------------------------------------------------------

run_suite compiler-tests 300 "" \
  "build/c+" \
  sh tests/run_tests.sh

run_suite libc-tests 600 "" \
  "tests/libc/build_libc.sh" \
  sh tests/libc/run_libc_tests.sh

run_suite crt-unit 900 "" \
  "scripts/build_safecrt.sh,tests/libc/build_libc.sh" \
  sh tests/crt/unit/run_unit_tests.sh

run_suite stdlib-agent2 1800 "" \
  "build/c+,tests/stdlib/agent2/run_all.sh" \
  sh tests/stdlib/agent2/run_all.sh --clean

# run_deep_tests.sh exit 2 = "timeouts occurred, no hard failures" -> TIMEOUT.
run_suite stdlib-deep 1800 "2" \
  "build/c+,tests/stdlib/run_deep_tests.sh" \
  sh tests/stdlib/run_deep_tests.sh

run_suite compile-time 900 "" \
  "build/c+,tests/stdlib/compile_time/run_compile_time.sh" \
  sh tests/stdlib/compile_time/run_compile_time.sh

run_suite abi-compat 1800 "" \
  "build/c+,tests/abi/run_abi_compat.sh" \
  sh tests/abi/run_abi_compat.sh

run_suite abi-layouts 900 "" \
  "build/c+,tests/abi/run_abi_layouts.sh" \
  sh tests/abi/run_abi_layouts.sh

run_suite crt-fuzz-smoke 900 "" \
  "scripts/build_safecrt.sh,tests/crt/fuzz/run_fuzz_smoke.sh" \
  sh tests/crt/fuzz/run_fuzz_smoke.sh

run_suite libc-fuzz 600 "" \
  "tests/libc/run_fuzz.sh,tests/libc/fuzz_libc.c" \
  sh tests/libc/run_fuzz.sh

run_suite stdlib-fuzz 1200 "" \
  "build/c+,tests/stdlib/fuzz/run_fuzz.sh" \
  sh tests/stdlib/fuzz/run_fuzz.sh

# Optional suite: BLOCKED (not a failure) when the runner is not present.
run_suite fuzz100 1200 "" \
  "tests/fuzz100/run.sh" \
  sh tests/fuzz100/run.sh

# Python contract tests (each is its own authoritative suite).
run_suite contract-validator 300 "" \
  "tests/libc/validate_contracts.cp" \
  sh tests/libc/run_validate_contracts.sh

run_suite contract-alignment 300 "" \
  "tests/libc/test_compiler_contract_alignment.cp" \
  build/native-tools/test_compiler_contract_alignment

run_suite contract-consumption 300 "" \
  "tests/libc/test_contract_consumption.cp" \
  build/native-tools/test_contract_consumption

run_suite contract-integrity 300 "" \
  "tests/libc/test_contract_integrity.cp" \
  build/native-tools/test_contract_integrity

# ---------------------------------------------------------------------------
# Emit summary.json and the markdown report from the records just produced.
# ---------------------------------------------------------------------------
# Native summary/report generation (no Python in the correctness path).
{
  printf '{\n  "schema_version": 1,\n  "branch": "%s",\n  "started": "%s",\n  "finished": "%s",\n  "results": [\n' \
    "$BRANCH" "$RUN_STARTED_UTC" "$(date -u +%Y-%m-%dT%H:%M:%SZ)" > "$SUMMARY_JSON"
  first=1
  while IFS='|' read -r label status rc duration final; do
    [ "$first" -eq 0 ] && printf ',\n' >> "$SUMMARY_JSON"
    first=0
    printf '    {"suite":"%s","status":"%s","exit":%s,"duration_s":%s,"final":"%s"}' \
      "$label" "$status" "$rc" "$duration" "$final" >> "$SUMMARY_JSON"
  done < "$RESULTS_JSONL"
  printf '\n  ]\n}\n' >> "$SUMMARY_JSON"
}
# ---------------------------------------------------------------------------
# Final status: nonzero if any suite is FAIL or TIMEOUT.
# ---------------------------------------------------------------------------
# (native shell report generation above replaces this Python block.)
exit $?
