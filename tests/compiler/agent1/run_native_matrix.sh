#!/usr/bin/env bash
# Native C+ safety-check regression matrix (agent1, owned files only).
#
# For every case we run exactly four configurations:
#   1. check --default --sanitize=none
#   2. check --strict  --sanitize=none
#   3. build --default --sanitize=none + execute
#   4. build --strict  --sanitize=undefined  (+ CPLUS_CLANG and -isysroot)
#
# Every cell records the exact rc, stdout, stderr, generated C and a
# PASS/FAIL/BLOCKED classification with an explicit reason.  PASS is only
# granted when an asserted diagnostic substring is found or an exact expected
# exit code is observed.  No failure suppression, no silent failures, all
# subprocesses are time-limited.
#
# Usage:
#   CPLUS_BIN=$PWD/build/c+ sh tests/compiler/agent1/run_native_matrix.sh
#   ... --timeout 90 --case b_runtime_signed_overflow
#
# Results: tests/compiler/agent1/native_matrix/results/

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS="${CPLUS_BIN:-$ROOT/build/c+}"
BASE="$ROOT/tests/compiler/agent1/native_matrix"
CASE_DIR="$BASE/cases"
INPUT_DIR="$BASE/inputs"
RES="$BASE/results"
LOGS="$RES/logs"
GEN="$RES/generated"
BINS="$RES/bins"
CELLS="$RES/cells"

CASES="a_array_decay_bounds b_runtime_signed_overflow c_runtime_div_mod_zero d_invalid_shift_count e_narrowing_conversion f_pointer_arithmetic_beyond_array"
CONFIGS="check_default check_strict build_default build_ubsan"

UBSAN_CLANG=/opt/homebrew/opt/llvm/bin/clang
SYSROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk
TIMEOUT=60
ONLY=""

usage() {
  cat <<EOF
Usage: run_native_matrix.sh [options]
  --timeout SECONDS  per-command timeout (default 60)
  --case NAME        run only one case id
  --list             list case ids and exit
  --help             this message
Environment:
  CPLUS_BIN          compiler under test (default \$ROOT/build/c+)
EOF
}

while [ $# -gt 0 ]; do
  case "$1" in
    --timeout) TIMEOUT="${2:-60}"; shift 2 ;;
    --case) ONLY="${2:-}"; shift 2 ;;
    --list) echo "$CASES" | tr ' ' '\n'; exit 0 ;;
    --help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage >&2; exit 2 ;;
  esac
done

mkdir -p "$LOGS" "$GEN" "$BINS" "$CELLS"

case "$TIMEOUT" in
  ''|*[!0-9]*) echo "--timeout must be an integer" >&2; exit 2 ;;
esac

# ---------------------------------------------------------------------------
# Portable timeout + exact-rc capture.  No output is swallowed by design:
# stdout/stderr go straight into per-cell files owned by the runner.
# ---------------------------------------------------------------------------
run_timed() {
  secs=$1; shift
  if command -v timeout >/dev/null 2>&1; then
    timeout -k 2 "$secs" "$@"
  elif command -v perl >/dev/null 2>&1; then
    perl -e '$s=shift @ARGV; alarm $s; exec @ARGV; alarm 0' "$secs" "$@"
  else
    "$@"
  fi
}

is_timeout_rc() {
  case "$1" in
    124|142) return 0 ;;
    *) return 1 ;;
  esac
}

# All `|`-separated substrings must occur in the supplied files.  The file
# list is passed as one quoted argument and split back here.
diags_present() {
  pattern=$1; shift
  files="$*"
  [ -n "$pattern" ] || return 0
  [ -n "$files" ] || return 1
  oldifs=$IFS; IFS='|'
  set -- $pattern
  IFS=$oldifs
  for needle in "$@"; do
    [ -n "$needle" ] || continue
    if grep -qF -- "$needle" $files 2>/dev/null; then :; else return 1; fi
  done
  return 0
}

json_escape() {
  printf '%s' "$1" | tr '\n\r' '  ' | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g'
}

# ---------------------------------------------------------------------------
# Environment record (compiler mtime is re-checked at start and at the end).
# ---------------------------------------------------------------------------
MT_START=$(stat -f '%m' "$CPLUS" 2>/dev/null)
MT_START_H=$(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "$CPLUS" 2>/dev/null)
SHA_START="unavailable"
if command -v shasum >/dev/null 2>&1; then
  SHA_START=$(shasum -a 256 "$CPLUS" 2>/dev/null | awk '{print $1}')
fi

{
  echo "run_at: $(date '+%Y-%m-%d %H:%M:%S %Z')"
  echo "root: $ROOT"
  echo "compiler: $CPLUS"
  echo "compiler_mtime_start: $MT_START_H (epoch $MT_START)"
  echo "compiler_sha256_start: $SHA_START"
  echo "compiler_check_note: mtime re-read at start; if the compiler is being rebuilt concurrently, mtime_end will differ and a warning is appended"
  echo "timeout_seconds: $TIMEOUT"
  echo "ubsan_clang: $UBSAN_CLANG"
  echo "ubsan_clang_executable: $([ -x "$UBSAN_CLANG" ] && echo yes || echo no)"
  echo "sysroot: $SYSROOT"
  echo "sysroot_exists: $([ -d "$SYSROOT" ] && echo yes || echo no)"
  echo "version_output:"
  run_timed 20 "$CPLUS" --version 2>&1 | sed 's/^/  /'
  echo "sanitizer_info:"
  run_timed 20 "$CPLUS" --print-sanitizers 2>&1 | sed 's/^/  /'
} > "$RES/environment.txt"

cat "$RES/environment.txt"

if [ ! -x "$CPLUS" ]; then
  echo "BLOCKED: compiler is not executable: $CPLUS" >&2
  printf 'BLOCKED all-cells reason=compiler binary missing or not executable\n' > "$RES/summary.txt"
  exit 2
fi

UBSAN_READY=1
if [ ! -x "$UBSAN_CLANG" ] || [ ! -d "$SYSROOT" ]; then
  UBSAN_READY=0
fi

# ---------------------------------------------------------------------------
# Expectations.  A PASS always requires either an asserted diagnostic substring
# or an exact expected exit code (plus generated-C evidence where relevant).
# ---------------------------------------------------------------------------
set_expectations() {
  cid=$1; cfg=$2
  REASON=""
  EXP_COMPILE="accept"       # accept | reject | either
  EXP_DIAG=""                # all '|'-separated substrings must be present
  EXP_REJECT_DIAG=""         # diagnostic required if an 'either' cell rejects
  EXP_GEN_SUBSTR=""          # substring required in the emitted C
  EXP_RUN=""                 # "" | trap
  EXP_RUN_RC=""              # exact expected rc for run 1
  EXP_RUN_DIAG=""            # diagnostic required in run 1 stderr
  EXP_RUN2_RC=""             # exact expected rc for run 2
  EXP_RUN2_DIAG=""

  case "$cid" in
    a_array_decay_bounds)
      REASON="proven OOB p[4] on decayed pointer with recorded extent 4 must be rejected statically"
      EXP_COMPILE=reject
      EXP_DIAG='CPLUS-BOUNDS|array index 4'
      ;;
    b_runtime_signed_overflow)
      REASON="runtime operand 2147483647 + 1 must trap (native 110 or UBSan 134), never wrap to -2147483648"
      EXP_GEN_SUBSTR='x + 1'
      case "$cfg" in
        check_default|check_strict)
          EXP_COMPILE=accept ;;
        build_default)
          EXP_COMPILE=either
          EXP_REJECT_DIAG='overflow'
          EXP_RUN=trap; EXP_RUN_RC=110; EXP_RUN_DIAG='overflow' ;;
        build_ubsan)
          EXP_COMPILE=either
          EXP_REJECT_DIAG='overflow'
          EXP_RUN=trap; EXP_RUN_RC=134; EXP_RUN_DIAG='signed integer overflow' ;;
      esac
      ;;
    c_runtime_div_mod_zero)
      REASON="zero divisor/op must trap for both / and % (native 110 or UBSan 134), never return 0/dividend"
      EXP_GEN_SUBSTR='checked_div'
      case "$cfg" in
        check_default)
          EXP_COMPILE=accept; EXP_GEN_SUBSTR='' ;;
        check_strict)
          EXP_COMPILE=reject; EXP_REJECT_DIAG='zero'; EXP_GEN_SUBSTR='' ;;
        build_default)
          EXP_COMPILE=either
          EXP_REJECT_DIAG='zero'
          EXP_RUN=trap; EXP_RUN_RC=110; EXP_RUN_DIAG='zero'
          EXP_RUN2_RC=110; EXP_RUN2_DIAG='zero' ;;
        build_ubsan)
          EXP_COMPILE=either
          EXP_REJECT_DIAG='zero'
          EXP_RUN=trap; EXP_RUN_RC=134; EXP_RUN_DIAG='zero'
          EXP_RUN2_RC=134; EXP_RUN2_DIAG='zero' ;;
      esac
      ;;
    d_invalid_shift_count)
      REASON="shift by -1 and by 32 (width 32) must both trap (native 110 or UBSan 134)"
      EXP_GEN_SUBSTR='checked_shl'
      case "$cfg" in
        check_default)
          EXP_COMPILE=accept; EXP_GEN_SUBSTR='' ;;
        check_strict)
          EXP_COMPILE=reject; EXP_REJECT_DIAG='shift'; EXP_GEN_SUBSTR='' ;;
        build_default)
          EXP_COMPILE=accept
          EXP_RUN=trap; EXP_RUN_RC=110; EXP_RUN_DIAG='shift'
          EXP_RUN2_RC=110; EXP_RUN2_DIAG='shift' ;;
        build_ubsan)
          EXP_COMPILE=either
          EXP_REJECT_DIAG='shift'
          EXP_RUN=trap; EXP_RUN_RC=134; EXP_RUN_DIAG='shift exponent'
          EXP_RUN2_RC=134; EXP_RUN2_DIAG='shift exponent' ;;
      esac
      ;;
    e_narrowing_conversion)
      REASON="implicit narrowing 300 -> char changes the value: DEFAULT must warn (rc 0), STRICT must reject (rc != 0)"
      EXP_DIAG='changes value'
      case "$cfg" in
        check_default) EXP_COMPILE=accept ;;
        build_default)
          EXP_COMPILE=accept
          EXP_RUN=accept; EXP_RUN_RC=0 ;;
        check_strict|build_ubsan) EXP_COMPILE=reject ;;
      esac
      ;;
    f_pointer_arithmetic_beyond_array)
      REASON="a + 5 exceeds one-past-the-end of a 4-element array and must be rejected statically"
      EXP_COMPILE=reject
      EXP_DIAG='pointer arithmetic more than one past'
      ;;
  esac
}

# First run inputs for cases with two runtime probes.
second_input() {
  case "$1" in
    c_runtime_div_mod_zero) echo "$INPUT_DIR/c_mod_zero.txt" ;;
    d_invalid_shift_count) echo "$INPUT_DIR/d_shift_wide.txt" ;;
    *) echo "" ;;
  esac
}
first_input() {
  case "$1" in
    b_runtime_signed_overflow) echo "$INPUT_DIR/b_overflow.txt" ;;
    c_runtime_div_mod_zero) echo "$INPUT_DIR/c_div_zero.txt" ;;
    d_invalid_shift_count) echo "$INPUT_DIR/d_shift_negative.txt" ;;
    e_narrowing_conversion) echo "$INPUT_DIR/empty.txt" ;;
    *) echo "" ;;
  esac
}

# ---------------------------------------------------------------------------
# Result sinks.
# ---------------------------------------------------------------------------
: > "$RES/summary.txt"
printf '{\n  "compiler": "%s",\n  "compiler_mtime_start": "%s",\n  "results": [\n' \
  "$(json_escape "$CPLUS")" "$(json_escape "$MT_START_H (epoch $MT_START)")" > "$RES/summary.json"

PASS=0; FAIL=0; BLOCKED=0; TOTAL=0

record() {
  cid=$1; cfg=$2; status=$3; reason=$4
  crec=$5; r1=$6; r2=$7
  TOTAL=$((TOTAL+1))
  case "$status" in
    PASS) PASS=$((PASS+1)) ;;
    FAIL) FAIL=$((FAIL+1)) ;;
    BLOCKED) BLOCKED=$((BLOCKED+1)) ;;
  esac
  printf '%-8s %-34s %-14s compile_rc=%-4s run_rc=%s,%s reason=%s\n' \
    "$status" "$cid" "$cfg" "$crec" "$r1" "$r2" "$reason" >> "$RES/summary.txt"
  if [ "$TOTAL" -gt 1 ]; then printf ',\n' >> "$RES/summary.json"; fi
  printf '    {"case":"%s","config":"%s","status":"%s","compile_rc":"%s","run_rc1":"%s","run_rc2":"%s","reason":"%s"}' \
    "$(json_escape "$cid")" "$(json_escape "$cfg")" "$status" "$crec" "$r1" "$r2" \
    "$(json_escape "$reason")" >> "$RES/summary.json"
}

write_cell() {
  f="$1"; shift
  {
    while [ $# -gt 0 ]; do
      printf '%s\n' "$1"
      shift
    done
  } > "$f"
}

# ---------------------------------------------------------------------------
# Main matrix.
# ---------------------------------------------------------------------------
for cid in $CASES; do
  if [ -n "$ONLY" ] && [ "$cid" != "$ONLY" ]; then continue; fi
  src="$CASE_DIR/$cid.c"
  if [ ! -r "$src" ]; then
    echo "BLOCKED $cid: missing fixture $src" >&2
    for cfg in $CONFIGS; do
      record "$cid" "$cfg" BLOCKED "fixture $src is missing or unreadable" NA NA NA
    done
    continue
  fi

  for cfg in $CONFIGS; do
    set_expectations "$cid" "$cfg"
    tag="${cid}__${cfg}"
    gen="$GEN/$tag.c"
    bin="$BINS/$tag.bin"
    co="$LOGS/${tag}__compile.stdout"
    ce="$LOGS/${tag}__compile.stderr"
    r1o="$LOGS/${tag}__run1.stdout"; r1e="$LOGS/${tag}__run1.stderr"
    r2o="$LOGS/${tag}__run2.stdout"; r2e="$LOGS/${tag}__run2.stderr"

    rm -f "$gen" "$gen.NOT_EMITTED" "$bin" "$co" "$ce" "$r1o" "$r1e" "$r2o" "$r2e"

    case "$cfg" in
      check_default)
        cmd_args=(check --default --sanitize=none --emit-c "$gen" "$src")
        compile_cmd="$CPLUS check --default --sanitize=none --emit-c $gen $src"
        ;;
      check_strict)
        cmd_args=(check --strict --sanitize=none --emit-c "$gen" "$src")
        compile_cmd="$CPLUS check --strict --sanitize=none --emit-c $gen $src"
        ;;
      build_default)
        cmd_args=(build --default --sanitize=none --emit-c "$gen" "$src" -o "$bin")
        compile_cmd="$CPLUS build --default --sanitize=none --emit-c $gen $src -o $bin"
        ;;
      build_ubsan)
        cmd_args=(build --strict --sanitize=undefined --emit-c "$gen" "$src" -o "$bin" -- -isysroot "$SYSROOT")
        compile_cmd="env CPLUS_CLANG=$UBSAN_CLANG $CPLUS build --strict --sanitize=undefined --emit-c $gen $src -o $bin -- -isysroot $SYSROOT"
        ;;
    esac

    if [ "$cfg" = build_ubsan ]; then
      run_timed "$TIMEOUT" env "CPLUS_CLANG=$UBSAN_CLANG" "$CPLUS" "${cmd_args[@]}" >"$co" 2>"$ce"
      compile_rc=$?
    else
      run_timed "$TIMEOUT" "$CPLUS" "${cmd_args[@]}" >"$co" 2>"$ce"
      compile_rc=$?
    fi

    if [ -s "$gen" ]; then
      gen_ref="$gen"
      wrapper=$(grep -c '__cplus_inline_' "$gen" 2>/dev/null)
    else
      gen_ref="$gen.NOT_EMITTED"
      printf 'generated C not emitted (compile rc=%s)\n' "$compile_rc" > "$gen_ref"
      wrapper=0
    fi

    r1_rc=NA; r2_rc=NA
    status=""; why=""

    if is_timeout_rc "$compile_rc"; then
      status=BLOCKED
      why="compile timed out after ${TIMEOUT}s; contract: $REASON"
    elif [ "$cfg" = build_ubsan ] && [ "$UBSAN_READY" -eq 0 ]; then
      status=BLOCKED
      why="UBSan prerequisite missing: CPLUS_CLANG=$UBSAN_CLANG executable or SDK sysroot $SYSROOT absent; contract: $REASON"
    elif [ "$compile_rc" -ne 0 ] && grep -qF "sanitizer 'undefined' is not supported" "$ce"; then
      status=BLOCKED
      why="compiler's UBSan compile-and-run probe rejected the selected backend; contract: $REASON"
    else
      # ---- compile-stage verdict ----
      compile_verdict=0
      reject_diag=''
      if [ "$EXP_COMPILE" = reject ]; then
        if [ "$compile_rc" -ne 0 ] && diags_present "$EXP_DIAG" "$co $ce"; then
          compile_verdict=1
          why="PASS: rejected with asserted diagnostic [$EXP_DIAG]; $REASON"
        else
          why="FAIL: expected compile rejection with [$EXP_DIAG], got rc=$compile_rc; $REASON"
        fi
      elif [ "$EXP_COMPILE" = accept ]; then
        if [ "$compile_rc" -eq 0 ] && diags_present "$EXP_DIAG" "$co $ce" &&
           [ -s "$gen" ] && { [ -z "$EXP_GEN_SUBSTR" ] || grep -qF -- "$EXP_GEN_SUBSTR" "$gen"; }; then
          compile_verdict=1
          why="PASS: rc=0 with asserted diagnostic [$EXP_DIAG] and generated-C token [$EXP_GEN_SUBSTR]; $REASON"
        else
          why="FAIL: expected rc=0 with [$EXP_DIAG] and generated-C token [$EXP_GEN_SUBSTR], got rc=$compile_rc gen=$gen_ref; $REASON"
        fi
      else # either
        if [ "$compile_rc" -eq 0 ] && [ -s "$gen" ] &&
           { [ -z "$EXP_GEN_SUBSTR" ] || grep -qF -- "$EXP_GEN_SUBSTR" "$gen"; }; then
          compile_verdict=1   # will still require the runtime trap below
          why="PASS: clean compile with generated-C token [$EXP_GEN_SUBSTR]; $REASON"
        elif [ "$compile_rc" -ne 0 ] && diags_present "$EXP_REJECT_DIAG" "$co $ce"; then
          compile_verdict=1
          why="PASS: compile-time rejection with asserted diagnostic [$EXP_REJECT_DIAG]; $REASON"
        else
          why="FAIL: expected clean compile with generated C or rejection with [$EXP_REJECT_DIAG], got rc=$compile_rc; $REASON"
        fi
      fi

      # ---- runtime stage (only for build configs that compiled) ----
      if [ "$compile_verdict" -eq 1 ] && [ "$compile_rc" -eq 0 ] && [ -n "$EXP_RUN" ]; then
        if [ ! -x "$bin" ]; then
          compile_verdict=0
          why="FAIL: compile rc=0 but no executable at $bin; $REASON"
        else
          in1=$(first_input "$cid")
          in2=$(second_input "$cid")
          if [ -z "$in1" ] || [ ! -r "$in1" ]; then
            compile_verdict=0
            why="FAIL: missing run input $in1; $REASON"
          else
            run_timed 30 env UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0 \
              "$bin" < "$in1" >"$r1o" 2>"$r1e"
            r1_rc=$?
            run1_ok=0
            if is_timeout_rc "$r1_rc"; then
              status=BLOCKED
              why="run1 timed out after 30s; $REASON"
            elif [ "$EXP_RUN" = trap ]; then
              if [ "$r1_rc" -eq "$EXP_RUN_RC" ] && diags_present "$EXP_RUN_DIAG" "$r1e"; then
                run1_ok=1
              fi
            elif [ "$r1_rc" -eq "$EXP_RUN_RC" ]; then
              run1_ok=1   # deterministic exit code is the assertion
            fi

            run2_ok=0
            if [ -z "$status" ] && [ -n "$in2" ] && [ -r "$in2" ]; then
              run_timed 30 env UBSAN_OPTIONS=halt_on_error=1 ASAN_OPTIONS=detect_leaks=0 \
                "$bin" < "$in2" >"$r2o" 2>"$r2e"
              r2_rc=$?
              if is_timeout_rc "$r2_rc"; then
                status=BLOCKED
                why="run2 timed out after 30s; $REASON"
              elif [ "$EXP_RUN" = trap ]; then
                if [ "$r2_rc" -eq "$EXP_RUN2_RC" ] && diags_present "$EXP_RUN2_DIAG" "$r2e"; then
                  run2_ok=1
                fi
              elif [ "$r2_rc" -eq "$EXP_RUN2_RC" ]; then
                run2_ok=1
              fi
            fi

            if [ -z "$status" ]; then
              if [ "$run1_ok" -eq 1 ] && { [ -z "$in2" ] || [ "$run2_ok" -eq 1 ]; }; then
                if [ "$EXP_RUN" = trap ]; then
                  why="PASS: run rc=$r1_rc/$r2_rc with asserted diagnostic [$EXP_RUN_DIAG]/[$EXP_RUN2_DIAG] and generated-C token [$EXP_GEN_SUBSTR]; $REASON"
                else
                  why="PASS: deterministic exit rc=$r1_rc with asserted compile diagnostic [$EXP_DIAG] and generated-C token [$EXP_GEN_SUBSTR]; $REASON"
                fi
              else
                compile_verdict=0
                why="FAIL: expected run rc=$EXP_RUN_RC/$EXP_RUN2_RC with [$EXP_RUN_DIAG]/[$EXP_RUN2_DIAG], got rc=$r1_rc/$r2_rc stdout=$(head -c 120 "$r1o") stderr=$(head -c 160 "$r1e"); $REASON"
              fi
            fi
          fi
        fi
      fi

      if [ -z "$status" ]; then
        if [ "$compile_verdict" -eq 1 ]; then status=PASS; else status=FAIL; fi
      fi
    fi

    record "$cid" "$cfg" "$status" "$why" "$compile_rc" "$r1_rc" "$r2_rc"

    write_cell "$CELLS/$tag.txt" \
      "CASE=$cid" \
      "CONFIG=$cfg" \
      "STATUS=$status" \
      "REASON=$why" \
      "EXPECT_COMPILE=$EXP_COMPILE" \
      "EXPECT_DIAG=$EXP_DIAG" \
      "EXPECT_REJECT_DIAG=$EXP_REJECT_DIAG" \
      "EXPECT_GEN_SUBSTR=$EXP_GEN_SUBSTR" \
      "EXPECT_RUN=$EXP_RUN" \
      "EXPECT_RUN_RC=$EXP_RUN_RC" \
      "EXPECT_RUN_DIAG=$EXP_RUN_DIAG" \
      "EXPECT_RUN2_RC=$EXP_RUN2_RC" \
      "EXPECT_RUN2_DIAG=$EXP_RUN2_DIAG" \
      "COMPILE_CMD=$compile_cmd" \
      "COMPILE_RC=$compile_rc" \
      "COMPILE_STDOUT=$co" \
      "COMPILE_STDERR=$ce" \
      "GENERATED_C=$gen_ref" \
      "NATIVE_GUARD_WRAPPER_COUNT=$wrapper" \
      "RUN1_RC=$r1_rc" \
      "RUN1_STDOUT=$r1o" \
      "RUN1_STDERR=$r1e" \
      "RUN2_RC=$r2_rc" \
      "RUN2_STDOUT=$r2o" \
      "RUN2_STDERR=$r2e"
  done
done

printf '\n  ],\n  "total": %d,\n  "pass": %d,\n  "fail": %d,\n  "blocked": %d\n}\n' \
  "$TOTAL" "$PASS" "$FAIL" "$BLOCKED" >> "$RES/summary.json"

MT_END=$(stat -f '%m' "$CPLUS" 2>/dev/null)
MT_END_H=$(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "$CPLUS" 2>/dev/null)
{
  echo "compiler_mtime_end: $MT_END_H (epoch $MT_END)"
  if [ "$MT_END" != "$MT_START" ]; then
    echo "WARNING: compiler mtime changed during this run ($MT_START -> $MT_END); results may mix binary revisions"
  else
    echo "compiler_mtime_note: unchanged during this run"
  fi
} >> "$RES/environment.txt"

printf '\nTOTAL %d  PASS %d  FAIL %d  BLOCKED %d\n' "$TOTAL" "$PASS" "$FAIL" "$BLOCKED" >> "$RES/summary.txt"
echo "TOTAL $TOTAL  PASS $PASS  FAIL $FAIL  BLOCKED $BLOCKED"
if [ "$MT_END" != "$MT_START" ]; then
  echo "WARNING: compiler mtime changed during this run ($MT_START -> $MT_END)"
fi
[ "$FAIL" -eq 0 ] && [ "$BLOCKED" -eq 0 ]
