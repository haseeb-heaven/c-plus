#!/usr/bin/env bash
# PHASE 3 audit — master test runner.
#
# Re-runs the tests discovered under tests/ by Agent B (audit_phase3) with the
# audit protocol: build with `--strict --sanitize=none -I.`, execute under
# `timeout 30`, capture stdout/stderr/exit, and byte-compare .expected fixtures
# when present. No `|| true`, no silent skips: every discovered test is
# classified PASS / FAIL / BUILD_FAIL / TIMEOUT / BLOCKED with an exact reason.
#
# Usage:  bash tests/audit_phase3/run_all.sh
#         CPLUS_BIN=/path/to/c+ bash tests/audit_phase3/run_all.sh
#
# Output: a per-test table, totals, and tests/audit_phase3/work/results.tsv
# (tab-separated: group, test, status, reason). All build/run logs are kept
# under tests/audit_phase3/work/logs/.
#
# Exit: 0 = no real failures; 1 = at least one FAIL / BUILD_FAIL / TIMEOUT.
# BLOCKED rows are reported separately and do not by themselves fail the run.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd) || exit 1
cd "$ROOT" || exit 1
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
WORK=${AUDIT_WORK:-"$ROOT/tests/audit_phase3/work"}
LOGS="$WORK/logs"
mkdir -p "$LOGS" || exit 1

BUILD_TIMEOUT=${AUDIT_BUILD_TIMEOUT:-30}
RUN_TIMEOUT=${AUDIT_RUN_TIMEOUT:-30}
KILL_AFTER=${AUDIT_KILL_AFTER:-5}
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH

RESULTS="$WORK/results.tsv"
: >"$RESULTS"
declare -a ROWS=()
declare -A COUNT=([PASS]=0 [FAIL]=0 [BUILD_FAIL]=0 [TIMEOUT]=0 [BLOCKED]=0)

# libs_for <src> — external library flags, mirroring tests/stdlib/run_stdlib_tests.sh.
libs_for() {
  local src="$1" out=""
  grep -q 'stdlib/json.hp' "$src" && out="$out -I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson"
  grep -q 'stdlib/image.hp' "$src" && out="$out -I$ROOT/third_party/stb -L$ROOT/build/ext -lstb"
  grep -q 'stdlib/audio.hp' "$src" && out="$out -I$ROOT/third_party/miniaudio -L$ROOT/build/ext -lminiaudio -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox"
  grep -qE 'stdlib/(window|graphics|input|gui)\.hp' "$src" && out="$out $(pkg-config --cflags --libs sdl3 2>/dev/null)"
  grep -q 'stdlib/gui.hp' "$src" && out="$out -I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear"
  grep -q 'stdlib/http.hp' "$src" && out="$out $(pkg-config --cflags --libs libcurl 2>/dev/null)"
  grep -q 'stdlib/async.hp' "$src" && out="$out $(pkg-config --cflags --libs libuv 2>/dev/null)"
  grep -q 'stdlib/video.hp' "$src" && out="$out $(pkg-config --cflags --libs libavformat libavcodec libavutil 2>/dev/null)"
  printf '%s' "$out"
}

record() { # record <group> <test> <status> <reason>
  local group="$1" test="$2" status="$3" reason="$4"
  COUNT[$status]=$(( ${COUNT[$status]:-0} + 1 ))
  ROWS+=("$group|$test|$status|$reason")
  printf '%s\t%s\t%s\t%s\n' "$group" "$test" "$status" "$reason" >>"$RESULTS"
}

# expect_for <src> — echo "<kind>|<trapcode>" from the relevant regular runner.
# kind: run | reject | trap
expect_for() {
  local src="$1" base name kind trap
  base=$(basename "$src" .cp)
  case "$src" in
    */cplus_breaker_v3/*)
      # Authoritative expectations live in cases.list.
      while IFS='|' read -r name kind trap; do
        [ "$name" = "$base" ] || continue
        printf '%s|%s' "$kind" "$trap"
        return 0
      done < "$ROOT/tests/stress/cplus_breaker_v3/cases.list"
      printf 'run|'
      ;;
    */cplus_stress_10/*)
      case "$base" in
        08_reject_resource_copy|09_reject_borrow_escape) printf 'reject|' ;;
        10_trap_double_free) printf 'trap|104' ;;
        *) printf 'run|' ;;
      esac
      ;;
    */cplus_probe_*)
      case "$base" in
        cplus_probe_bad_*) printf 'reject|' ;;
        cplus_probe_double_release) printf 'trap|104' ;;
        cplus_probe_explicit_bounds) printf 'trap|101' ;;
        *) printf 'run|' ;;
      esac
      ;;
    *) printf 'run|' ;;
  esac
}

one() { # one <group> <src>
  local group="$1" src="$2"
  local base stem bin buildlog stdout stderr
  base=$(basename "$src")
  stem=${base%.cp}
  bin="$WORK/${group}_${stem}"
  buildlog="$LOGS/${group}_${stem}.build.log"
  stdout="$LOGS/${group}_${stem}.out"
  stderr="$LOGS/${group}_${stem}.err"
  local expectation kind trap
  expectation=$(expect_for "$src")
  kind=${expectation%%|*}
  trap=${expectation#*|}

  local flags extra brc=0 rrc=0
  extra=$(libs_for "$src")
  rm -f "$bin" "$stdout" "$stderr" "$buildlog"
  # shellcheck disable=SC2086
  timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$src" \
      --strict --sanitize=none -I. $extra -o "$bin" >"$buildlog" 2>&1
  brc=$?

  if [ "$brc" -ne 0 ]; then
    if [ "$kind" = reject ]; then
      record "$group" "$stem" PASS "expected compile-time rejection (build rc=$brc)"
      return 0
    fi
    if [ "$brc" -eq 124 ] || [ "$brc" -eq 137 ]; then
      record "$group" "$stem" TIMEOUT "build exceeded ${BUILD_TIMEOUT}s (rc=$brc)"
    elif [ "$brc" -ge 128 ]; then
      record "$group" "$stem" BUILD_FAIL "build terminated by signal $((brc-128)) (rc=$brc)"
    else
      local why
      why=$(grep -m1 -E 'error:|error ' "$buildlog" 2>/dev/null | head -c 200)
      record "$group" "$stem" BUILD_FAIL "build rc=$brc; ${why:-no diagnostic}"
    fi
    return 0
  fi
  if [ "$kind" = reject ]; then
    record "$group" "$stem" FAIL "unsafe program was ACCEPTED by strict build"
    return 0
  fi

  local dir stdin=/dev/null
  dir=$(dirname "$src")
  [ -f "$dir/$stem.stdin" ] && stdin="$dir/$stem.stdin"
  # shellcheck disable=SC2086
  ( ulimit -c 0; timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$bin" \
      <"$stdin" >"$stdout" 2>"$stderr" )
  rrc=$?

  if [ "$rrc" -eq 124 ] || [ "$rrc" -eq 137 ]; then
    record "$group" "$stem" TIMEOUT "run exceeded ${RUN_TIMEOUT}s (rc=$rrc)"
    return 0
  fi
  if [ "$rrc" -ge 128 ]; then
    local sigmsg
    sigmsg=$(head -n 1 "$stderr" 2>/dev/null | head -c 160)
    record "$group" "$stem" FAIL "runtime terminated by signal $((rrc-128)) (rc=$rrc); stderr: ${sigmsg:-empty}"
    return 0
  fi
  if [ "$kind" = trap ]; then
    if [ "$rrc" -eq 0 ]; then
      record "$group" "$stem" FAIL "expected runtime trap, got exit 0"
    elif [ -n "$trap" ] && [ "$rrc" -ne "$trap" ]; then
      record "$group" "$stem" FAIL "expected trap exit $trap, got $rrc"
    else
      record "$group" "$stem" PASS "expected runtime trap (exit $rrc)"
    fi
    return 0
  fi
  if [ "$rrc" -ne 0 ]; then
    local why
    why=$(head -n 1 "$stderr" 2>/dev/null | head -c 200)
    record "$group" "$stem" FAIL "runtime exit $rrc; stderr: ${why:-empty}"
    return 0
  fi

  # Byte-exact fixture comparison where available.
  local ef="$dir/$stem.expected" sef="$dir/$stem.stderr.expected"
  if [ -f "$ef" ] || [ -f "$sef" ]; then
    if [ -f "$ef" ] && ! cmp -s "$ef" "$stdout"; then
      local first
      first=$(diff -u "$ef" "$stdout" 2>/dev/null | grep -m1 '^[-+][^-+]' | head -c 160)
      record "$group" "$stem" FAIL "stdout mismatch vs .expected (${first:-differs})"
      return 0
    fi
    if [ -f "$sef" ] && ! cmp -s "$sef" "$stderr"; then
      local first
      first=$(diff -u "$sef" "$stderr" 2>/dev/null | grep -m1 '^[-+][^-+]' | head -c 160)
      record "$group" "$stem" FAIL "stderr mismatch vs .stderr.expected (${first:-differs})"
      return 0
    fi
    record "$group" "$stem" PASS "exit 0; stdout/stderr match .expected fixtures"
  else
    record "$group" "$stem" PASS "exit 0 (no byte-exact fixture; exit-code only)"
  fi
}

printf 'PHASE 3 audit runner: compiler=%s build<=%ss run<=%ss\n' "$CPLUS" "$BUILD_TIMEOUT" "$RUN_TIMEOUT"
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler not executable: $CPLUS" >&2
  exit 2
fi
command -v timeout >/dev/null 2>&1 || { echo "error: timeout utility missing" >&2; exit 2; }

# Group: deep stdlib regression tests.
for src in "$ROOT"/tests/stdlib/deep_*.cp; do
  [ -f "$src" ] && one deep "$src"
done
# Group: agent2 regression tests (run_all.cp is the runner itself, not a test).
for src in "$ROOT"/tests/stdlib/agent2/*.cp; do
  [ -f "$src" ] || continue
  [ "$(basename "$src")" = run_all.cp ] && continue
  one agent2 "$src"
done
# Group: adversarial regression tests.
for src in "$ROOT"/tests/stdlib/regressions/adv_*.cp; do
  [ -f "$src" ] && one regression "$src"
done
# Group: breaker V3, stress-10, and breaker probe suites.
for src in "$ROOT"/tests/stress/cplus_breaker_v3/*.cp; do
  [ -f "$src" ] && one breaker_v3 "$src"
done
for src in "$ROOT"/tests/stress/cplus_stress_10/*.cp; do
  [ -f "$src" ] && one stress10 "$src"
done
for src in "$ROOT"/tests/breaker/cplus_probe_*.cp; do
  [ -f "$src" ] && one breaker_probe "$src"
done

printf '\n%-14s %-38s %-11s %s\n' GROUP TEST STATUS REASON
printf '%-14s %-38s %-11s %s\n' -------- ---- ------ ------
for row in "${ROWS[@]}"; do
  IFS='|' read -r g t s r <<<"$row"
  printf '%-14s %-38s %-11s %s\n' "$g" "$t" "$s" "$r"
done

printf '\nTOTALS: discovered/executed=%s  PASS=%s  FAIL=%s  BUILD_FAIL=%s  TIMEOUT=%s  BLOCKED=%s\n' \
  "${#ROWS[@]}" "${COUNT[PASS]}" "${COUNT[FAIL]}" "${COUNT[BUILD_FAIL]}" \
  "${COUNT[TIMEOUT]}" "${COUNT[BLOCKED]}"
printf 'results: %s ; logs: %s\n' "$RESULTS" "$LOGS"

if [ "${COUNT[FAIL]}" -ne 0 ] || [ "${COUNT[BUILD_FAIL]}" -ne 0 ] || [ "${COUNT[TIMEOUT]}" -ne 0 ]; then
  exit 1
fi
exit 0
