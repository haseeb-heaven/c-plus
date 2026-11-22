#!/bin/sh
# Deep stdlib regression runner: bounded builds/runs, per-mode accounting.
#
# Usage:
#   sh tests/stdlib/run_deep_tests.sh [pattern ...]
#
# With no pattern every tests/stdlib/deep_*.cp is selected. With one or more
# patterns, a test is selected when its base name CONTAINS any pattern
# (substring match, the same convention as run_stdlib_tests.sh). Examples:
#   sh tests/stdlib/run_deep_tests.sh deep_net
#   sh tests/stdlib/run_deep_tests.sh deep_io deep_file
#
# Each selected test is built and run in three modes: default, strict-none,
# and ubsan. Every compiler invocation and every test-binary invocation is
# wrapped in `timeout` (with SIGKILL escalation), so no single probe can hang
# the suite. A timeout is always SKIPPED, never PASSED.
#
# Exit status: 0 = every selected invocation passed;
#              1 = at least one build/runtime failure (or no test matched);
#              2 = no failures, but at least one invocation timed out.
#   2 is deliberately non-zero: an unverified test is not a green test.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd) || exit 1
cd "$ROOT" || exit 1

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT=${DEEP_OUT:-"$ROOT/build/stdlib-deep"}
BUILD_TIMEOUT=${DEEP_BUILD_TIMEOUT:-60}
RUN_TIMEOUT=${DEEP_RUN_TIMEOUT:-30}
KILL_AFTER=${DEEP_KILL_AFTER:-5}

# Keep the toolchain and `timeout` deterministic across hosts.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 1

if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing to run unbounded tests" >&2
  exit 1
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS (build it first)" >&2
  exit 1
fi

# Internal mode keys use underscores (POSIX sh identifiers cannot contain '-').
MODES="default strict_none ubsan"
mode_label() {
  case "$1" in
    strict_none) printf '%s' 'strict-none' ;;
    *) printf '%s' "$1" ;;
  esac
}
flags_for() {
  case "$1" in
    default) printf '%s' '' ;;
    strict_none) printf '%s' '--strict --sanitize=none' ;;
    ubsan) printf '%s' '--strict --sanitize=undefined' ;;
  esac
}

# Counters, kept per mode. bfail/rfail/ofail split "failed" by phase so a
# compile error is never confused with a runtime error or an output mismatch.
for _key in $MODES; do
  eval "pass_$_key=0; fail_$_key=0; skip_$_key=0; bfail_$_key=0; rfail_$_key=0; ofail_$_key=0"
done
add_pass()       { eval "pass_$1=\$((pass_$1 + 1))"; }
add_fail()       { eval "fail_$1=\$((fail_$1 + 1))"; }
add_skip()       { eval "skip_$1=\$((skip_$1 + 1))"; }
add_build_fail() { eval "bfail_$1=\$((bfail_$1 + 1))"; }
add_run_fail()   { eval "rfail_$1=\$((rfail_$1 + 1))"; }
add_out_fail()   { eval "ofail_$1=\$((ofail_$1 + 1))"; }

# Optional byte-exact expectation fixtures:
#   tests/stdlib/<name>.expected       -> expected stdout
#   tests/stdlib/<name>.stderr.expected -> expected stderr
# A test without fixtures still runs and is checked by exit code; it is listed
# in the final NO-EXPECTATION section so it is never silently treated as proven.
no_expectation=""
compare_output() { # compare_output <name> <key> <stdout> <stderr>
  _n=$1; _k=$2; _so=$3; _se=$4
  _eo="tests/stdlib/${_n}.expected"
  _ee="tests/stdlib/${_n}.stderr.expected"
  [ -f "$_eo" ] || [ -f "$_ee" ] || {
    case " $no_expectation " in
      *" $_n "*) ;;
      *) no_expectation="$no_expectation $_n" ;;
    esac
    return 0
  }
  _bad=0
  _diff="$OUT/${_n}_${_k}.diff"
  : >"$_diff"
  if [ -f "$_eo" ] && ! diff -u "$_eo" "$_so" >>"$_diff" 2>&1; then _bad=1; fi
  if [ -f "$_ee" ] && ! diff -u "$_ee" "$_se" >>"$_diff" 2>&1; then _bad=1; fi
  if [ "$_bad" -ne 0 ]; then
    printf 'OUT-FAIL %s (%s): expected stdout/stderr mismatch (see %s)\n' \
      "$_n" "$(mode_label "$_k")" "$_diff"
    head -n 10 "$_diff" | sed 's/^/           /'
    add_fail "$_k"
    add_out_fail "$_k"
  fi
}

# matches <basename> [pattern ...] -> 0 when the basename contains any pattern.
matches() {
  _n=$1
  shift
  [ "$#" -eq 0 ] && return 0
  for _p in "$@"; do
    case "$_n" in
      *"$_p"*) return 0 ;;
    esac
  done
  return 1
}

selected=0

one() { # one <name> <mode-key> <src>
  _name=$1; _key=$2; _src=$3
  _label=$(mode_label "$_key")
  _bin="$OUT/${_name}_${_key}"
  _build_log="$OUT/${_name}_${_key}.build.log"
  _out="$OUT/${_name}_${_key}.out"
  _err="$OUT/${_name}_${_key}.err"

  _flags=$(flags_for "$_key")
  _extra=""
  case "$_name" in
    deep_json*|deep_cross_family) _extra="-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson" ;;
  esac

  # ---- build: always bounded. Capture $? directly; `if ! cmd` would erase it.
  rm -f "$_bin"
  # shellcheck disable=SC2086
  timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$_src" \
      $_flags $_extra -o "$_bin" >"$_build_log" 2>&1
  _brc=$?

  if [ "$_brc" -ne 0 ]; then
    # 124 = timeout's own sentinel; >=128 = terminated by a signal (e.g. the
    # SIGKILL escalation for a process that ignores SIGTERM). Neither is a pass.
    if [ "$_brc" -eq 124 ] || [ "$_brc" -ge 128 ]; then
      printf 'TIMEOUT  %s (%s): build exceeded %ss (rc=%s)\n' \
        "$_name" "$_label" "$BUILD_TIMEOUT" "$_brc"
      add_skip "$_key"
    else
      printf 'BUILD-FAIL %s (%s): compiler exit %s\n' "$_name" "$_label" "$_brc"
      tail -n 5 "$_build_log" | sed 's/^/           /'
      add_fail "$_key"
      add_build_fail "$_key"
    fi
    return 0
  fi

  # ---- run: always bounded, stdin fixture when present else /dev/null.
  _stdin=/dev/null
  [ -f "tests/stdlib/${_name}.stdin" ] && _stdin="tests/stdlib/${_name}.stdin"
  timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$_bin" \
      <"$_stdin" >"$_out" 2>"$_err"
  _rrc=$?

  if [ "$_rrc" -eq 0 ]; then
    printf 'ok       %s (%s): %s\n' "$_name" "$_label" "$(tail -n 1 "$_out")"
    add_pass "$_key"
    compare_output "$_name" "$_key" "$_out" "$_err"
  elif [ "$_rrc" -eq 124 ] || [ "$_rrc" -ge 128 ]; then
    printf 'TIMEOUT  %s (%s): run exceeded %ss (rc=%s)\n' \
      "$_name" "$_label" "$RUN_TIMEOUT" "$_rrc"
    add_skip "$_key"
  else
    # Preserve the exact non-zero exit code; never collapse it to "1".
    printf 'RUN-FAIL %s (%s): exit %s\n' "$_name" "$_label" "$_rrc"
    tail -n 6 "$_err" | sed 's/^/           /'
    add_fail "$_key"
    add_run_fail "$_key"
  fi
}

printf 'deep stdlib harness: build<=%ss, run<=%ss, kill-after=%ss, compiler=%s\n' \
  "$BUILD_TIMEOUT" "$RUN_TIMEOUT" "$KILL_AFTER" "$CPLUS"
if [ "$#" -gt 0 ]; then
  printf 'pattern(s): %s\n' "$*"
fi

for _src in tests/stdlib/deep_*.cp; do
  [ -f "$_src" ] || continue
  _name=$(basename "$_src" .cp)
  matches "$_name" "$@" || continue
  selected=$((selected + 1))
  for _key in $MODES; do
    one "$_name" "$_key" "$_src"
  done
done

if [ "$selected" -eq 0 ]; then
  echo "error: no deep_*.cp matched pattern(s): $*" >&2
  exit 1
fi

# Per-mode summary. "failed" is split into build vs runtime for diagnosis.
printf '\n%-12s %8s %8s %8s %11s %9s %9s\n' \
  MODE PASSED FAILED SKIPPED BUILD-FAIL RUN-FAIL OUT-FAIL
tp=0; tf=0; ts=0
for _key in $MODES; do
  eval "_p=\$pass_$_key; _f=\$fail_$_key; _s=\$skip_$_key; _bf=\$bfail_$_key; _rf=\$rfail_$_key; _of=\$ofail_$_key"
  printf '%-12s %8s %8s %8s %11s %9s %9s\n' \
    "$(mode_label "$_key")" "$_p" "$_f" "$_s" "$_bf" "$_rf" "$_of"
  tp=$((tp + _p)); tf=$((tf + _f)); ts=$((ts + _s))
done
printf 'deep stdlib tests: %s passed, %s failed, %s skipped (%s file(s) x %s modes)\n' \
  "$tp" "$tf" "$ts" "$selected" "3"
if [ -n "$no_expectation" ]; then
  printf 'NO-EXPECTATION (exit code only, no byte-exact fixture):%s\n' "$no_expectation"
fi

if [ "$tf" -ne 0 ]; then
  exit 1
fi
if [ "$ts" -ne 0 ]; then
  echo "note: $ts invocation(s) timed out and were SKIPPED, not counted as passes" >&2
  exit 2
fi
exit 0
