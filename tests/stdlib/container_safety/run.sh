#!/bin/sh
# Bounded runner for the container-safety regression suite.
#
# Usage:  sh tests/stdlib/container_safety/run.sh
#
# Order is deliberate: strict/no-sanitizer FIRST, then UBSan.
# Every compiler invocation and every program invocation is wrapped in
# `timeout` (30s default, SIGKILL escalation).  A timeout is SKIPPED, never
# counted as a pass.  Each test is accepted only when:
#   * build and run both exit 0,
#   * stderr is empty (UBSan diagnostics would appear here),
#   * the final stdout line is "CONTAINER_SAFETY <checks> <failures>",
#   * <failures> == 0 and every "<tag> <1|0>" line printed 1,
#   * the number of tags equals <checks>,
#   * strict-none and UBSan stdout are byte-identical (determinism).
#
# Exit status: 0 = everything verified; 1 = a check failed; 2 = tool missing.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT=${CS_OUT:-"$DIR/.run"}
BUILD_TIMEOUT=${CS_BUILD_TIMEOUT:-30}
RUN_TIMEOUT=${CS_RUN_TIMEOUT:-30}
KILL_AFTER=${CS_KILL_AFTER:-5}

# Keep `timeout` and the toolchain deterministic across hosts.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 2
if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS (build it first)" >&2
  exit 2
fi

MODES="strict_none ubsan"
label() { case "$1" in strict_none) printf '%s' 'strict-none' ;; *) printf '%s' "$1" ;; esac; }
flags() { case "$1" in strict_none) printf '%s' '--strict --sanitize=none' ;; ubsan) printf '%s' '--strict --sanitize=undefined' ;; esac; }
numeric() { case "$1" in ''|*[!0-9]*) return 1 ;; *) return 0 ;; esac; }
add() { eval "$1=\$((\$1 + 1))"; }

for _k in $MODES; do
  eval "pass_$_k=0; fail_$_k=0; skip_$_k=0; checks_$_k=0"
done

selected=0
for _src in "$DIR"/cs_*.cp; do
  [ -f "$_src" ] || continue
  selected=$((selected + 1))
  _name=$(basename "$_src" .cp)

  for _k in $MODES; do
    _lab=$(label "$_k")
    _fl=$(flags "$_k")
    _bin="$OUT/${_name}_${_k}"
    _blog="$OUT/${_name}_${_k}.build.log"
    _so="$OUT/${_name}_${_k}.out"
    _se="$OUT/${_name}_${_k}.err"
    rm -f "$_bin" "$_so" "$_se"

    # shellcheck disable=SC2086
    timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$_src" $_fl -o "$_bin" >"$_blog" 2>&1
    _brc=$?
    if [ "$_brc" -ne 0 ]; then
      if [ "$_brc" -eq 124 ] || [ "$_brc" -ge 128 ]; then
        printf 'TIMEOUT  %s (%s): build exceeded %ss (rc=%s)\n' "$_name" "$_lab" "$BUILD_TIMEOUT" "$_brc"
        add "skip_$_k"
      else
        printf 'BUILD-FAIL %s (%s): compiler exit %s\n' "$_name" "$_lab" "$_brc"
        tail -n 5 "$_blog" | sed 's/^/           /'
        add "fail_$_k"
      fi
      continue
    fi

    timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$_bin" </dev/null >"$_so" 2>"$_se"
    _rrc=$?
    if [ "$_rrc" -eq 124 ] || [ "$_rrc" -ge 128 ]; then
      printf 'TIMEOUT  %s (%s): run exceeded %ss (rc=%s)\n' "$_name" "$_lab" "$RUN_TIMEOUT" "$_rrc"
      add "skip_$_k"
      continue
    fi

    _final=$(tail -n 1 "$_so")
    _c="?"; _f="?"
    case "$_final" in
      "CONTAINER_SAFETY "*)
        _rest=${_final#CONTAINER_SAFETY }
        _c=${_rest%% *}
        _f=${_rest##* }
        ;;
    esac
    _counts=$(awk '/^CONTAINER_SAFETY /{next} NF==2 && ($2=="0" || $2=="1"){n++; if($2=="0") b++} END{print n+0 "," b+0}' "$_so")
    _tags=${_counts%%,*}
    _bad=${_counts##*,}
    _why=""
    if [ "$_rrc" -ne 0 ]; then _why="exit $_rrc"; fi
    if [ -s "$_se" ]; then _why="$_why stderr"; fi
    if ! numeric "$_c" || ! numeric "$_f"; then _why="$_why bad-summary"; fi
    if numeric "$_f" && [ "$_f" -ne 0 ]; then _why="$_why failures=$_f"; fi
    if numeric "$_bad" && [ "$_bad" -ne 0 ]; then _why="$_why bad-tags=$_bad"; fi
    if numeric "$_c" && [ "$_c" -ne "$_tags" ]; then _why="$_why tags=$_tags"; fi

    if [ -n "$_why" ]; then
      printf 'FAIL     %s (%s):%s\n' "$_name" "$_lab" "$_why"
      head -n 4 "$_se" | sed 's/^/           /'
      add "fail_$_k"
    else
      printf 'ok       %s (%s): %s\n' "$_name" "$_lab" "$_final"
      add "pass_$_k"
      eval "checks_$_k=\$((checks_$_k + _c))"
    fi
  done

  # Determinism: the same program under both sanitizer settings must agree
  # byte-for-byte on every printed tag and on the summary.
  if [ -f "$OUT/${_name}_strict_none.out" ] && [ -f "$OUT/${_name}_ubsan.out" ]; then
    if cmp -s "$OUT/${_name}_strict_none.out" "$OUT/${_name}_ubsan.out"; then
      printf 'deterministic %s: strict-none == ubsan stdout\n' "$_name"
    else
      printf 'FAIL     %s: stdout differs between strict-none and ubsan\n' "$_name"
      add fail_ubsan
    fi
  fi
done

if [ "$selected" -eq 0 ]; then
  echo "error: no cs_*.cp tests found in $DIR" >&2
  exit 2
fi

printf '\n%-14s %8s %8s %8s %10s\n' MODE PASSED FAILED SKIPPED CHECKS
tp=0; tf=0; ts=0; tc=0
for _k in $MODES; do
  eval "_p=\$pass_$_k; _f=\$fail_$_k; _s=\$skip_$_k; _c=\$checks_$_k"
  printf '%-14s %8s %8s %8s %10s\n' "$(label "$_k")" "$_p" "$_f" "$_s" "$_c"
  tp=$((tp + _p)); tf=$((tf + _f)); ts=$((ts + _s)); tc=$((tc + _c))
done
printf 'container safety: %s passed, %s failed, %s skipped, %s checks (%s file(s) x 2 modes)\n' \
  "$tp" "$tf" "$ts" "$tc" "$selected"

if [ "$tf" -ne 0 ]; then exit 1; fi
if [ "$ts" -ne 0 ]; then
  echo "note: $ts invocation(s) timed out and were SKIPPED, not counted as passes" >&2
  exit 1
fi
exit 0
