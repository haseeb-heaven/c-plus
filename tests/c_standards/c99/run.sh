#!/usr/bin/env bash
# =============================================================================
# tests/c_standards/c99/run.sh
#
# Evidence harness for C94 / C99 standard-spelling support in the C+ compiler
# driver.  It compiles two minimal genuine sources and three real downloaded
# projects for every std spelling, records exact commands / exits / diagnostics,
# runs successful binaries under a 30-second timeout, and byte-compares their
# outputs against expected values captured from the original upstream builds.
#
# Run from anywhere:
#   bash tests/c_standards/c99/run.sh
#
# All artifacts stay under tests/c_standards/c99/ (.work/ and logs/).
# This script never modifies external_projects/ or any source file.
# It always exits 0: it is an evidence collector, not a test gate.
# =============================================================================
set -u
set -o pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../../.." && pwd)"
cd "$REPO_ROOT" || exit 0

CLANG=/usr/bin/clang
CP=./build/c+
TIMEOUT=/usr/bin/timeout
HERE=tests/c_standards/c99
SRC=$HERE
WORK=$HERE/.work
LOGS=$HERE/logs
EXP=$HERE/expected

STDS=(c94 iso9899:199409 c99 c9x gnu99)

rm -rf "$WORK"
mkdir -p "$WORK" "$LOGS"
: >"$LOGS/SUMMARY.txt"
: >"$LOGS/results.tsv"

summary() { printf '%s\n' "$*" | tee -a "$LOGS/SUMMARY.txt"; }
tsv() { printf '%s\n' "$*" >>"$LOGS/results.tsv"; }

# run_case NAME STDIN_FILE CMD...
# Writes NAME.cmd (exact argv), NAME.stdout, NAME.stderr, NAME.exit.
# Echoes the exit code on stdout for callers.
run_case() {
  local name="$1" stdin="$2"
  shift 2
  {
    printf 'COMMAND:'
    printf ' %q' "$@"
    printf '\nSTDIN: %s\n' "$stdin"
  } >"$LOGS/$name.cmd"
  "$@" <"$stdin" >"$LOGS/$name.stdout" 2>"$LOGS/$name.stderr"
  local ec=$?
  printf '%d\n' "$ec" >"$LOGS/$name.exit"
  printf '%d' "$ec"
}

exit_of() { cat "$LOGS/$1.exit"; }

# match NAME EXPECTED_FILE -> echoes MATCH / MISMATCH / NO-RUN
match() {
  local name="$1" exp="$2"
  if [ "$(exit_of "$name")" != "0" ]; then
    printf 'NO-RUN'
  elif cmp -s "$LOGS/$name.stdout" "$exp"; then
    printf 'MATCH'
  else
    printf 'MISMATCH'
  fi
}

tag() { printf '%s' "$1" | tr ':' '_'; }

# ---------------------------------------------------------------------------
# Deterministic fixture for xdump (same 52 bytes as the original evidence).
# ---------------------------------------------------------------------------
python3 - "$WORK/sample.bin" <<'PY'
import sys
data = bytes(range(0x20)) + b"Hello, xdump!\n" + bytes([0x7f, 0x80, 0xc3, 0xa9, 0xff, 0x00])
with open(sys.argv[1], "wb") as f:
    f.write(data)
PY
printf 'q' >"$WORK/q.in"

summary "C99/C94 STANDARD-SPELLING EVIDENCE  (generated $(date -u '+%Y-%m-%dT%H:%M:%SZ'))"
summary "clang: $(/usr/bin/clang --version | head -1)"
summary "driver: $CP"
summary ""

# ===========================================================================
# PART 1+2: minimal genuine sources x 5 spellings x {clang ref, c+, c++pedantic}
# ===========================================================================
summary "=== PART 2: minimal-source compile matrix ==="
for src in c99_features c94_features; do
  for std in "${STDS[@]}"; do
    t=$(tag "$std")
    base="${src}_${t}"

    # Reference: plain clang, strict ISO diagnostics.
    run_case "ref_build_$base" /dev/null \
      "$CLANG" -std="$std" -pedantic-errors "$SRC/$src.c" -o "$WORK/${base}_ref" >/dev/null
    rec=$(exit_of "ref_build_$base")
    tsv "ref_compile	$src	$std	$rec"
    if [ "$rec" -eq 0 ]; then
      run_case "ref_run_$base" /dev/null "$WORK/${base}_ref" >/dev/null
      m=$(match "ref_run_$base" "$EXP/$src.stdout")
      tsv "ref_run	$src	$std	$(exit_of "ref_run_$base")	$m"
    else
      m=SKIPPED
      tsv "ref_run	$src	$std	-	SKIPPED"
    fi
    summary "ref      $src  -std=$std  build_exit=$rec  run_match=$m"

    # C+ driver, exact required command form (Safe mode default).
    run_case "cp_build_$base" /dev/null \
      "$CP" build "$SRC/$src.c" -std="$std" --sanitize=none -o "$WORK/${base}_cp" >/dev/null
    rec=$(exit_of "cp_build_$base")
    tsv "cp_compile	$src	$std	$rec"
    if [ "$rec" -eq 0 ]; then
      run_case "cp_run_$base" /dev/null "$WORK/${base}_cp" >/dev/null
      m=$(match "cp_run_$base" "$EXP/$src.stdout")
      tsv "cp_run	$src	$std	$(exit_of "cp_run_$base")	$m"
    else
      m=SKIPPED
      tsv "cp_run	$src	$std	-	SKIPPED"
    fi
    summary "c+       $src  -std=$std  build_exit=$rec  run_match=$m"

    # Extra parity probe: same C+ command with -pedantic-errors forwarded.
    run_case "cpped_build_$base" /dev/null \
      "$CP" build "$SRC/$src.c" -std="$std" -pedantic-errors --sanitize=none \
        -o "$WORK/${base}_cpped" >/dev/null
    rec=$(exit_of "cpped_build_$base")
    tsv "cp_pedantic_compile	$src	$std	$rec"
    summary "c+ped    $src  -std=$std  build_exit=$rec"
  done
done
summary ""

# ===========================================================================
# PART 3: real downloaded projects.  For each spelling tested, compile via the
# C+ driver both in default Safe mode and in --legacy-c mode (needed because
# the unmodified upstream sources are not C+ memory-safe), then run under
# timeout 30 and byte-compare against the original upstream outputs.
# ===========================================================================
summary "=== PART 3: real-project compile + run under C+ driver ==="
PROJECT_STDS=(c99 iso9899:199409 c94)

# --- helper: build a project and record exactly one build case --------------
build_project() {
  local proj="$1" src="$2" std="$3" mode="$4" name="$5"
  local out="$WORK/${proj}_$(tag "$std")_${mode}"
  if [ "$mode" = "legacy" ]; then
    run_case "$name" /dev/null \
      "$CP" build "$src" -std="$std" --sanitize=none --legacy-c -o "$out" >/dev/null
  else
    run_case "$name" /dev/null \
      "$CP" build "$src" -std="$std" --sanitize=none -o "$out" >/dev/null
  fi
  printf '%s' "$out"
}

for std in "${PROJECT_STDS[@]}"; do
  t=$(tag "$std")
  summary "-- std=$std --"

  # ---- game_a: snake -------------------------------------------------------
  for mode in safe legacy; do
    name="proj_snake_${t}_${mode}_build"
    bin=$(build_project snake external_projects/game_a/original/snake.c "$std" "$mode" "$name")
    rec=$(exit_of "$name")
    tsv "project_build	snake	$std	$mode	$rec"
    summary "snake    $std  $mode  build_exit=$rec"
    if [ "$rec" -eq 0 ]; then
      rname="proj_snake_${t}_${mode}_run"
      run_case "$rname" "$WORK/q.in" "$TIMEOUT" 30 "$bin" >/dev/null
      m=$(match "$rname" "$EXP/snake.stdout")
      tsv "project_run	snake	$std	$mode	$(exit_of "$rname")	$m"
      summary "snake    $std  $mode  run_exit=$(exit_of "$rname")  output=$m"
    else
      tsv "project_run	snake	$std	$mode	-	SKIPPED"
    fi
  done

  # ---- game_b: 2048 --------------------------------------------------------
  for mode in safe legacy; do
    name="proj_2048_${t}_${mode}_build"
    bin=$(build_project 2048 external_projects/game_b/original/2048.c "$std" "$mode" "$name")
    rec=$(exit_of "$name")
    tsv "project_build	2048	$std	$mode	$rec"
    summary "2048     $std  $mode  build_exit=$rec"
    if [ "$rec" -eq 0 ]; then
      for scen in test version help; do
        case "$scen" in
          test) arg=test; exp=2048_test.stdout ;;
          version) arg=--version; exp=2048_version.stdout ;;
          help) arg=--help; exp=2048_help.stdout ;;
        esac
        rname="proj_2048_${t}_${mode}_${scen}"
        run_case "$rname" /dev/null "$TIMEOUT" 30 "$bin" "$arg" >/dev/null
        m=$(match "$rname" "$EXP/$exp")
        tsv "project_run	2048	$std	$mode	$scen	$(exit_of "$rname")	$m"
        summary "2048     $std  $mode  $scen  run_exit=$(exit_of "$rname")  output=$m"
      done
    else
      tsv "project_run	2048	$std	$mode	-	SKIPPED"
    fi
  done

  # ---- cli_app: xdump ------------------------------------------------------
  for mode in safe legacy; do
    name="proj_xdump_${t}_${mode}_build"
    bin=$(build_project xdump external_projects/cli_app/original/xdump.c "$std" "$mode" "$name")
    rec=$(exit_of "$name")
    tsv "project_build	xdump	$std	$mode	$rec"
    summary "xdump    $std  $mode  build_exit=$rec"
    if [ "$rec" -eq 0 ]; then
      for scen in version help file_default; do
        case "$scen" in
          version) arg=-v; exp=xdump_version.stdout; input=/dev/null ;;
          help) arg=-h; exp=xdump_help.stdout; input=/dev/null ;;
          file_default) arg="$WORK/sample.bin"; exp=xdump_file_default.stdout; input=/dev/null ;;
        esac
        rname="proj_xdump_${t}_${mode}_${scen}"
        run_case "$rname" "$input" "$TIMEOUT" 30 "$bin" "$arg" >/dev/null
        m=$(match "$rname" "$EXP/$exp")
        tsv "project_run	xdump	$std	$mode	$scen	$(exit_of "$rname")	$m"
        summary "xdump    $std  $mode  $scen  run_exit=$(exit_of "$rname")  output=$m"
      done
    else
      tsv "project_run	xdump	$std	$mode	-	SKIPPED"
    fi
  done
done

summary ""
summary "Detailed command/exit/stdout/stderr for every case: $LOGS/<case>.{cmd,stdout,stderr,exit}"
summary "Machine-readable table: $LOGS/results.tsv"
printf '\n'
cat "$LOGS/SUMMARY.txt"
exit 0
