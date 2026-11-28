#!/bin/sh
# resource_exhaustion suite runner — bounded, deterministic, auto-discovering.
#
#   sh tests/security/resource_exhaustion/run.sh
#   sh tests/security/resource_exhaustion/run.sh --only re_fd_exhaust
#   sh tests/security/resource_exhaustion/run.sh --mode ubsan
#   sh tests/security/resource_exhaustion/run.sh --json-stdout
#
# Design rules:
#   * set -u only, never set -e: a failing case is recorded, not masked.
#   * EVERY build AND run is wrapped in `timeout -k <kill> <seconds>` (30 s).
#   * Modes run in a fixed order: strict-none FIRST, then ubsan.  A timeout is
#     TIMEOUT, never a PASS.
#   * UBSan builds need a build-only shim for an EXTERNAL compiler/runtime
#     interface mismatch (see NOTES.md): -DCPLUS_EXIT_ARITHMETIC=110.  No source
#     file is edited; the value 110 matches the external header.  The shim is
#     recorded in the JSON and is NOT a suppression of sanitizer findings.
#   * Output goes to <this dir>/.reout (gitignored by the *.reout pattern in
#     local tooling); nothing is written into the test source directory.
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT=${RE_OUT:-"$DIR/.reout"}
BUILD_TIMEOUT=${RE_BUILD_TIMEOUT:-30}
RUN_TIMEOUT=${RE_RUN_TIMEOUT:-30}
KILL_AFTER=${RE_KILL_AFTER:-5}
UBSAN_SHIM='-DCPLUS_EXIT_ARITHMETIC=110'

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

usage() {
  cat <<'EOF'
usage: sh tests/security/resource_exhaustion/run.sh [options]

Options:
  --only NAME      select a harness by basename/substring (repeatable)
  --mode LIST      strict-none (default), ubsan, or both; always ordered
                   strict-none first
  --json PATH      override JSON summary path (default: .reout/summary.json)
  --json-stdout    also print the JSON summary object on stdout
  -h, --help       show this message

Environment:
  CPLUS_BIN, RE_OUT, RE_BUILD_TIMEOUT, RE_RUN_TIMEOUT, RE_KILL_AFTER

Exit status: 0 = >=1 PASS and no FAIL/BUILD_FAILURE/TIMEOUT/BLOCKED;
             1 = a required case failed; 2 = setup/usage error.
EOF
}

die() { printf 'error: %s\n' "$*" >&2; exit 2; }

only=''
modes=''
json_path=''
json_stdout=0
while [ "$#" -gt 0 ]; do
  case "$1" in
    --only) [ "$#" -ge 2 ] || die '--only requires NAME'; only="$only $2"; shift 2 ;;
    --only=*) only="$only ${1#--only=}"; shift ;;
    --mode) [ "$#" -ge 2 ] || die '--mode requires LIST'; modes="$modes $2"; shift 2 ;;
    --mode=*) modes="$modes ${1#--mode=}"; shift ;;
    --json) [ "$#" -ge 2 ] || die '--json requires PATH'; json_path=$2; shift 2 ;;
    --json=*) json_path=${1#--json=}; shift ;;
    --json-stdout) json_stdout=1; shift ;;
    -h|--help) usage; exit 0 ;;
    *) die "unknown option: $1 (try --help)" ;;
  esac
done

# Fixed canonical order: strict-none is ALWAYS attempted before ubsan.
[ -n "$modes" ] || modes=' strict-none '
want_strict=0; want_ubsan=0
for tok in $modes; do
  case "$tok" in
    strict-none|strict_none|strict) want_strict=1 ;;
    ubsan|undefined|sanitize) want_ubsan=1 ;;
    all|both) want_strict=1; want_ubsan=1 ;;
    *) die "unknown mode: $tok (expected strict-none, ubsan, all)" ;;
  esac
done
canonical=''
[ "$want_strict" -eq 1 ] && canonical='strict-none'
[ "$want_ubsan" -eq 1 ] && canonical="$canonical ubsan"
[ -n "$canonical" ] || die 'no mode selected'

command -v timeout >/dev/null 2>&1 || die "'timeout' utility not found; refusing unbounded tests"
command -v diff >/dev/null 2>&1 || die "'diff' utility not found"
[ -x "$CPLUS" ] || die "C+ compiler missing: $CPLUS (override with CPLUS_BIN)"

mkdir -p "$OUT" || die "cannot create output directory: $OUT"
[ -n "$json_path" ] || json_path="$OUT/summary.json"
CASES="$OUT/.cases.$$"
mkdir -p "$CASES" || die "cannot create scratch directory: $CASES"
trap 'rm -rf "$CASES"' EXIT HUP INT TERM

rel() { printf '%s' "${1#"$ROOT"/}"; }
jesc() {
  printf '%s' "$1" | sed -e 's/\\/\\\\/g' -e 's/"/\\"/g' -e 's/\t/\\t/g' -e 's/\r/\\r/g'
}
SEP=$(printf '\037')

c_PASS=0; c_FAIL=0; c_BUILD=0; c_TIMEOUT=0; c_SKIP=0; c_BLOCK=0
bump() {
  case "$1" in
    PASS) c_PASS=$((c_PASS + 1)) ;;
    FAIL) c_FAIL=$((c_FAIL + 1)) ;;
    BUILD_FAILURE) c_BUILD=$((c_BUILD + 1)) ;;
    TIMEOUT) c_TIMEOUT=$((c_TIMEOUT + 1)) ;;
    SKIPPED) c_SKIP=$((c_SKIP + 1)) ;;
    BLOCKED) c_BLOCK=$((c_BLOCK + 1)) ;;
  esac
}

seq=0
emit() {  # status name mode src build_rc run_rc extra_flags reason blog so se diff skips
  meta="$CASES/$(printf '%04d' "$seq").meta"
  seq=$((seq + 1))
  printf '%s\n' "$1$SEP$2$SEP$3$SEP$4$SEP$5$SEP$6$SEP$7$SEP$8$SEP$9${SEP}${10}${SEP}${11}${SEP}${12}${SEP}${13}" > "$meta"
  bump "$1"
  printf '%-14s %-26s %-12s %s\n' "$3" "$2" "$1" "$8"
}

# discovery
discovered="$CASES/discovered"
: > "$discovered"
selected=0
for src in "$DIR"/re_*.cp; do
  [ -f "$src" ] || continue
  name=$(basename "$src" .cp)
  if [ -n "$only" ]; then
    matched=0
    for tok in $only; do
      [ "$name" = "$tok" ] && matched=1 && break
      case "$name" in *"$tok"*) matched=1; break ;; esac
    done
    [ "$matched" -eq 1 ] || continue
  fi
  printf '%s%s%s\n' "$name" "$SEP" "$src" >> "$discovered"
  selected=$((selected + 1))
done
[ "$selected" -gt 0 ] || die "no re_*.cp harness found in $DIR"

printf 'resource_exhaustion: %s harness(es), modes: %s\n' "$selected" "$canonical"
printf '%-14s %-26s %-12s %s\n' MODE NAME STATUS DETAIL

while IFS="$SEP" read -r name src; do
  extra=''
  if grep -q 'stdlib/json\.hp\|yyjson' "$src" 2>/dev/null; then
    extra="-I$ROOT/third_party/yyjson -L$ROOT/build/ext -lyyjson"
  fi
  skip_file="$DIR/$name.skip"

  for mode in $canonical; do
    mode_flags='--strict --sanitize=none'
    mode_shim=''
    if [ "$mode" = ubsan ]; then
      mode_flags='--strict --sanitize=undefined'
      mode_shim="$UBSAN_SHIM"
    fi
    all_flags="$mode_flags $mode_shim $extra"
    bin="$OUT/${name}_${mode}"
    blog="$OUT/${name}_${mode}.build.log"
    so="$OUT/${name}_${mode}.stdout"
    se="$OUT/${name}_${mode}.stderr"
    dfx="$OUT/${name}_${mode}.diff"
    rm -f "$bin" "$blog" "$so" "$se" "$dfx"

    if [ -f "$skip_file" ]; then
      reason=$(head -n 1 "$skip_file" | tr '\n\t\r' '   ')
      [ -n "$reason" ] || reason='explicit .skip marker'
      emit SKIPPED "$name" "$mode" "$(rel "$src")" - - "$all_flags" \
        "$reason" - - - - -
      continue
    fi

    # shellcheck disable=SC2086
    timeout -k "$KILL_AFTER" "$BUILD_TIMEOUT" "$CPLUS" build "$src" \
      $mode_flags $mode_shim $extra -o "$bin" >"$blog" 2>&1
    brc=$?
    if [ "$brc" -eq 124 ] || [ "$brc" -ge 128 ]; then
      emit TIMEOUT "$name" "$mode" "$(rel "$src")" "$brc" - "$all_flags" \
        "build exceeded ${BUILD_TIMEOUT}s (rc=$brc)" "$(rel "$blog")" - - - -
      continue
    fi
    if [ "$brc" -ne 0 ]; then
      if grep -q 'CPLUS_EXIT_ARITHMETIC' "$blog" 2>/dev/null; then
        emit BLOCKED "$name" "$mode" "$(rel "$src")" "$brc" - "$all_flags" \
          'external runtime/header mismatch: CPLUS_EXIT_ARITHMETIC' \
          "$(rel "$blog")" - - - -
      else
        reason=$(head -n 1 "$blog" | tr '\n\t\r' '   ')
        [ -n "$reason" ] || reason="compiler exit $brc"
        emit BUILD_FAILURE "$name" "$mode" "$(rel "$src")" "$brc" - \
          "$all_flags" "$reason" "$(rel "$blog")" - - - -
      fi
      continue
    fi

    timeout -k "$KILL_AFTER" "$RUN_TIMEOUT" "$bin" </dev/null >"$so" 2>"$se"
    rrc=$?
    if [ "$rrc" -eq 124 ] || [ "$rrc" -ge 128 ]; then
      emit TIMEOUT "$name" "$mode" "$(rel "$src")" "$brc" "$rrc" "$all_flags" \
        "run exceeded ${RUN_TIMEOUT}s (rc=$rrc)" "$(rel "$blog")" \
        "$(rel "$so")" "$(rel "$se")" - -
      continue
    fi

    final=$(tail -n 1 "$so")
    self=$(awk 'END { if (NF==3 && $1=="RESOURCE_EXHAUSTION" && $2 ~ /^[0-9]+$/ && $3 ~ /^[0-9]+$/) print $0; else print "-" }' "$so")
    skips=$(grep '^SKIP_REASON ' "$so" 2>/dev/null | head -n 3 | tr '\n\t\r' '   ')
    [ -n "$skips" ] || skips='-'
    bad=0; reason=''
    [ "$rrc" -ne 0 ] && { bad=1; reason="exit $rrc"; }
    if [ "$self" = '-' ]; then
      bad=1; reason="$reason missing-self-report"
    else
      sf=${self##* }
      [ "$sf" -ne 0 ] && { bad=1; reason="$reason self-failures=$sf"; }
    fi
    if [ -s "$se" ]; then bad=1; reason="$reason stderr"; fi
    if [ -n "$reason" ]; then
      reason=$(printf '%s' "$reason" | sed 's/^ //' | tr '\n\t\r' '   ')
    fi

    if [ "$bad" -ne 0 ]; then
      [ -n "$reason" ] || reason='verification failed'
      emit FAIL "$name" "$mode" "$(rel "$src")" "$brc" "$rrc" "$all_flags" \
        "$reason" "$(rel "$blog")" "$(rel "$so")" "$(rel "$se")" "$(rel "$dfx")" "$skips"
      head -n 4 "$se" | sed 's/^/           /'
    else
      detail="$final"
      [ -n "$detail" ] || detail="exit $rrc"
      emit PASS "$name" "$mode" "$(rel "$src")" "$brc" "$rrc" "$all_flags" \
        "$detail" "$(rel "$blog")" "$(rel "$so")" "$(rel "$se")" - "$skips"
    fi
  done
done < "$discovered"

printf '\n%-12s %7s %6s %7s %8s %7s %8s\n' MODE PASS FAIL BUILD TIMEOUT SKIP BLOCKED
for mode in $canonical; do
  mp=0; mf=0; mb=0; mt=0; ms=0; mk=0
  for meta in "$CASES"/*.meta; do
    [ -f "$meta" ] || continue
    IFS="$SEP" read -r st _nm md _rest < "$meta"
    [ "$md" = "$mode" ] || continue
    case "$st" in
      PASS) mp=$((mp + 1)) ;; FAIL) mf=$((mf + 1)) ;;
      BUILD_FAILURE) mb=$((mb + 1)) ;; TIMEOUT) mt=$((mt + 1)) ;;
      SKIPPED) ms=$((ms + 1)) ;; BLOCKED) mk=$((mk + 1)) ;;
    esac
  done
  printf '%-12s %7s %6s %7s %8s %7s %8s\n' "$mode" "$mp" "$mf" "$mb" "$mt" "$ms" "$mk"
done
printf 'resource_exhaustion: %s passed, %s failed, %s build-failures, %s timeouts, %s skipped, %s blocked\n' \
  "$c_PASS" "$c_FAIL" "$c_BUILD" "$c_TIMEOUT" "$c_SKIP" "$c_BLOCK"

mkdir -p "$(dirname "$json_path")" || die "cannot create JSON directory"
{
  printf '{\n'
  printf '  "suite": "resource_exhaustion",\n'
  printf '  "generated_at": "%s",\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
  printf '  "root": "%s",\n' "$(jesc "$ROOT")"
  printf '  "compiler": "%s",\n' "$(jesc "$CPLUS")"
  printf '  "ubsan_build_shim": "%s",\n' "$UBSAN_SHIM"
  printf '  "modes": ['
  mfirst=1
  for mode in $canonical; do
    [ "$mfirst" -eq 0 ] && printf ', '
    printf '"%s"' "$mode"; mfirst=0
  done
  printf '],\n'
  printf '  "limits": {"build_timeout_s": %s, "run_timeout_s": %s, "kill_after_s": %s},\n' \
    "$BUILD_TIMEOUT" "$RUN_TIMEOUT" "$KILL_AFTER"
  printf '  "counts": {"PASS": %s, "FAIL": %s, "BUILD_FAILURE": %s, "TIMEOUT": %s, "SKIPPED": %s, "BLOCKED": %s},\n' \
    "$c_PASS" "$c_FAIL" "$c_BUILD" "$c_TIMEOUT" "$c_SKIP" "$c_BLOCK"
  printf '  "cases": [\n'
  first=1
  for meta in "$CASES"/*.meta; do
    [ -f "$meta" ] || continue
    IFS="$SEP" read -r st nm md sr brc rrc flags reason blog so se dfx skips < "$meta"
    [ "$first" -eq 0 ] && printf ',\n'
    first=0
    printf '    {"name": "%s", "mode": "%s", "status": "%s", "source": "%s",\n' \
      "$(jesc "$nm")" "$md" "$st" "$(jesc "$sr")"
    if [ "$brc" = '-' ]; then printf '     "build_rc": null,'; else printf '     "build_rc": %s,' "$brc"; fi
    if [ "$rrc" = '-' ]; then printf ' "run_rc": null,\n'; else printf ' "run_rc": %s,\n' "$rrc"; fi
    printf '     "flags": "%s", "reason": "%s", "skip_reasons": "%s",\n' \
      "$(jesc "$flags")" "$(jesc "$reason")" "$(jesc "$skips")"
    printf '     "logs": {"build": "%s", "stdout": "%s", "stderr": "%s", "diff": "%s"}}' \
      "$(jesc "$blog")" "$(jesc "$so")" "$(jesc "$se")" "$(jesc "$dfx")"
  done
  printf '\n  ]\n}\n'
} > "$json_path" || die "cannot write JSON summary: $json_path"

printf 'JSON summary: %s\n' "$(rel "$json_path")"
[ "$json_stdout" -eq 1 ] && cat "$json_path"

if [ "$c_FAIL" -ne 0 ] || [ "$c_BUILD" -ne 0 ] || [ "$c_TIMEOUT" -ne 0 ] || [ "$c_BLOCK" -ne 0 ]; then
  printf 'resource_exhaustion suite: FAIL\n' >&2; exit 1
fi
[ "$c_PASS" -eq 0 ] && { printf 'resource_exhaustion suite: FAIL (no case passed)\n' >&2; exit 1; }
[ "$c_SKIP" -ne 0 ] && printf 'note: %s case(s) SKIPPED; not counted as passes\n' "$c_SKIP" >&2
printf 'resource_exhaustion suite: PASS\n'
exit 0
