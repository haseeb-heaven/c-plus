#!/bin/sh
# system_safety suite runner — auto-discovery, bounded build/run, JSON + table.
#
#   sh tests/stdlib/system_safety/run.sh
#   sh tests/stdlib/system_safety/run.sh --only net_invalid
#   sh tests/stdlib/system_safety/run.sh --mode strict-none,default,ubsan
#
# Design rules (deliberate; see tests/stdlib/system_safety/README.md):
#   * set -u only.  There is NO set -e: a failing case is recorded and the
#     runner always continues with the next case / mode.
#   * no failure-suppressing short-circuits: every rc is captured explicitly
#     and classified, never masked.
#   * every build AND every run is bounded by `timeout -k <kill> <seconds>`.
#   * a timeout is TIMEOUT, never a PASS.  A build that fails because of the
#     known external c-plus-core runtime mismatch is BLOCKED, never a PASS.
#   * nothing is written into the test directory; all logs/bins/JSON live in
#     build/system-safety/ (gitignored).
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT=${SS_OUT:-"$ROOT/build/system-safety"}
BUILD_TIMEOUT=${SS_BUILD_TIMEOUT:-30}
RUN_TIMEOUT=${SS_RUN_TIMEOUT:-30}
KILL_AFTER=${SS_KILL_AFTER:-5}

# Keep timeout/compiler/diff deterministic across hosts.
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

usage() {
  cat <<'EOF'
usage: sh tests/stdlib/system_safety/run.sh [options]

Options:
  --only NAME        run only matching tests (exact basename or substring;
                     repeatable).  NAME is the .cp filename without suffix.
  --mode LIST        modes to run: strict-none (default), default, ubsan,
                     all; comma-separated and/or repeatable.
  --json PATH        override the JSON summary path
                     (default: build/system-safety/summary.json).
  --json-stdout      also print the JSON summary object on stdout.
  -h, --help         show this message.

Environment:
  CPLUS_BIN          compiler driver (default: <root>/build/c+)
  SS_OUT             output directory (default: <root>/build/system-safety)
  SS_BUILD_TIMEOUT   build timeout seconds (default: 30)
  SS_RUN_TIMEOUT     run timeout seconds (default: 30)
  SS_KILL_AFTER      SIGKILL escalation seconds (default: 5)

Exit status: 0 = at least one PASS and no FAIL/BUILD_FAILURE/TIMEOUT/BLOCKED;
             1 = a required case failed (details in terminal table + JSON);
             2 = setup/usage error (no compiler, no timeout, no tests, ...).
EOF
}

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

only=""
modes=""
json_path=""
json_stdout=0

# Accept repeatable --only and comma-separated values in one flag.
add_only() {
  _oifs=$IFS; IFS=,; set -- $1; IFS=$_oifs
  for _o in "$@"; do
    _o=$(printf '%s' "$_o" | tr -d ' \t\r\n')
    [ -n "$_o" ] || continue
    only="$only $_o"
  done
}

while [ "$#" -gt 0 ]; do
  case "$1" in
    --only) [ "$#" -ge 2 ] || die "--only requires a NAME"; add_only "$2"; shift 2 ;;
    --only=*) add_only "${1#--only=}"; shift ;;
    --mode) [ "$#" -ge 2 ] || die "--mode requires a LIST"; modes="$modes $2"; shift 2 ;;
    --mode=*) modes="$modes ${1#--mode=}"; shift ;;
    --json) [ "$#" -ge 2 ] || die "--json requires a PATH"; json_path=$2; shift 2 ;;
    --json=*) json_path=${1#--json=}; shift ;;
    --json-stdout) json_stdout=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

# --- mode list ------------------------------------------------------------
[ -n "$modes" ] || modes=" strict-none "
canonical_modes=""
add_modes() {
  _ifs=$IFS; IFS=,; set -- $1; IFS=$_ifs
  for _m in "$@"; do
    _m=$(printf '%s' "$_m" | tr -d ' \t\r\n')
    case "$_m" in
      strict-none|strict_none|strictnone|strict) _key=strict-none ;;
      default|safe) _key=default ;;
      ubsan|undefined) _key=ubsan ;;
      all) _key=all ;;
      '') continue ;;
      *) die "unknown mode: $_m (expected strict-none, default, ubsan, all)" ;;
    esac
    if [ "$_key" = all ]; then
      canonical_modes="$canonical_modes strict-none default ubsan"
    else
      case " $canonical_modes " in
        *" $_key "*) ;;
        *) canonical_modes="$canonical_modes $_key" ;;
      esac
    fi
  done
}
add_modes "$modes"
[ -n "$canonical_modes" ] || die "no mode selected"
modes=$canonical_modes

flags_for() {
  case "$1" in
    strict-none) MODE_FLAGS='--strict --sanitize=none' ;;
    default) MODE_FLAGS='' ;;
    ubsan) MODE_FLAGS='--strict --sanitize=undefined' ;;
  esac
}

# --- setup ----------------------------------------------------------------
mkdir -p "$OUT" || die "cannot create output directory: $OUT"
CASES="$OUT/.cases.$$"
mkdir -p "$CASES" || die "cannot create scratch directory: $CASES"
trap 'rm -rf "$CASES"' EXIT HUP INT TERM

command -v timeout >/dev/null 2>&1 || die "'timeout' utility not found; refusing unbounded tests"
command -v diff >/dev/null 2>&1 || die "'diff' utility not found; cannot verify fixtures"
[ -x "$CPLUS" ] || die "C+ compiler missing: $CPLUS (build it first; override with CPLUS_BIN)"
[ -n "$json_path" ] || json_path="$OUT/summary.json"

rel() { printf '%s' "${1#"$ROOT"/}"; }

# Escape a string for use inside a JSON double-quoted string.  All callers
# pass single-line values (newlines are folded when the value is built).
jesc() {
  printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/\t/\\t/g' -e 's/\r/\\r/g'
}

# Non-whitespace field separator for on-disk report rows.  Tab cannot be used:
# tab is IFS whitespace and `read` would collapse empty fields.
SEP=$(printf '\037')

c_PASS=0; c_FAIL=0; c_BUILD_FAILURE=0; c_TIMEOUT=0; c_SKIPPED=0; c_BLOCKED=0
bump() {
  case "$1" in
    PASS) c_PASS=$((c_PASS + 1)) ;;
    FAIL) c_FAIL=$((c_FAIL + 1)) ;;
    BUILD_FAILURE) c_BUILD_FAILURE=$((c_BUILD_FAILURE + 1)) ;;
    TIMEOUT) c_TIMEOUT=$((c_TIMEOUT + 1)) ;;
    SKIPPED) c_SKIPPED=$((c_SKIPPED + 1)) ;;
    BLOCKED) c_BLOCKED=$((c_BLOCKED + 1)) ;;
  esac
}

# emit <status> <name> <mode> <src> <build_rc> <run_rc> <expected_rc> \
#       <stdin_rel> <expectation> <self_report> <reason> <buildlog> \
#       <stdout> <stderr> <diff> <extra_flags> <skip_reasons>
emit() {
  _meta="$CASES/$(printf '%04d' "$seq").meta"
  seq=$((seq + 1))
  # Braces are required for positional parameters >= 10 in strict POSIX sh.
  printf '%s\n' "$1$SEP$2$SEP$3$SEP$4$SEP$5$SEP$6$SEP$7$SEP$8$SEP$9${SEP}${10}${SEP}${11}${SEP}${12}${SEP}${13}${SEP}${14}${SEP}${15}${SEP}${16}${SEP}${17}" > "$_meta"
  bump "$1"
  printf '%-14s %-24s %-14s %s\n' "$3" "$2" "$1" "${11}"
}

seq=0

# --- discovery ------------------------------------------------------------
discovered="$CASES/discovered"
: > "$discovered"
selected=0
for src in "$DIR"/*.cp; do
  [ -f "$src" ] || continue
  name=$(basename "$src" .cp)
  if [ -n "$only" ]; then
    matched=0
    for tok in $only; do
      if [ "$name" = "$tok" ]; then matched=1; break; fi
      case "$name" in *"$tok"*) matched=1; break ;; esac
    done
    [ "$matched" -eq 1 ] || continue
  fi
  printf '%s%s%s\n' "$name" "$SEP" "$src" >> "$discovered"
  selected=$((selected + 1))
done

if [ "$selected" -eq 0 ]; then
  if [ -n "$only" ]; then
    die "--only $only matched no *.cp test in $DIR"
  fi
  die "no *.cp tests found in $DIR (agents may still be writing them)"
fi

printf 'system_safety: %s test file(s), modes:%s\n' "$selected" "$modes"
printf '%-14s %-24s %-14s %s\n' MODE NAME STATUS DETAIL

# --- execution ------------------------------------------------------------
while IFS="$SEP" read -r name src; do
  # Optional per-test opt-out.  The first line of <name>.skip is the reason.
  skip_file="$DIR/$name.skip"

  # yyjson-backed tests need the same three separate argv operands used by
  # the rest of the repository (see tests/stdlib/run_deep_tests.sh).
  extra=''
  case "$name" in
    *json*) extra=1 ;;
  esac
  if grep -q 'stdlib/json\.hp\|yyjson' "$src" 2>/dev/null; then extra=1; fi
  if [ -n "$extra" ]; then
    extra="-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson"
  fi

  stdin_rel='-'
  stdin_file=''
  if [ -f "$DIR/$name.stdin" ]; then
    stdin_file="$DIR/$name.stdin"
    stdin_rel=$(rel "$stdin_file")
  fi

  expected_rc=0
  for rcf in "$DIR/$name.exit" "$DIR/$name.rc"; do
    [ -f "$rcf" ] || continue
    expected_rc=$(head -n 1 "$rcf" | tr -d ' \t\r\n')
    break
  done
  case "$expected_rc" in
    ''|*[!0-9]*) expected_rc='?' ;;
  esac

  for mode in $modes; do
    flags_for "$mode"
    bin="$OUT/${name}_${mode}"
    blog="$OUT/${name}_${mode}.build.log"
    so="$OUT/${name}_${mode}.stdout"
    se="$OUT/${name}_${mode}.stderr"
    diffout="$OUT/${name}_${mode}.diff"
    rm -f "$bin" "$blog" "$so" "$se" "$diffout"

    if [ -f "$skip_file" ]; then
      reason=$(head -n 1 "$skip_file" | tr '\n\t\r' '   ')
      [ -n "$reason" ] || reason='explicit .skip marker'
      emit SKIPPED "$name" "$mode" "$(rel "$src")" - - "$expected_rc" \
        "$stdin_rel" 'none' '-' "$reason" - - - - "$extra" -
      continue
    fi

    # ---- build (bounded) -------------------------------------------------
    # shellcheck disable=SC2086
    timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$src" \
      $MODE_FLAGS $extra -o "$bin" >"$blog" 2>&1
    brc=$?
    if [ "$brc" -eq 124 ] || [ "$brc" -ge 128 ]; then
      emit TIMEOUT "$name" "$mode" "$(rel "$src")" "$brc" - "$expected_rc" \
        "$stdin_rel" 'none' '-' "build exceeded ${BUILD_TIMEOUT}s (rc=$brc)" \
        "$(rel "$blog")" - - - "$extra" -
      continue
    fi
    if [ "$brc" -ne 0 ]; then
      # Known external blocker: c-plus-core/runtime/cplus_runtime.c uses
      # CPLUS_EXIT_ARITHMETIC, which this branch's runtime header lacks.
      # Default/UBSan builds compile that instrumented runtime; strict-none
      # does not.  Do NOT edit the runtime to work around it.
      if grep -q 'CPLUS_EXIT_ARITHMETIC' "$blog" 2>/dev/null; then
        emit BLOCKED "$name" "$mode" "$(rel "$src")" "$brc" - "$expected_rc" \
          "$stdin_rel" 'none' '-' \
          'external blocker: c-plus-core runtime uses CPLUS_EXIT_ARITHMETIC; absent from this branch runtime header' \
          "$(rel "$blog")" - - - "$extra" -
      else
        reason=$(head -n 1 "$blog" | tr '\n\t\r' '   ')
        [ -n "$reason" ] || reason="compiler exit $brc"
        emit BUILD_FAILURE "$name" "$mode" "$(rel "$src")" "$brc" - "$expected_rc" \
          "$stdin_rel" 'none' '-' "$reason" "$(rel "$blog")" - - - "$extra" -
      fi
      continue
    fi

    # ---- run (bounded, optional stdin fixture) --------------------------
    if [ -n "$stdin_file" ]; then
      timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$bin" <"$stdin_file" >"$so" 2>"$se"
    else
      timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$bin" </dev/null >"$so" 2>"$se"
    fi
    rrc=$?
    if [ "$rrc" -eq 124 ] || [ "$rrc" -ge 128 ]; then
      emit TIMEOUT "$name" "$mode" "$(rel "$src")" "$brc" "$rrc" "$expected_rc" \
        "$stdin_rel" 'none' '-' "run exceeded ${RUN_TIMEOUT}s (rc=$rrc)" \
        "$(rel "$blog")" "$(rel "$so")" "$(rel "$se")" - "$extra" -
      continue
    fi

    # Self-checking suites end with "<TAG> <checks> <failures>".
    final=$(tail -n 1 "$so")
    self_report='-'
    self_fail='-'
    self_report=$(awk 'END { if (NF==3 && $1 ~ /^[A-Z][A-Z0-9_]*$/ && $2 ~ /^[0-9]+$/ && $3 ~ /^[0-9]+$/) print $0; else print "-" }' "$so")
    if [ "$self_report" != '-' ]; then self_fail=${self_report##* }; fi

    skip_reasons=$(grep '^SKIP_REASON ' "$so" 2>/dev/null | head -n 3 | tr '\n\t\r' '   ')
    [ -n "$skip_reasons" ] || skip_reasons='-'

    expectation='exit-code-only'
    bad=0
    reason=''
    if [ "$expected_rc" = '?' ]; then
      bad=1; reason="invalid expected-rc fixture ($expected_rc)"
    elif [ "$rrc" -ne "$expected_rc" ]; then
      bad=1; reason="exit $rrc (expected $expected_rc)"
    fi
    if [ -f "$DIR/$name.expected" ]; then
      expectation="$expectation,stdout-fixture"
      diff -u "$DIR/$name.expected" "$so" >"$diffout" 2>&1
      drc=$?
      if [ "$drc" -ne 0 ]; then bad=1; reason="$reason stdout-mismatch"; fi
    fi
    if [ -f "$DIR/$name.stderr.expected" ]; then
      expectation="$expectation,stderr-fixture"
      diff -u "$DIR/$name.stderr.expected" "$se" >>"$diffout" 2>&1
      drc=$?
      if [ "$drc" -ne 0 ]; then bad=1; reason="$reason stderr-mismatch"; fi
    else
      # No stderr fixture: any output is suspicious (UBSan writes here).
      if [ -s "$se" ]; then bad=1; reason="$reason stderr"; fi
    fi
    if [ "$self_fail" != '-' ] && [ "$self_fail" -ne 0 ]; then
      bad=1; reason="$reason self-reported-failures=$self_fail"
    fi

    if [ -n "$reason" ]; then reason=$(printf '%s' "$reason" | sed 's/^ //' | tr '\n\t\r' '   '); fi
    if [ "$bad" -ne 0 ]; then
      [ -n "$reason" ] || reason='verification failed'
      emit FAIL "$name" "$mode" "$(rel "$src")" "$brc" "$rrc" "$expected_rc" \
        "$stdin_rel" "$expectation" "$self_report" "$reason" \
        "$(rel "$blog")" "$(rel "$so")" "$(rel "$se")" "$(rel "$diffout")" \
        "$extra" "$skip_reasons"
      head -n 4 "$se" | sed 's/^/           /'
    else
      detail="$final"
      [ -n "$detail" ] || detail="exit $rrc"
      emit PASS "$name" "$mode" "$(rel "$src")" "$brc" "$rrc" "$expected_rc" \
        "$stdin_rel" "$expectation" "$self_report" "$detail" \
        "$(rel "$blog")" "$(rel "$so")" "$(rel "$se")" - "$extra" "$skip_reasons"
    fi
  done
done < "$discovered"

# --- counts table ---------------------------------------------------------
printf '\n%-14s %8s %8s %8s %8s %8s %8s\n' MODE PASS FAIL BUILD TIMEOUT SKIP BLOCKED
for mode in $modes; do
  mp=0; mf=0; mb=0; mt=0; ms=0; mk=0
  for meta in "$CASES"/*.meta; do
    [ -f "$meta" ] || continue
    IFS="$SEP" read -r _st _nm _md _rest < "$meta"
    [ "$_md" = "$mode" ] || continue
    case "$_st" in
      PASS) mp=$((mp + 1)) ;;
      FAIL) mf=$((mf + 1)) ;;
      BUILD_FAILURE) mb=$((mb + 1)) ;;
      TIMEOUT) mt=$((mt + 1)) ;;
      SKIPPED) ms=$((ms + 1)) ;;
      BLOCKED) mk=$((mk + 1)) ;;
    esac
  done
  printf '%-14s %8s %8s %8s %8s %8s %8s\n' "$mode" "$mp" "$mf" "$mb" "$mt" "$ms" "$mk"
done

printf 'system_safety: %s passed, %s failed, %s build-failures, %s timeouts, %s skipped, %s blocked (%s file(s) x %s mode(s))\n' \
  "$c_PASS" "$c_FAIL" "$c_BUILD_FAILURE" "$c_TIMEOUT" "$c_SKIPPED" "$c_BLOCKED" \
  "$selected" "$(printf '%s\n' $modes | wc -l | tr -d ' ')"

# --- JSON summary ---------------------------------------------------------
mkdir -p "$(dirname "$json_path")" || die "cannot create JSON directory: $(dirname "$json_path")"
{
  printf '{\n'
  printf '  "suite": "system_safety",\n'
  printf '  "generated_at": "%s",\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  printf '  "root": "%s",\n' "$(jesc "$ROOT")"
  printf '  "compiler": "%s",\n' "$(jesc "$CPLUS")"
  printf '  "modes": ['
  _first=1
  for mode in $modes; do
    if [ "$_first" -eq 0 ]; then printf ', '; fi
    printf '"%s"' "$mode"; _first=0
  done
  printf '],\n'
  printf '  "filter": ['
  _first=1
  for tok in $only; do
    if [ "$_first" -eq 0 ]; then printf ', '; fi
    printf '"%s"' "$(jesc "$tok")"; _first=0
  done
  printf '],\n'
  printf '  "limits": {"build_timeout_s": %s, "run_timeout_s": %s, "kill_after_s": %s},\n' \
    "$BUILD_TIMEOUT" "$RUN_TIMEOUT" "$KILL_AFTER"
  printf '  "counts": {"PASS": %s, "FAIL": %s, "BUILD_FAILURE": %s, "TIMEOUT": %s, "SKIPPED": %s, "BLOCKED": %s},\n' \
    "$c_PASS" "$c_FAIL" "$c_BUILD_FAILURE" "$c_TIMEOUT" "$c_SKIPPED" "$c_BLOCKED"
  printf '  "cases": [\n'
  _first=1
  for meta in "$CASES"/*.meta; do
    [ -f "$meta" ] || continue
    IFS="$SEP" read -r st nm md sr brc rrc erc sin exp self reason blog sout serr dfx ext skip < "$meta"
    if [ "$_first" -eq 0 ]; then printf ',\n'; fi
    _first=0
    printf '    {"name": "%s", "mode": "%s", "source": "%s", "status": "%s",\n' \
      "$(jesc "$nm")" "$md" "$(jesc "$sr")" "$st"
    if [ "$brc" = '-' ]; then printf '     "build_rc": null,'; else printf '     "build_rc": %s,' "$brc"; fi
    if [ "$rrc" = '-' ]; then printf ' "run_rc": null,\n'; else printf ' "run_rc": %s,\n' "$rrc"; fi
    printf '     "expected_rc": %s, "stdin": "%s", "expectation": "%s",\n' \
      "$( [ "$erc" = '?' ] && printf 'null' || printf '%s' "$erc")" "$(jesc "$sin")" "$(jesc "$exp")"
    printf '     "self_report": "%s", "skip_reasons": "%s", "extra_flags": "%s",\n' \
      "$(jesc "$self")" "$(jesc "$skip")" "$(jesc "$ext")"
    printf '     "reason": "%s",\n' "$(jesc "$reason")"
    printf '     "logs": {"build": "%s", "stdout": "%s", "stderr": "%s", "diff": "%s"}}' \
      "$(jesc "$blog")" "$(jesc "$sout")" "$(jesc "$serr")" "$(jesc "$dfx")"
  done
  printf '\n  ]\n}\n'
} > "$json_path" || die "cannot write JSON summary: $json_path"

printf 'JSON summary: %s\n' "$(rel "$json_path")"
if [ "$json_stdout" -eq 1 ]; then cat "$json_path"; fi

if [ "$c_FAIL" -ne 0 ] || [ "$c_BUILD_FAILURE" -ne 0 ] || [ "$c_TIMEOUT" -ne 0 ] || [ "$c_BLOCKED" -ne 0 ]; then
  printf 'system_safety suite: FAIL (required cases did not pass)\n' >&2
  exit 1
fi
if [ "$c_PASS" -eq 0 ]; then
  printf 'system_safety suite: FAIL (no case passed)\n' >&2
  exit 1
fi
if [ "$c_SKIPPED" -ne 0 ]; then
  printf 'note: %s case(s) SKIPPED; skipped cases are not counted as passes\n' "$c_SKIPPED" >&2
fi
printf 'system_safety suite: PASS\n'
exit 0
