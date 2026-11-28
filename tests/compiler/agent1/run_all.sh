#!/usr/bin/env bash
# Agent 1 complete compiler safety runner.
# Discovers and executes all compiler safety tests with actual exit codes,
# expected diagnostics, and generated-C capture. No Python. No suppressed failures.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS="${CPLUS_BIN:-$ROOT/build/c+}"
RES="$ROOT/tests/compiler/agent1/results"
RESULTS_FULL=0; ONE=""; CATEGORY=""; MODE="strict"; TIMEOUT=60
mkdir -p "$RES/logs" "$RES/generated"

usage() {
  cat <<EOF
Usage: run_all.sh [options]
  --full              run every category
  --test NAME         run one named test
  --category NAME     compile_fail|runtime_fail|positive|strict_positive|native|strict40|stress|regression|core|legacy|sanitizer
  --mode strict|default|legacy
  --timeout SECONDS   per-test timeout (default 60)
  --help
Results: $RES/summary.txt, $RES/summary.json
EOF
}
while [ $# -gt 0 ]; do
  case "$1" in
    --full) RESULTS_FULL=1; shift ;;
    --test) ONE="${2:-}"; shift 2 ;;
    --category) CATEGORY="${2:-}"; shift 2 ;;
    --mode) MODE="${2:-strict}"; shift 2 ;;
    --timeout) TIMEOUT="${2:-60}"; shift 2 ;;
    --help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 2 ;;
  esac
done
if [ "$RESULTS_FULL" -eq 0 ] && [ -z "$ONE" ] && [ -z "$CATEGORY" ]; then
  RESULTS_FULL=1
fi


# Portable timeout: GNU timeout when present, otherwise Perl alarm.
run_timed() {
  secs=$1; shift
  if command -v timeout >/dev/null 2>&1; then
    timeout "$secs" "$@"
  elif command -v perl >/dev/null 2>&1; then
    perl -e '$s=shift @ARGV; alarm $s; exec @ARGV; alarm 0' "$secs" "$@"
  else
    "$@"
  fi
}

MODEFLAG="--strict"
case "$MODE" in
  strict) MODEFLAG="--strict" ;;
  default) MODEFLAG="--default" ;;
  legacy) MODEFLAG="--legacy-c" ;;
  *) echo "bad mode: $MODE" >&2; exit 2 ;;
esac

: > "$RES/summary.txt"
printf '{\n  "mode": "%s",\n  "results": [\n' "$MODE" > "$RES/summary.json"
cat > "$RES/mode_matrix.txt" <<EOF
MODE MATRIX
- compile_fail: DEFAULT, expect compile rejection with exact diagnostic.
- runtime_fail: DEFAULT with sanitizers enabled (fixtures rely on runtime traps); expect exact stderr + exit code.
- positive: DEFAULT, build+run+stdout match (compat fixtures).
- strict_positive: STRICT --sanitize=none, build+run (safe subset only; multi-TU linked; uaf_conditional excluded as documented possible-UAF).
- native/strict40: STRICT + build --strict --sanitize=none via native harnesses.
- stress: project breaker suites.
EOF
PASS=0; FAIL=0; SKIP=0; TOTAL=0

record() {
  name=$1; status=$2; cmd=$3; stdout=$4; stderr=$5; code=$6
  TOTAL=$((TOTAL+1))
  if [ "$status" = "PASS" ]; then PASS=$((PASS+1));
  elif [ "$status" = "SKIPPED" ] || [ "$status" = "SKIP" ]; then
    SKIP=$((SKIP+1));
  else FAIL=$((FAIL+1)); fi
  printf '%-8s %-42s rc=%s cmd=%s\n' "$status" "$name" "$code" "$cmd" >> "$RES/summary.txt"
  printf '    {"name":"%s","status":"%s","rc":"%s","cmd":"%s"},\n' \
    "$name" "$status" "$code" "$cmd" >> "$RES/summary.json"
  printf '%s\n' "$stdout" > "$RES/logs/$name.stdout"
  printf '%s\n' "$stderr" > "$RES/logs/$name.stderr"
}

want_category() {
  [ "$RESULTS_FULL" -eq 1 ] && return 0
  [ "$CATEGORY" = "$1" ] && return 0
  [ "$CATEGORY" = "$ONE" ] && return 0
  return 1
}

run_source() {
  name=$1; src=$2; kind=$3
  if [ -n "$ONE" ] && [ "$ONE" != "$name" ]; then return; fi
  out="$OUT/$name.stdout"; err="$OUT/$name.stderr"; gen="$RES/generated/$name.c"
  sourceflag="$MODEFLAG"
  case "$kind" in
    positive) sourceflag="--default" ;;
    runtime_fail) sourceflag="--default"; safe_flags="" ;;
    compile_fail) sourceflag="--default" ;;
  esac
  sanflag="--sanitize=none"
  [ "$kind" = "runtime_fail" ] && sanflag=""
  # Issue #34: warnings are fatal by default. These two kinds exercise runtime
  # behaviour and intentionally include programs with warning-level findings;
  # --no-werror is the documented diagnostic-only opt-out. Rejection tests keep
  # the default policy so warning-promoted failures stay covered.
  wflag=""
  [ "$kind" = "positive" ] && wflag="--no-werror"
  [ "$kind" = "runtime_fail" ] && wflag="--no-werror"
  cmd="$CPLUS $sourceflag $sanflag $wflag --emit-c \"$gen\" \"$src\" -o \"$OUT/$name.bin\""
  run_timed "$TIMEOUT" $CPLUS $sourceflag $sanflag $wflag --emit-c "$gen" "$src" -o "$OUT/$name.bin" >"$out" 2>"$err"
  code=$?
  diag=$(grep -E 'error:|warning:' "$err" "$out" 2>/dev/null | head -1)
  expected=""
  case "$kind" in
    compile_fail) expected=$(cat "${src%.*}.err" 2>/dev/null) ;;
    runtime_fail) expected=$(cat "${src%.*}.stderr" 2>/dev/null) ;;
  esac
  if [ "$kind" = "positive" ]; then
    if [ "$code" -eq 0 ]; then
      if [ -x "$OUT/$name.bin" ]; then
        want=""
        [ -f "${src%.*}.expected" ] && want=$(cat "${src%.*}.expected")
        if [ -f "${src%.*}.stdin" ]; then
          got=$(run_timed "$TIMEOUT" sh -c '"$1" < "$2"' sh "$OUT/$name.bin" "${src%.*}.stdin" 2>>"$err")
        else
          got=$(run_timed "$TIMEOUT" "$OUT/$name.bin" 2>>"$err")
        fi
        if [ -z "$want" ] || [ "$got" = "$want" ]; then
          record "$name" PASS "$cmd" "$got" "$diag" 0
        else
          record "$name" DIAGNOSTIC_MISMATCH "$cmd" "got=$got want=$want" "$diag" 0
        fi
      else
        record "$name" BUILD_FAILURE "$cmd" "no executable" "$diag" "$code"
      fi
    else
      record "$name" BUILD_FAILURE "$cmd" "" "$diag" "$code"
    fi
    return
  fi
  if [ "$kind" = "runtime_fail" ]; then
    if [ "$code" -ne 0 ] || [ ! -x "$OUT/$name.bin" ]; then
      record "$name" BUILD_FAILURE "$cmd" "" "$diag" "$code"
      return
    fi
    wantcode=$(cat "${src%.*}.exit" 2>/dev/null)
    [ -n "$wantcode" ] || wantcode=134
    run_timed "$TIMEOUT" "$OUT/$name.bin" >"$out.run" 2>"$err.run"
    rcode=$?
    rdiag=$(grep -E 'error:|warning:|runtime error' "$err.run" "$out.run" 2>/dev/null | head -1)
    if [ "$rcode" -eq "$wantcode" ] && { [ -z "$expected" ] || printf '%s' "$rdiag" | grep -Fq -- "$expected"; }; then
      record "$name" PASS "$cmd && $OUT/$name.bin" "$rdiag" "" "$rcode"
    else
      record "$name" RUNTIME_FAILURE "$cmd && $OUT/$name.bin" "rc=$rcode want=$wantcode diag=$rdiag want_diag=$expected" "" "$rcode"
    fi
    return
  fi
  # rejection tests
  if [ "$code" -ne 0 ]; then
    if [ -n "$expected" ] && ! printf '%s' "$diag" | grep -Fq -- "$expected"; then
      record "$name" DIAGNOSTIC_MISMATCH "$cmd" "$diag" "expected=$expected" "$code"
    else
      record "$name" PASS "$cmd" "$diag" "" "$code"
    fi
  else
    record "$name" FAIL "$cmd" "compiled (expected reject)" "$diag" "$code"
  fi
}

OUT="$ROOT/tests/out/agent1"
mkdir -p "$OUT"

if want_category compile_fail; then
  for src in "$ROOT"/tests/compile_fail/*.c "$ROOT"/tests/compile_fail/*.cp; do
    [ -e "$src" ] || continue
    run_source "cf_$(basename "${src%.*}")" "$src" compile_fail
  done
fi
if want_category positive; then
  for src in "$ROOT"/tests/cases/*.c "$ROOT"/tests/cases/*.cp; do
    [ -e "$src" ] || continue
    run_source "pos_$(basename "${src%.*}")" "$src" positive
  done
fi
if want_category runtime_fail; then
  for src in "$ROOT"/tests/runtime_fail/*.c "$ROOT"/tests/runtime_fail/*.cp; do
    [ -e "$src" ] || continue
    run_source "rt_$(basename "${src%.*}")" "$src" runtime_fail
  done
fi
if want_category native; then
  for harness in cplus_safety_tests strict40_runner; do
    bin="$OUT/$harness"
    if $CPLUS build --legacy-c "$ROOT/tests/native/$harness.c" -o "$bin" >"$RES/logs/$harness.build.stdout" 2>"$RES/logs/$harness.build.stderr"; then
      if [ "$harness" = strict40_runner ]; then
        args="$ROOT/tests/fixtures/strict40"
      else
        args=""
      fi
      if [ -n "$ONE" ] && [ "$ONE" != "$harness" ]; then continue; fi
      run_timed "$TIMEOUT" env CPLUS_BIN="$CPLUS" "$bin" $args >"$RES/logs/$harness.stdout" 2>"$RES/logs/$harness.stderr"
      code=$?
      if [ "$code" -eq 0 ]; then
        record "$harness" PASS "$bin $args" "$(tail -1 "$RES/logs/$harness.stdout")" "" "$code"
      else
        record "$harness" FAIL "$bin $args" "$(tail -3 "$RES/logs/$harness.stdout")" "$(tail -2 "$RES/logs/$harness.stderr")" "$code"
      fi
    else
      record "$harness" BUILD_FAILURE "$CPLUS build --legacy-c $harness.c" "" "$(cat "$RES/logs/$harness.build.stderr")" 1
    fi
  done
fi
if want_category stress; then
  for runner in "$ROOT/tests/cp_breaker.sh" "$ROOT/tests/stress/cplus_breaker_v3/run.sh"; do
    [ -f "$runner" ] || continue
    name="stress_$(basename "$(dirname "$runner")")"
    if [ -n "$ONE" ] && [ "$ONE" != "$name" ]; then continue; fi
    run_timed "$TIMEOUT" sh "$runner" >"$RES/logs/$name.stdout" 2>"$RES/logs/$name.stderr"
    code=$?
    if [ "$code" -eq 0 ]; then record "$name" PASS "sh $runner" "$(tail -1 "$RES/logs/$name.stdout")" "" "$code"
    else record "$name" FAIL "sh $runner" "$(tail -3 "$RES/logs/$name.stdout")" "$(tail -3 "$RES/logs/$name.stderr")" "$code"; fi
  done
fi
if want_category strict_positive; then
  for src in "$ROOT"/tests/core/positive/*.c; do
    [ -e "$src" ] || continue
    b=$(basename "$src")
    case "$b" in
      multi_tu_helper.c|multi_tu_resource_a.c)
        record "sp_$(basename "${src%.*}")" SKIPPED "helper TU (linked via main)" "" "helper TU" 0
        continue ;;
      uaf_conditional.c)
        record "sp_uaf_conditional" SKIPPED "documented possible-UAF, not in strict safe subset" "" "excluded by contract" 0
        continue ;;
    esac
    name="sp_$(basename "${src%.*}")"
    if [ -n "$ONE" ] && [ "$ONE" != "$name" ]; then continue; fi
    gen="$RES/generated/$name.c"
    out="$RES/logs/$name.sp.stdout"; err="$RES/logs/$name.sp.stderr"
    case "$b" in
      multi_tu_main.c) srcs="$src $ROOT/tests/core/positive/multi_tu_helper.c" ;;
      multi_tu_resource_main.c) srcs="$src $ROOT/tests/core/positive/multi_tu_resource_a.c -I$ROOT/tests/core/positive" ;;
      *) srcs="$src" ;;
    esac
    cmd="$CPLUS --strict --sanitize=none --no-werror --emit-c \"$gen\" $srcs -o \"$OUT/$name.sp.bin\""
    run_timed "$TIMEOUT" $CPLUS --strict --sanitize=none --no-werror --emit-c "$gen" $srcs -o "$OUT/$name.sp.bin" >"$out" 2>"$err"
    code=$?
    if [ "$code" -eq 0 ]; then
      # quarantine_probe is a deliberate use-after-free probe. Run it with
      # CPLUS_QUARANTINE=1 (as tests/core/run.sh does) so the stale alias is
      # caught and the correct result is the dangling-pointer exit (105).
      # Without quarantine, allocator address reuse can make the stale alias
      # bit-identical to a live pointer — a documented, still-open limitation.
      case "$b" in
        quarantine_probe.c)
          run_timed "$TIMEOUT" env CPLUS_QUARANTINE=1 "$OUT/$name.sp.bin" >"$out.run" 2>"$err.run"
          rcode=$?; expected_rcode=105 ;;
        *)
          run_timed "$TIMEOUT" "$OUT/$name.sp.bin" >"$out.run" 2>"$err.run"
          rcode=$?; expected_rcode=0 ;;
      esac
      if [ "$rcode" -eq "$expected_rcode" ]; then record "$name" PASS "$cmd" "$(cat "$out.run")" "" "$rcode"
      else record "$name" RUNTIME_FAILURE "$cmd && run (expected rc=$expected_rcode)" "rc=$rcode" "$(cat "$err.run")" "$rcode"; fi
    else
      record "$name" BUILD_FAILURE "$cmd" "" "$(head -3 "$err")" "$code"
    fi
  done
fi
if want_category regression; then
  runner="$ROOT/tests/compiler/agent1/regression_issues198_201.sh"
  if [ -n "$ONE" ] && [ "$ONE" != "regression_issues198_201" ]; then :; else
    run_timed "$TIMEOUT" env CPLUS_BUILD="${CPLUS_BUILD:-$ROOT/build-local}" \
      CPLUS_BIN="$CPLUS" sh "$runner" >"$RES/logs/regression198201.stdout" \
      2>"$RES/logs/regression198201.stderr"
    code=$?
    if [ "$code" -eq 0 ]; then
      record regression_issues198_201 PASS "sh $runner" \
        "$(tail -1 "$RES/logs/regression198201.stdout")" "" "$code"
    else
      record regression_issues198_201 FAIL "sh $runner" \
        "$(tail -3 "$RES/logs/regression198201.stdout")" \
        "$(tail -3 "$RES/logs/regression198201.stderr")" "$code"
    fi
  fi
fi
if want_category core; then
  runner="$ROOT/tests/core/run.sh"
  if [ -n "$ONE" ] && [ "$ONE" != core ]; then :; else
    run_timed 1800 env CPLUS_BIN="$CPLUS" sh "$runner" >"$RES/logs/core.stdout" 2>"$RES/logs/core.stderr"
    code=$?
    if [ "$code" -eq 0 ]; then record core PASS "sh tests/core/run.sh" "$(tail -1 "$RES/logs/core.stdout")" "" "$code"
    else record core FAIL "sh tests/core/run.sh" "$(tail -3 "$RES/logs/core.stdout")" "$(tail -3 "$RES/logs/core.stderr")" "$code"; fi
  fi
fi

if want_category legacy; then
  for src in "$ROOT"/tests/legacy/*.c "$ROOT"/tests/compile_fail/classic_bug1_oob.c; do
    [ -e "$src" ] || continue
    name="legacy_$(basename "${src%.*}")"
    if [ -n "$ONE" ] && [ "$ONE" != "$name" ]; then continue; fi
    out="$RES/logs/$name.legacy.stdout"; err="$RES/logs/$name.legacy.stderr"
    cmd="$CPLUS --legacy-c $src -o $OUT/$name.bin"
    run_timed "$TIMEOUT" $CPLUS --legacy-c "$src" -o "$OUT/$name.bin" >"$out" 2>"$err"
    code=$?
    if [ "$code" -eq 0 ]; then record "$name" PASS "$cmd" "legacy build accepted" "" "$code"
    else record "$name" FAIL "$cmd" "" "$(head -3 "$err")" "$code"; fi
  done
fi
if want_category sanitizer; then
  for san in none address undefined address,undefined; do
    name="san_$san"
    if [ -n "$ONE" ] && [ "$ONE" != "$name" ]; then continue; fi
    src="$ROOT/tests/core/positive/safe_memory.c"
    out="$RES/logs/$name.stdout"; err="$RES/logs/$name.stderr"
    san_env=""
    san_mode="$MODEFLAG"
    case "$san" in
      # Issue #202: STRICT deliberately ignores the CPLUS_CLANG environment
      # override, so a backend-specific ASan probe cannot select Homebrew
      # clang there. Validate ASan support in DEFAULT mode instead; the strict
      # analysis path is covered by the other categories and run_tests.sh.
      *address*)
        san_env="CPLUS_CLANG=/opt/homebrew/opt/llvm/bin/clang"
        san_mode="--default" ;;
    esac
    cmd="$san_env $CPLUS $san_mode --sanitize=$san $src -o $OUT/$name.bin"
    if [ -n "$san_env" ]; then
      run_timed "$TIMEOUT" env CPLUS_CLANG=/opt/homebrew/opt/llvm/bin/clang $CPLUS $san_mode --sanitize=$san "$src" -o "$OUT/$name.bin" -- -isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk >"$out" 2>"$err"
    else
      run_timed "$TIMEOUT" $CPLUS $san_mode --sanitize=$san "$src" -o "$OUT/$name.bin" >"$out" 2>"$err"
    fi
    code=$?
    # A host whose selected backend cannot run a sanitizer probe reports
    # "sanitizer ... is not supported"; that is an environmental blocker, not
    # a compiler regression. Record it as a skip with the exact diagnostic.
    if [ "$code" -ne 0 ] && grep -q 'sanitizer .* is not supported by the selected backend' "$err"; then
      record "$name" SKIP "$cmd" "backend unsupported" "$(head -2 "$err")" "$code"
      continue
    fi
    if [ "$code" -eq 0 ]; then record "$name" PASS "$cmd" "build ok" "" "$code"
    else record "$name" FAIL "$cmd" "" "$(head -3 "$err")" "$code"; fi
  done
fi

printf '  "total": %d,\n  "pass": %d,\n  "fail": %d,\n  "skip": %d\n}\n' "$TOTAL" "$PASS" "$FAIL" "$SKIP" >> "$RES/summary.json"
printf '\nTOTAL %d  PASS %d  FAIL %d  SKIP %d\n' "$TOTAL" "$PASS" "$FAIL" "$SKIP" >> "$RES/summary.txt"
echo "TOTAL $TOTAL  PASS $PASS  FAIL $FAIL  SKIP $SKIP"
[ "$FAIL" -eq 0 ]
