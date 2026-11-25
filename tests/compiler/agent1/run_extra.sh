#!/usr/bin/env bash
# Extra assertion runner for the 111 safety fixtures that have no active
# assertion runner (see tests/compiler/agent1/test_inventory.md):
#   - 11 top-level tests/safety/*.c metric-only fixtures (explicit list below)
#   - 100 fixtures under tests/safety/{fnptr_bounds,integration,p0new,
#     runtime_crashes,sanitizer,rust_compare}
#
# Assertions are taken from the fixtures themselves:
#   - // EXPECT: accept|reject   (p0new generator contract) -> verdict
#   - // DIAG: <text>            (p0new generator contract) -> diagnostic substring
#   - <name>.err                 expected compile diagnostic substring
#   - <name>.exit / <name>.stderr expected runtime exit code / stderr substring
# STRICT is the authoritative mode for embedded EXPECT/DIAG markers (the
# generators state "checked in isolation with c+ check --strict"); DEFAULT is
# still executed and recorded as evidence. A fixture with NO pinned
# expectation is never silently passed: it is recorded as BLOCKED with the
# exact reason and remains visible in both summaries.
#
# No Python. No compiler rebuild. No edits to existing test files.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../../.." && pwd)
CPLUS="${CPLUS_BIN:-$ROOT/build/c+}"
RES="$ROOT/tests/compiler/agent1/results_extra"
LOGDIR="$RES/logs"
OUTDIR="$RES/bin"
mkdir -p "$LOGDIR" "$OUTDIR"
TIMEOUT=30
ONE=""

usage() {
  cat <<EOF
Usage: run_extra.sh [options]
  --timeout SECONDS  per-invocation timeout (default 30)
  --test NAME        run one fixture (basename without extension)
  --help
Environment:
  CPLUS_BIN          path to c+ (default: \$ROOT/build/c+)
Results: $RES/summary.txt, $RES/summary.json, logs in $RES/logs/
EOF
}
while [ $# -gt 0 ]; do
  case "$1" in
    --timeout) TIMEOUT="${2:-30}"; shift 2 ;;
    --test) ONE="${2:-}"; shift 2 ;;
    --help) usage; exit 0 ;;
    *) echo "unknown option: $1" >&2; usage; exit 2 ;;
  esac
done

# Portable timeout (GNU timeout, else Perl alarm, else no timeout).
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

json_escape() {
  printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' | tr '\r\n\t' '   '
}

SUMMARY="$RES/summary.txt"
JSONL="$RES/.records.jsonl"
: > "$SUMMARY"
: > "$JSONL"

N_PASS=0; N_FAIL=0; N_DIAG=0; N_BUILD=0; N_RT=0; N_TIMEOUT=0
N_BLOCKED=0; N_SKIP=0; TOTAL=0

record() {
  # name status reason cmd strict_rc default_rc diag category source
  name=$1; status=$2; reason=$3; cmd=$4; src_rc=$5; def_rc=$6; diag=$7; cat=$8; source=$9
  TOTAL=$((TOTAL+1))
  case "$status" in
    PASS) N_PASS=$((N_PASS+1)) ;;
    FAIL) N_FAIL=$((N_FAIL+1)) ;;
    DIAGNOSTIC_MISMATCH) N_DIAG=$((N_DIAG+1)) ;;
    BUILD_FAILURE) N_BUILD=$((N_BUILD+1)) ;;
    RUNTIME_FAILURE) N_RT=$((N_RT+1)) ;;
    TIMEOUT) N_TIMEOUT=$((N_TIMEOUT+1)) ;;
    BLOCKED) N_BLOCKED=$((N_BLOCKED+1)) ;;
    SKIPPED) N_SKIP=$((N_SKIP+1)) ;;
  esac
  printf '%-13s %-42s %-14s %s\n' "$status" "$name" "[$cat]" "$reason" >> "$SUMMARY"
  printf '{"name":"%s","status":"%s","reason":"%s","cmd":"%s","strict_rc":"%s","default_rc":"%s","diag":"%s","category":"%s","source":"%s"}\n' \
    "$(json_escape "$name")" "$(json_escape "$status")" "$(json_escape "$reason")" \
    "$(json_escape "$cmd")" "$(json_escape "$src_rc")" "$(json_escape "$def_rc")" \
    "$(json_escape "$diag")" "$(json_escape "$cat")" "$(json_escape "$source")" >> "$JSONL"
}

cat > "$RES/mode_matrix.txt" <<EOF
MODE MATRIX (extra orphaned-fixture runner)
- embedded // EXPECT: accept -> c+ check --strict must exit 0.
- embedded // EXPECT: reject -> c+ check --strict must exit non-zero and the
  // DIAG: substring must appear (if present). DEFAULT is run for evidence
  only: p0new_realloc_old_after_grow is a warning (rc=0) in DEFAULT but an
  error (rc=1) in STRICT, so STRICT is authoritative for these generators.
- <name>.err -> expected compile diagnostic substring (reject).
- <name>.exit[+.stderr] -> build then execute with timeout; assert exit code
  and optional stderr substring. No such expectation files exist in this set.
- no expectation marker/file -> BLOCKED (never PASS by default), reason recorded.
EOF

# ---------------------------------------------------------------------------
# Assertion of one fixture.
# ---------------------------------------------------------------------------
assert_fixture() {
  name=$1; src=$2; cat=$3
  [ -n "$ONE" ] && [ "$ONE" != "$name" ] && return
  [ -f "$src" ] || { record "$name" BLOCKED "source missing: $src" "-" "-" "-" "-" "$cat" "$src"; return; }

  base="${src%.*}"
  expect=$(grep -iEm1 '//[[:space:]]*EXPECT:[[:space:]]*(accept|reject)' "$src" \
           | tr '[:upper:]' '[:lower:]' | sed -E 's/.*expect:[[:space:]]*//' | tr -d '\r')
  diag=$(grep -m1 '//[[:space:]]*DIAG:' "$src" \
         | sed -E 's/.*DIAG:[[:space:]]*//' | sed 's/[[:space:]]*$//' | tr -d '\r')
  skip_mark=$(grep -m1 '//[[:space:]]*SKIP:' "$src" \
              | sed -E 's/.*SKIP:[[:space:]]*//' | tr -d '\r')
  err_file=""; exit_file=""; stderr_file=""
  [ -f "$base.err" ] && err_file="$base.err"
  [ -f "$base.exit" ] && exit_file="$base.exit"
  [ -f "$base.stderr" ] && stderr_file="$base.stderr"

  if [ -n "$skip_mark" ]; then
    record "$name" SKIPPED "explicit marker: $skip_mark" "-" "-" "-" "-" "$cat" "$src"
    return
  fi

  # No pinned expectation -> BLOCKED with an exact reason, never PASS.
  if [ -z "$expect" ] && [ -z "$err_file" ] && [ -z "$exit_file" ]; then
    suffix=""
    case "$cat" in
      top11) suffix="; top-level metric-only fixture listed in test_inventory.md" ;;
      runtime_crashes) suffix="; interactive stdin-driven crash corpus with no pinned exit code" ;;
      integration) suffix="; multi-TU fixture (int_main.c links int_helper.c) with no standalone expectation" ;;
      sanitizer) suffix="; sanitizer corpus with no pinned expected diagnostic" ;;
      fnptr_bounds) suffix="; function-pointer bounds corpus with no pinned expected diagnostic" ;;
      rust_compare) suffix="; Rust-comparison corpus with no pinned expected diagnostic" ;;
    esac
    record "$name" BLOCKED \
      "no embedded // EXPECT:/DIAG marker and no .err/.exit expectation file${suffix}" \
      "-" "-" "-" "-" "$cat" "$src"
    return
  fi

  # A .exit file means this is a runtime case: build, then execute with timeout.
  if [ -n "$exit_file" ]; then
    wantcode=$(tr -cd '0-9' < "$exit_file")
    wantstderr=""
    [ -n "$stderr_file" ] && wantstderr=$(cat "$stderr_file" | tr -d '\r')
    bin="$OUTDIR/$name.bin"
    cmd="$CPLUS --default \"$src\" -o \"$bin\" && timeout ${TIMEOUT}s \"$bin\""
    run_timed "$TIMEOUT" "$CPLUS" --default "$src" -o "$bin" \
      >"$LOGDIR/$name.build.stdout" 2>"$LOGDIR/$name.build.stderr"
    brc=$?
    if [ "$brc" -ne 0 ] || [ ! -x "$bin" ]; then
      record "$name" BUILD_FAILURE "build failed (rc=$brc)" "$cmd" "-" "-" "-" "$cat" "$src"
      return
    fi
    run_timed "$TIMEOUT" "$bin" >"$LOGDIR/$name.run.stdout" 2>"$LOGDIR/$name.run.stderr"
    rrc=$?
    gotstderr=$(cat "$LOGDIR/$name.run.stderr" 2>/dev/null)
    if [ "$rrc" -eq 124 ] || [ "$rrc" -eq 142 ]; then
      record "$name" TIMEOUT "runtime exceeded ${TIMEOUT}s" "$cmd" "-" "-" "-" "$cat" "$src"
    elif [ "$rrc" != "$wantcode" ]; then
      record "$name" RUNTIME_FAILURE "exit $rrc != expected $wantcode" "$cmd" "-" "-" "-" "$cat" "$src"
    elif [ -n "$wantstderr" ] && ! printf '%s' "$gotstderr" | grep -qF -- "$wantstderr"; then
      record "$name" RUNTIME_FAILURE "stderr missing: $wantstderr" "$cmd" "-" "-" "-" "$cat" "$src"
    else
      record "$name" PASS "exit $rrc (expected $wantcode)" "$cmd" "-" "-" "-" "$cat" "$src"
    fi
    return
  fi

  # Compile-check case. Embedded markers use STRICT as authoritative; a bare
  # .err file is checked in both modes (DEFAULT reported as evidence).
  run_timed "$TIMEOUT" "$CPLUS" check --strict "$src" \
    >"$LOGDIR/$name.strict.stdout" 2>"$LOGDIR/$name.strict.stderr"
  src_rc=$?
  run_timed "$TIMEOUT" "$CPLUS" check --default "$src" \
    >"$LOGDIR/$name.default.stdout" 2>"$LOGDIR/$name.default.stderr"
  def_rc=$?
  cmd="$CPLUS check --strict $src (default: $CPLUS check --default $src)"

  needle="$diag"
  if [ -z "$needle" ] && [ -n "$err_file" ]; then
    needle=$(grep -m1 -v '^[[:space:]]*$' "$err_file" | tr -d '\r')
  fi
  sdiag=$(cat "$LOGDIR/$name.strict.stderr" "$LOGDIR/$name.strict.stdout" 2>/dev/null)

  # Expectation: accept (rc == 0).
  if [ "$expect" = "accept" ]; then
    if [ "$src_rc" -eq 0 ]; then
      record "$name" PASS "expect=accept; strict_rc=0 default_rc=$def_rc" "$cmd" \
        "$src_rc" "$def_rc" "-" "$cat" "$src"
    else
      first=$(printf '%s' "$sdiag" | grep -E 'error:|warning:' | head -1)
      record "$name" FAIL "expect=accept but strict rejected (strict_rc=$src_rc): $first" \
        "$cmd" "$src_rc" "$def_rc" "-" "$cat" "$src"
    fi
    return
  fi

  # Expectation: reject (rc != 0), with optional diagnostic substring.
  if [ "$src_rc" -eq 0 ]; then
    record "$name" FAIL "expect=reject but strict accepted (strict_rc=0)" "$cmd" \
      "$src_rc" "$def_rc" "$needle" "$cat" "$src"
    return
  fi
  if [ -n "$needle" ] && ! printf '%s' "$sdiag" | grep -qF -- "$needle"; then
    record "$name" DIAGNOSTIC_MISMATCH "expected diagnostic '$needle' not in strict output" \
      "$cmd" "$src_rc" "$def_rc" "$needle" "$cat" "$src"
    return
  fi
  if [ -n "$needle" ]; then
    record "$name" PASS "expect=reject; diag='$needle' found; strict_rc=$src_rc default_rc=$def_rc" \
      "$cmd" "$src_rc" "$def_rc" "$needle" "$cat" "$src"
  else
    record "$name" PASS "expect=reject; strict_rc=$src_rc default_rc=$def_rc (no diag text pinned)" \
      "$cmd" "$src_rc" "$def_rc" "-" "$cat" "$src"
  fi
}

# ---------------------------------------------------------------------------
# Discovery.
# ---------------------------------------------------------------------------
# The 11 top-level fixtures named in test_inventory.md as never asserted.
TOP11="borrow_conflict format_string_nonliteral format_string_s_nonliteral \
interproc_double_free libc_double_free libc_memcpy_overflow \
libc_strcpy_overflow realloc_failure_alias_live uaf_alias \
uninit_addr_non_output uninit_compound_assign"
for n in $TOP11; do
  assert_fixture "$n" "$ROOT/tests/safety/$n.c" top11
done

# The 100 fixtures under the six orphaned subdirectories (.c and .cp only;
# .rs/.h files are not C+ compiler fixtures and are excluded).
SUBDIRS="fnptr_bounds integration p0new runtime_crashes sanitizer rust_compare"
for d in $SUBDIRS; do
  # Process substitution (not a pipeline) so counters in record() persist.
  while IFS= read -r src; do
    b=$(basename "$src")
    assert_fixture "${b%.*}" "$src" "$d"
  done < <(find "$ROOT/tests/safety/$d" -type f \( -name '*.c' -o -name '*.cp' \) -print \
           | LC_ALL=C sort)
done

# Re-render txt so the BLOCKED list is grouped at the end while counts stay exact.
{
  echo "EXTRA ORPHANED-FIXTURE RUNNER  (c+ = $CPLUS, timeout = ${TIMEOUT}s)"
  echo "Columns: STATUS NAME [CATEGORY] EXACT-REASON"
  echo "Discovery: 11 top-level + 100 subdirectory fixtures = 111 expected; $TOTAL processed this run."
  echo
  grep -v '^BLOCKED' "$SUMMARY" || true
  echo
  echo "BLOCKED LIST (no expected diagnostic; kept visible, never PASS):"
  grep '^BLOCKED' "$SUMMARY" || true
} > "$SUMMARY.tmp" && mv "$SUMMARY.tmp" "$SUMMARY"

FAILALL=$((N_FAIL + N_DIAG + N_BUILD + N_RT + N_TIMEOUT))
{
  printf '  "total": %d,\n  "pass": %d,\n  "fail": %d,\n  "blocked": %d,\n  "skip": %d,\n' \
    "$TOTAL" "$N_PASS" "$FAILALL" "$N_BLOCKED" "$N_SKIP"
  printf '  "by_status": {"PASS":%d,"FAIL":%d,"DIAGNOSTIC_MISMATCH":%d,"BUILD_FAILURE":%d,"RUNTIME_FAILURE":%d,"TIMEOUT":%d,"BLOCKED":%d,"SKIPPED":%d}\n}\n' \
    "$N_PASS" "$N_FAIL" "$N_DIAG" "$N_BUILD" "$N_RT" "$N_TIMEOUT" "$N_BLOCKED" "$N_SKIP"
} > "$RES/.footer.json"
{
  printf '{\n  "mode": "strict-authoritative (default recorded as evidence)",\n'
  printf '  "cplus_bin": "%s",\n  "discovered_expected": 111,\n  "processed": %d,\n  "results": [\n' "$(json_escape "$CPLUS")" "$TOTAL"
  if [ -s "$JSONL" ]; then
    sed '$!s/$/,/' "$JSONL"
  fi
  printf '  ],\n'
  cat "$RES/.footer.json"
} > "$RES/summary.json"
rm -f "$JSONL" "$RES/.footer.json"

printf '\nTOTAL %d  PASS %d  FAIL %d  BLOCKED %d  SKIP %d\n' \
  "$TOTAL" "$N_PASS" "$FAILALL" "$N_BLOCKED" "$N_SKIP" >> "$SUMMARY"
echo "TOTAL $TOTAL  PASS $N_PASS  FAIL $FAILALL  BLOCKED $N_BLOCKED  SKIP $N_SKIP"
echo "Summaries: $RES/summary.txt , $RES/summary.json"
[ "$FAILALL" -eq 0 ]
