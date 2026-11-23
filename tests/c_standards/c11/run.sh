#!/usr/bin/env bash
# C11 standard-support evidence for the C+ compiler driver.
#
# Reproduces, from scratch, every claim in RESULTS.md.  All files it creates
# live under tests/c_standards/c11/.  Nothing under external_projects/ is
# modified (those trees are only read).  No git add/commit is performed.
#
# Usage:  bash tests/c_standards/c11/run.sh
set -uo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
cd "$REPO" || exit 1

CPLUS="$REPO/build/c+"
CLANG=/usr/bin/clang
OUT="$HERE/artifacts"
REPORT="$OUT/run-report.log"

if [ ! -x "$CPLUS" ]; then
  echo "FATAL: $CPLUS not found; build the driver first." >&2
  exit 1
fi
if [ ! -x "$CLANG" ]; then
  echo "FATAL: $CLANG not found." >&2
  exit 1
fi
if ! command -v timeout >/dev/null 2>&1; then
  echo "FATAL: timeout(1) not found." >&2
  exit 1
fi

mkdir -p "$OUT"
: > "$REPORT"
log() { printf '%s\n' "$*" | tee -a "$REPORT"; }
run() { # run <label> <cmd...> ; echoes exit code, appends to report
  local label="$1"; shift
  local rc
  "$@" >>"$REPORT" 2>&1
  rc=$?
  log "  [$label] exit=$rc"
  return $rc
}

SRC="$HERE/source.c"
log "======================================================================"
log "C11 standard-support evidence  (repo: $REPO)"
log "C+ driver : $CPLUS"
log "clang     : $($CLANG --version | head -1)"
log "date      : $(date -u '+%Y-%m-%dT%H:%M:%SZ')"
log "======================================================================"

# ----------------------------------------------------------------------
# 1. Reference build: plain clang -std=c11 -pedantic-errors
# ----------------------------------------------------------------------
log ""
log "SECTION 1 -- reference compile (plain clang, -std=c11 -pedantic-errors)"
log "CMD: $CLANG -std=c11 -pedantic-errors tests/c_standards/c11/source.c -o artifacts/ref_c11"
run ref-c11 "$CLANG" -std=c11 -pedantic-errors "$SRC" -o "$OUT/ref_c11"
log "CMD: artifacts/ref_c11"
run ref-run "$OUT/ref_c11"

# Plain-clang reference for the other two spellings (comparison only).
for s in c1x gnu11; do
  log "CMD: $CLANG -std=$s -pedantic-errors tests/c_standards/c11/source.c -o artifacts/ref_$s"
  run "ref-$s" "$CLANG" -std=$s -pedantic-errors "$SRC" -o "$OUT/ref_$s"
  if [ -x "$OUT/ref_$s" ]; then
    log "CMD: artifacts/ref_$s"
    run "ref-run-$s" "$OUT/ref_$s"
  fi
done

# Negative control: the source is genuinely C11-only under a pedantic C99 compiler.
log "CMD: $CLANG -std=c99 -pedantic-errors ... source.c  (expected FAIL)"
run ref-c99-negative "$CLANG" -std=c99 -pedantic-errors "$SRC" -o "$OUT/ref_c99_fail"

# ----------------------------------------------------------------------
# 2. C+ driver with -std=c11 / c1x / gnu11 (minimal C11 program)
# ----------------------------------------------------------------------
log ""
log "SECTION 2 -- C+ driver, each -std spelling, minimal C11 program"
for s in c11 c1x gnu11; do
  log "CMD: ./build/c+ build tests/c_standards/c11/source.c -std=$s --sanitize=none -o artifacts/src_$s --verbose"
  ./build/c+ build "$SRC" -std=$s --sanitize=none -o "$OUT/src_$s" --verbose >"$OUT/src_$s.verbose.log" 2>&1
  rc=$?
  log "  [cplus-build-$s] exit=$rc"
  # Show the effective -std token actually handed to clang's cc1.
  std_tokens=$(grep -o -- '-std=[a-z0-9]*' "$OUT/src_$s.verbose.log" | sort | uniq -c | tr '\n' ' ')
  log "  [cplus-build-$s] -std tokens seen in verbose log: $std_tokens"
  if [ $rc -eq 0 ] && [ -x "$OUT/src_$s" ]; then
    log "CMD: artifacts/src_$s"
    run "cplus-run-$s" "$OUT/src_$s"
  fi
  # Rebuild without --verbose to get clean build diagnostics.
  ./build/c+ build "$SRC" -std=$s --sanitize=none -o "$OUT/src_$s.clean" >"$OUT/src_$s.build.log" 2>&1
  log "  [cplus-clean-build-$s] exit=$? ; diagnostics bytes=$(wc -c < "$OUT/src_$s.build.log")"
done

# Strictness caveat: the C+ driver does not add -pedantic-errors, so a
# C11-only source also builds under its -std=c99 spelling (clang accepts the
# C11 constructs as extensions). This is documented, not a C11 failure.
log "CMD: ./build/c+ build source.c -std=c99 --sanitize=none -o artifacts/src_c99  (strictness caveat)"
./build/c+ build "$SRC" -std=c99 --sanitize=none -o "$OUT/src_c99" >"$OUT/src_c99.build.log" 2>&1
log "  [cplus-build-c99-of-C11-source] exit=$? ; diagnostics bytes=$(wc -c < "$OUT/src_c99.build.log")"

# ----------------------------------------------------------------------
# 3. Real downloaded projects via the C+ driver with -std=c11
# ----------------------------------------------------------------------
log ""
log "SECTION 3 -- real projects, C+ driver -std=c11"
log "NOTE: C+ SAFE mode rejects these legacy C sources for C+ *safety*"
log "      reasons (bounds/lifetime/FFI), not for C11-standard reasons."
log "      --legacy-c is the documented mode for compiling legacy C and is"
log "      used here. The -std=c11 spelling is still passed verbatim to clang."

declare -A PROJ_SRC=(
  [game_a]=external_projects/game_a/original/snake.c
  [game_b]=external_projects/game_b/original/2048.c
  [cli_app]=external_projects/cli_app/original/xdump.c
)
for name in game_a game_b cli_app; do
  # Document SAFE-mode failure first.
  ./build/c+ build "${PROJ_SRC[$name]}" -std=c11 --sanitize=none -o "$OUT/${name}_safe" >"$OUT/${name}_safe.build.log" 2>&1
  log "  [SAFE-mode $name] exit=$? (diagnostics in artifacts/${name}_safe.build.log)"
  # Actual build in legacy-C compatibility mode.
  log "CMD: ./build/c+ build ${PROJ_SRC[$name]} -std=c11 --sanitize=none --legacy-c -o artifacts/${name}_c11"
  ./build/c+ build "${PROJ_SRC[$name]}" -std=c11 --sanitize=none --legacy-c -o "$OUT/${name}_c11" >"$OUT/${name}_c11.build.log" 2>&1
  rc=$?
  log "  [legacy-c $name build] exit=$rc ; diagnostics bytes=$(wc -c < "$OUT/${name}_c11.build.log")"
done

# Verify -std=c11 is still the effective cc1 standard under --legacy-c.
log "CMD: ./build/c+ build snake.c -std=c11 --sanitize=none --legacy-c --verbose  (std-token check)"
./build/c+ build "${PROJ_SRC[game_a]}" -std=c11 --sanitize=none --legacy-c -o "$OUT/stdcheck" --verbose >"$OUT/stdcheck.verbose.log" 2>&1
log "  [legacy-c std tokens in verbose log] $(grep -o -- '-std=[a-z0-9]*' "$OUT/stdcheck.verbose.log" | sort | uniq -c | tr '\n' ' ')"

# --- game_a: deterministic 'q' quit -----------------------------------
log ""
log "GAME_A: printf 'q' | timeout 30 artifacts/game_a_c11"
printf 'q' | timeout 30 "$OUT/game_a_c11" >"$OUT/game_a.run.stdout" 2>"$OUT/game_a.run.stderr"
log "  [game_a run] exit=$? ; stdout bytes=$(wc -c < "$OUT/game_a.run.stdout") ; stderr bytes=$(wc -c < "$OUT/game_a.run.stderr")"
cmp -s "$OUT/game_a.run.stdout" external_projects/game_a/build/run.stdout
log "  [game_a stdout vs recorded original] cmp_exit=$? (0 = byte-identical)"

# --- game_b: deterministic modes + stdin quit -------------------------
log ""
for mode in test --version --help; do
  tag=${mode#--}
  log "GAME_B: timeout 30 artifacts/game_b_c11 $mode"
  timeout 30 "$OUT/game_b_c11" $mode >"$OUT/game_b_${tag}.stdout" 2>"$OUT/game_b_${tag}.stderr"
  log "  [game_b $mode] exit=$? ; stdout bytes=$(wc -c < "$OUT/game_b_${tag}.stdout")"
  cmp -s "$OUT/game_b_${tag}.stdout" "external_projects/game_b/run_${tag}_stdout.log"
  log "  [game_b $mode stdout vs recorded original] cmp_exit=$?"
done
log "GAME_B: timeout 30 artifacts/game_b_c11 < external_projects/game_b/quit_input.txt  (stdin 'qy')"
timeout 30 "$OUT/game_b_c11" < external_projects/game_b/quit_input.txt >"$OUT/game_b_stdin.stdout" 2>"$OUT/game_b_stdin.stderr"
log "  [game_b stdin quit] exit=$? ; stdout bytes=$(wc -c < "$OUT/game_b_stdin.stdout") ; ref bytes=$(wc -c < external_projects/game_b/run_game_stdin_stdout.log)"
log "  [game_b stdin quit] QUIT prompt count=$(grep -c 'QUIT?' "$OUT/game_b_stdin.stdout") ; keyboard-error count=$(grep -c 'Cannot read keyboard' "$OUT/game_b_stdin.stdout")"
cmp -s "$OUT/game_b_stdin.stdout" external_projects/game_b/run_game_stdin_stdout.log
log "  [game_b stdin quit exact byte cmp] cmp_exit=$? (board is time-seeded, so byte differences are expected)"

# --- cli_app: fixed argv/stdin cases against recorded originals -------
log ""
log "CLI_APP: fixture copied from external_projects (read-only) -> artifacts/cli_fixtures/sample.bin"
mkdir -p "$OUT/cli_fixtures"
cp external_projects/cli_app/build/fixtures/sample.bin "$OUT/cli_fixtures/sample.bin"
FIX="$OUT/cli_fixtures/sample.bin"
CBIN="$OUT/cli_app_c11"
cli_case() {
  local tag="$1"; shift
  log "CLI_APP: timeout 30 artifacts/cli_app_c11 $*"
  timeout 30 "$CBIN" "$@" >"$OUT/cli_${tag}.stdout" 2>"$OUT/cli_${tag}.stderr"
  log "  [cli_app $tag] exit=$? ; stdout bytes=$(wc -c < "$OUT/cli_${tag}.stdout") ; stderr bytes=$(wc -c < "$OUT/cli_${tag}.stderr")"
  cmp -s "$OUT/cli_${tag}.stdout" "external_projects/cli_app/build/runs/${tag}.stdout"
  log "  [cli_app $tag stdout vs recorded original] cmp_exit=$?"
}
cli_case version -v
cli_case help -h
cli_case file_default "$FIX"
cli_case file_width8 -w 8 -g 4 "$FIX"
cli_case file_skip_n -s 32 -n 16 "$FIX"
log "CLI_APP: timeout 30 artifacts/cli_app_c11 < fixture"
timeout 30 "$CBIN" < "$FIX" >"$OUT/cli_stdin_default.stdout" 2>"$OUT/cli_stdin_default.stderr"
log "  [cli_app stdin_default] exit=$?"
cmp -s "$OUT/cli_stdin_default.stdout" external_projects/cli_app/build/runs/stdin_default.stdout
log "  [cli_app stdin_default stdout vs recorded original] cmp_exit=$?"
cli_case bad_flag -Z
cli_case missing_file /definitely/not/a/file

log ""
log "======================================================================"
log "END OF REPORT. Full raw command output above; see RESULTS.md for summary."
log "======================================================================"
echo "Report written to $REPORT"
