#!/bin/sh
# Security regression runner for the modern-stdlib workstream.
#
# Usage:
#   sh tests/security/stdlib/run.sh
#   sh tests/security/stdlib/run.sh --only hostile --mode strict-none,ubsan
#   sh tests/security/stdlib/run.sh --mode default --only sec_vector
#
# Discovers every .c/.cp source under:
#   tests/security/stdlib/            (group: stdlib)
#   tests/security/hostile_inputs/    (group: hostile_inputs)
#   tests/security/resource_exhaustion/ (group: resource_exhaustion)
# NOTES/README files, hidden paths and anything inside a fixtures/ directory
# are not tests.  Fixture sidecars (<name>.stdin, <name>.exit, <name>.expected,
# <name>.stderr.expected, <name>.skip, <name>.blocked, <name>.modes) are consumed
# when present; see README.md.
#
# Each (test, mode) invocation is built and run under `timeout -k 5 30`.
# stdout, stderr, exit status and the compiler log are captured verbatim under
# build/security/.  A JSON summary is written to
# build/security/security_summary.json and the exact terminal table to
# build/security/security_table.txt.
#
# Exit status: 0 = no required failures; 1 = FAIL/BUILD_FAILURE/TIMEOUT/BLOCKED;
#              2 = bad invocation or environment (missing c+/timeout).
# Empty groups and SKIPPED tests are reported, never silently counted as green.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2

CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT=${SECURITY_OUT:-"$ROOT/build/security"}
RUN_TIMEOUT=${SECURITY_RUN_TIMEOUT:-30}
BUILD_TIMEOUT=${SECURITY_BUILD_TIMEOUT:-30}
KILL_AFTER=${SECURITY_KILL_AFTER:-5}

# Deterministic timeout/toolchain selection across hosts.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

usage() {
  cat <<'EOF'
Usage: sh tests/security/stdlib/run.sh [options]

Options:
  --only PATTERN        Run only tests whose name or group/name contains
                        PATTERN (literal substring; repeatable).
  --mode MODE[,MODE...] Modes: strict-none (default), default, ubsan
                        (repeatable; comma-separated lists accepted).
  -h, --help            Show this help.

Environment:
  CPLUS_BIN             Path to c+ (default: <repo>/build/c+).
  SECURITY_OUT          Artifact directory (default: <repo>/build/security).
  SECURITY_TEST_ROOTS   Newline-separated override for the three test roots.
  SECURITY_RUN_TIMEOUT  Per-run timeout seconds (default: 30).
  SECURITY_BUILD_TIMEOUT Per-build timeout seconds (default: 30).
  SECURITY_KILL_AFTER   SIGKILL escalation grace seconds (default: 5).
EOF
}

MODESPEC=""
ONLYSPEC=""
while [ "$#" -gt 0 ]; do
  case "$1" in
    --only)
      [ "$#" -ge 2 ] || { echo "error: --only needs a pattern" >&2; exit 2; }
      ONLYSPEC="$ONLYSPEC
$2"
      shift 2
      ;;
    --only=*)
      ONLYSPEC="$ONLYSPEC
${1#--only=}"
      shift
      ;;
    --mode)
      [ "$#" -ge 2 ] || { echo "error: --mode needs a value" >&2; exit 2; }
      MODESPEC="$MODESPEC
$2"
      shift 2
      ;;
    --mode=*)
      MODESPEC="$MODESPEC
${1#--mode=}"
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "error: unknown argument: $1" >&2
      usage >&2
      exit 2
      ;;
  esac
done

mkdir -p "$OUT" || exit 2
WORK="$OUT/.work.$$"
mkdir -p "$WORK" || exit 2
trap 'rm -rf "$WORK"' 0 1 2 3 15

# --- modes / filters -------------------------------------------------------

printf '%s\n' "$MODESPEC" | tr ',' '\n' |
  sed 's/^[[:space:]]*//; s/[[:space:]]*$//; /^$/d' |
  awk '!seen[$0]++' > "$WORK/modes.list"
if [ ! -s "$WORK/modes.list" ]; then
  printf 'strict-none\n' > "$WORK/modes.list"
fi
while IFS= read -r m; do
  case "$m" in
    strict-none|default|ubsan) ;;
    *) echo "error: unsupported mode '$m' (want strict-none, default, ubsan)" >&2; exit 2 ;;
  esac
done < "$WORK/modes.list"

printf '%s\n' "$ONLYSPEC" | sed '/^$/d' > "$WORK/only.list"

mode_flags() {
  case "$1" in
    strict-none) printf '%s' '--strict --sanitize=none' ;;
    default) printf '%s' '' ;;
    ubsan) printf '%s' '--strict --sanitize=undefined' ;;
  esac
}

matches_only() {
  [ -s "$WORK/only.list" ] || return 0
  while IFS= read -r p; do
    [ -n "$p" ] || continue
    case "$1/$2" in *"$p"*) return 0 ;; esac
    case "$2" in *"$p"*) return 0 ;; esac
  done < "$WORK/only.list"
  return 1
}

read_reason() {
  if [ -f "$1" ]; then
    r=$(tr '\n\t' '  ' < "$1" | cut -c1-160)
    printf '%s' "${r:-$2}"
  else
    printf '%s' "$2"
  fi
}

# --- preconditions ---------------------------------------------------------

TIMEOUT_BIN=$(command -v timeout 2>/dev/null)
GLOBAL_BLOCK=""
if [ -z "$TIMEOUT_BIN" ]; then
  GLOBAL_BLOCK="'timeout' utility not found"
fi
if [ ! -x "$CPLUS" ]; then
  if [ -n "$GLOBAL_BLOCK" ]; then
    GLOBAL_BLOCK="$GLOBAL_BLOCK; C+ compiler not executable: $CPLUS"
  else
    GLOBAL_BLOCK="C+ compiler not executable: $CPLUS"
  fi
fi

ROOTS_DEFAULT="$DIR
$ROOT/tests/security/hostile_inputs
$ROOT/tests/security/resource_exhaustion"
ROOTS=${SECURITY_TEST_ROOTS:-$ROOTS_DEFAULT}
printf '%s\n' "$ROOTS" | sed '/^[[:space:]]*$/d' > "$WORK/roots.list"

: > "$WORK/warnings.txt"
: > "$WORK/groups.tsv"
: > "$WORK/tests.list"
: > "$WORK/missing_groups.txt"

# --- discovery -------------------------------------------------------------

while IFS= read -r tr; do
  [ -n "$tr" ] || continue
  grp=$(basename "$tr")
  trel=$tr
  case "$trel" in "$ROOT"/*) trel=${trel#"$ROOT"/} ;; esac
  printf '%s\t%s\n' "$grp" "$trel" >> "$WORK/groups.tsv"

  if [ ! -d "$tr" ]; then
    printf 'group %s (%s) is EMPTY: directory does not exist\n' "$grp" "$trel" >> "$WORK/warnings.txt"
    printf '%s\n' "$grp" >> "$WORK/missing_groups.txt"
    continue
  fi

  find "$tr" -type f \( -name '*.cp' -o -name '*.c' \) \
    ! -name '.*' ! -path '*/.*/*' \
    ! -path '*/fixtures/*' ! -path '*/Fixtures/*' ! -path '*/FIXTURES/*' \
    ! -name 'NOTES.*' ! -name 'README.*' -print |
    LC_ALL=C sort |
    while IFS= read -r src; do
      rel=${src#"$tr"/}
      nm=${rel%.*}
      safe=$(printf '%s' "$nm" | tr '/ ' '__')
      srel=$src
      case "$srel" in "$ROOT"/*) srel=${srel#"$ROOT"/} ;; esac
      printf '%s\t%s\t%s\t%s\t%s\n' "$grp" "$nm" "$safe" "$src" "$srel" >> "$WORK/tests.list"
    done
done < "$WORK/roots.list"

awk -F'\t' 'FILENAME==ARGV[1]{c[$1]++;next} FILENAME==ARGV[2]{m[$1]++;next} !($1 in c) && !($1 in m){printf "group %s (%s) is EMPTY: no .c/.cp tests discovered\n", $1, $2}' \
  "$WORK/tests.list" "$WORK/missing_groups.txt" "$WORK/groups.tsv" >> "$WORK/warnings.txt"

# --- build + run -----------------------------------------------------------

: > "$WORK/records.tsv"
idx=0

while IFS="$(printf '\t')" read -r grp nm safe src srel; do
  [ -n "${grp:-}" ] || continue
  base=$src
  base=${base%.*}

  stdin_file="$base.stdin"
  stdin_rel="-"
  if [ -f "$stdin_file" ]; then
    stdin_rel=$stdin_file
    case "$stdin_rel" in "$ROOT"/*) stdin_rel=${stdin_rel#"$ROOT"/} ;; esac
  else
    stdin_file=""
  fi

  expected_rc=0
  if [ -f "$base.exit" ]; then
    expected_rc=$(tr -d '[:space:]' < "$base.exit")
  fi
  case "$expected_rc" in ''|*[!0-9]*) expected_rc=0 ;; esac

  outdir="$OUT/$grp"
  mkdir -p "$outdir" || exit 2

  for mode in $(cat "$WORK/modes.list"); do
    idx=$((idx + 1))
    flags=$(mode_flags "$mode")
    status=""; detail=""
    erc=""; bcmd="-"; rcmd="-"
    so_rel="-"; se_rel="-"; blog_rel="-"; rf_rel="-"

    if [ -n "$GLOBAL_BLOCK" ]; then
      status=BLOCKED
      detail="environment: $GLOBAL_BLOCK"
    elif [ -f "$base.blocked" ]; then
      status=BLOCKED
      detail=$(read_reason "$base.blocked" "blocked by <name>.blocked")
    elif [ -f "$base.skip" ]; then
      status=SKIPPED
      detail=$(read_reason "$base.skip" "skipped by <name>.skip")
    elif ! matches_only "$grp" "$nm"; then
      status=SKIPPED
      detail="filtered by --only"
    elif [ -f "$base.modes" ] && ! tr ',' ' ' < "$base.modes" | grep -qw -- "$mode"; then
      status=SKIPPED
      detail="mode not listed in <name>.modes"
    else
      bin="$outdir/${safe}.${mode}"
      blog="$outdir/${safe}.${mode}.build.log"
      so="$outdir/${safe}.${mode}.out"
      se="$outdir/${safe}.${mode}.err"
      rf="$outdir/${safe}.${mode}.rc"
      rm -f "$bin" "$blog" "$so" "$se" "$rf" \
        "$outdir/${safe}.${mode}.diff"
      blog_rel=$blog; case "$blog_rel" in "$ROOT"/*) blog_rel=${blog_rel#"$ROOT"/} ;; esac
      so_rel=$so; case "$so_rel" in "$ROOT"/*) so_rel=${so_rel#"$ROOT"/} ;; esac
      se_rel=$se; case "$se_rel" in "$ROOT"/*) se_rel=${se_rel#"$ROOT"/} ;; esac
      rf_rel=$rf; case "$rf_rel" in "$ROOT"/*) rf_rel=${rf_rel#"$ROOT"/} ;; esac

      extra=""
      if grep -q 'stdlib/json.hp' "$src" 2>/dev/null; then
        extra="-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson"
      fi
      bcmd="$CPLUS build $src $flags $extra -o $bin"
      # shellcheck disable=SC2086  # $flags/$extra must word-split into CLI flags
      timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$src" $flags $extra \
        -o "$bin" > "$blog" 2>&1
      brc=$?

      if [ "$brc" -eq 124 ] || [ "$brc" -eq 137 ]; then
        status=TIMEOUT
        detail="build exceeded ${BUILD_TIMEOUT}s (rc=$brc)"
      elif [ "$brc" -ne 0 ]; then
        status=BUILD_FAILURE
        tailtxt=$(tail -n 3 "$blog" | tr '\n\t' '  ' | cut -c1-160)
        detail="compiler exit $brc: $tailtxt"
      else
        # The brace group suppresses the *shell's* "Abort trap"/"Segmentation
        # fault" job notice for signal-killed children; the child's own stderr
        # still goes to $se verbatim.
        if [ -n "$stdin_file" ]; then
          rcmd="timeout -k $KILL_AFTER $RUN_TIMEOUT $bin < $stdin_file"
          { timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$bin" \
              < "$stdin_file" > "$so" 2> "$se"; erc=$?; } 2>/dev/null
        else
          rcmd="timeout -k $KILL_AFTER $RUN_TIMEOUT $bin < /dev/null"
          { timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$bin" \
              </dev/null > "$so" 2> "$se"; erc=$?; } 2>/dev/null
        fi
        printf '%s\n' "$erc" > "$rf"

        if [ "$erc" -eq 124 ] || [ "$erc" -eq 137 ]; then
          status=TIMEOUT
          detail="run exceeded ${RUN_TIMEOUT}s (rc=$erc)"
        elif [ "$erc" -ne "$expected_rc" ]; then
          status=FAIL
          detail="exit $erc (expected $expected_rc)"
        elif [ -f "$base.expected" ] && \
             ! diff -u "$base.expected" "$so" > "$outdir/${safe}.${mode}.diff" 2>&1; then
          status=FAIL
          detail="stdout mismatch vs <name>.expected"
        elif [ -f "$base.stderr.expected" ] && \
             ! grep -qF -f "$base.stderr.expected" "$se"; then
          status=FAIL
          detail="stderr missing <name>.stderr.expected substring"
        else
          status=PASS
          detail="exit $erc"
        fi
      fi
    fi

    [ -n "$detail" ] || detail="-"
    printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' \
      "$idx" "$grp" "$nm" "$mode" "$status" "${erc:--}" "$expected_rc" \
      "$detail" "$srel" "$stdin_rel" "$bcmd" "$rcmd" \
      "$so_rel" "$se_rel" "$blog_rel" "$rf_rel" >> "$WORK/records.tsv"
  done
done < "$WORK/tests.list"

# --- terminal table --------------------------------------------------------

{
  printf '%-20s %-30s %-12s %-14s %5s  %s\n' GROUP TEST MODE STATUS RC DETAIL
  printf '%s\n' '---------------------------------------------------------------------------------------------------------'
  awk -F'\t' '{printf "%-20s %-30s %-12s %-14s %5s  %s\n", $2, $3, $4, $5, $6, substr($8,1,62)}' \
    "$WORK/records.tsv"
  awk -F'\t' 'FNR==NR{c[$1]++;next} !($1 in c){printf "%-20s %-30s %-12s %-14s %5s  %s\n", $1, "<EMPTY>", "-", "EMPTY", "-", "no .c/.cp tests discovered"}' \
    "$WORK/tests.list" "$WORK/groups.tsv"
} | tee "$OUT/security_table.txt"

# --- JSON summary ----------------------------------------------------------

AWK_JE='function je(s){ gsub(/\\/,"\\\\",s); gsub(/"/,"\\\"",s); gsub(/\t/,"\\t",s); gsub(/\r/,"\\r",s); return s }'

cstatus() { awk -F'\t' -v s="$1" '$5==s{n++} END{print n+0}' "$WORK/records.tsv"; }
n_pass=$(cstatus PASS)
n_fail=$(cstatus FAIL)
n_bfail=$(cstatus BUILD_FAILURE)
n_timeout=$(cstatus TIMEOUT)
n_skip=$(cstatus SKIPPED)
n_block=$(cstatus BLOCKED)
n_total=$(awk 'END{print NR+0}' "$WORK/records.tsv")

JSON="$OUT/security_summary.json"
JSON_REL=$JSON; case "$JSON_REL" in "$ROOT"/*) JSON_REL=${JSON_REL#"$ROOT"/} ;; esac

{
  printf '{\n'
  printf '  "schema": "security-regression-runner/1",\n'
  printf '  "generated_at_utc": "%s",\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  printf '  "root": "%s",\n' "$(printf '%s' "$ROOT" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')"
  printf '  "modes": ['
  awk "$AWK_JE"'{if(sep++)printf ", "; printf "\"%s\"", je($0)}' "$WORK/modes.list"
  printf '],\n'
  printf '  "only_patterns": ['
  awk "$AWK_JE"'{if(sep++)printf ", "; printf "\"%s\"", je($0)}' "$WORK/only.list"
  printf '],\n'
  printf '  "toolchain": {"cplus": "%s", "timeout": "%s", "path": "%s"},\n' \
    "$(printf '%s' "$CPLUS" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')" \
    "$TIMEOUT_BIN" \
    "$(printf '%s' "$PATH" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g')"
  printf '  "limits": {"build_timeout_s": %s, "run_timeout_s": %s, "kill_after_s": %s},\n' \
    "$BUILD_TIMEOUT" "$RUN_TIMEOUT" "$KILL_AFTER"
  printf '  "totals": {"invocations": %s, "PASS": %s, "FAIL": %s, "BUILD_FAILURE": %s, "TIMEOUT": %s, "SKIPPED": %s, "BLOCKED": %s},\n' \
    "$n_total" "$n_pass" "$n_fail" "$n_bfail" "$n_timeout" "$n_skip" "$n_block"
  printf '  "groups": [\n'
  awk -F'\t' "$AWK_JE"'FNR==NR{c[$1]++;next} {if(sep++)printf ",\n"; printf "    {\"name\":\"%s\",\"path\":\"%s\",\"discovered\":%d,\"status\":\"%s\"}", je($1), je($2), c[$1]+0, (c[$1]+0?"OK":"EMPTY")} END{if(sep)printf "\n"}' \
    "$WORK/tests.list" "$WORK/groups.tsv"
  printf '  ],\n'
  printf '  "results": [\n'
  awk -F'\t' "$AWK_JE"'{
    if(NR>1) printf ",\n"
    printf "    {\"group\":\"%s\",\"name\":\"%s\",\"mode\":\"%s\",\"status\":\"%s\",", je($2), je($3), je($4), je($5)
    printf "\"source\":\"%s\",\"detail\":\"%s\",", je($9), je($8)
    printf "\"exit_code\":%s,\"expected_exit_code\":%s,", ($6=="-"?"null":$6), $7
    printf "\"stdin\":%s,", ($10=="-"?"null":("\"" je($10) "\""))
    printf "\"build_command\":%s,\"run_command\":%s,", ($11=="-"?"null":("\"" je($11) "\"")), ($12=="-"?"null":("\"" je($12) "\""))
    printf "\"artifacts\":{\"stdout\":%s,\"stderr\":%s,\"build_log\":%s,\"exit_file\":%s}}", \
      ($13=="-"?"null":("\"" je($13) "\"")), ($14=="-"?"null":("\"" je($14) "\"")), \
      ($15=="-"?"null":("\"" je($15) "\"")), ($16=="-"?"null":("\"" je($16) "\""))
  } END{if(NR)printf "\n"}' "$WORK/records.tsv"
  printf '  ],\n'
  printf '  "warnings": [\n'
  awk "$AWK_JE"'{if(sep++)printf ",\n"; printf "    \"%s\"", je($0)} END{if(sep)printf "\n"}' \
    "$WORK/warnings.txt"
  printf '  ]\n'
  printf '}\n'
} > "$JSON"

# --- final report ----------------------------------------------------------

ngroups=$(awk 'END{print NR+0}' "$WORK/groups.tsv")
printf '\nsecurity regressions: %s invocation(s), %s group(s), modes: %s\n' \
  "$n_total" "$ngroups" "$(paste -sd, "$WORK/modes.list")"
printf '  PASS=%s FAIL=%s BUILD_FAILURE=%s TIMEOUT=%s SKIPPED=%s BLOCKED=%s\n' \
  "$n_pass" "$n_fail" "$n_bfail" "$n_timeout" "$n_skip" "$n_block"
TABLE_REL=$OUT/security_table.txt
case "$TABLE_REL" in "$ROOT"/*) TABLE_REL=${TABLE_REL#"$ROOT"/} ;; esac
printf '  JSON: %s\n' "$JSON_REL"
printf '  table: %s\n' "$TABLE_REL"

while IFS= read -r w; do
  [ -n "$w" ] || continue
  printf 'warning: %s\n' "$w" >&2
done < "$WORK/warnings.txt"
if [ "$n_total" -eq 0 ]; then
  printf 'warning: zero tests discovered; this is NOT a passing result\n' >&2
fi

if [ -n "$GLOBAL_BLOCK" ]; then
  printf 'error: %s\n' "$GLOBAL_BLOCK" >&2
  exit 2
fi
if [ $((n_fail + n_bfail + n_timeout + n_block)) -ne 0 ]; then
  exit 1
fi
exit 0
