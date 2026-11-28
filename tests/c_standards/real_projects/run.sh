#!/usr/bin/env bash
# Real-downloaded-project C-standard matrix for the C+ compiler.
#
# Scope (strict): reads only from external_projects/{game_a,game_b,cli_app}/original/
# and from the preserved golden logs in those projects' build/ directories.
# Writes ONLY under tests/c_standards/real_projects/ (generated data under logs/).
#
# Primary matrix (A): the exact command required by the task,
#   ./build/c+ build <file> -std=<val> --sanitize=none -o <bin>
# Supplementary matrix (B): same command plus --legacy-c ("legacy C compatibility
# mode"), so that unmodified real C sources can pass the C+ default-mode safety
# gate.  Without B, the default-mode safety diagnostics (bounds/lifetime/FFI)
# fire identically for every -std value and no C-version signal is observable.
#
# Nothing is fabricated: every COMPILE_* / RUN_* cell is derived from an actual
# c+ invocation and, on compile success, actual `timeout 30` runs compared
# byte-for-byte (or by fixed markers for the randomly-seeded 2048 stdin game)
# against the ORIGINAL_EVIDENCE.md golden outputs preserved on disk.

set -u

# --- locate repo root from this script's own path -----------------------------
HERE="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
ROOT="$(cd -- "$HERE/../../.." && pwd)"
cd -- "$ROOT" || exit 1

BASE="tests/c_standards/real_projects"
LOG="$BASE/logs"
CELLS="$LOG/cells"
BINS="$LOG/bins"
RUNS="$LOG/runs"
mkdir -p -- "$CELLS" "$BINS" "$RUNS"

CPLUS="./build/c+"
CLANG="/usr/bin/clang"
TIMEOUT="$(command -v timeout || true)"
[ -n "$TIMEOUT" ] || { echo "timeout(1) not found" >&2; exit 1; }
PY="$(command -v python3 || true)"
[ -n "$PY" ] || { echo "python3 not found" >&2; exit 1; }

# --- projects, sources, golden logs ------------------------------------------
SRC_GAME_A="external_projects/game_a/original/snake.c"
SRC_GAME_B="external_projects/game_b/original/2048.c"
SRC_CLI_APP="external_projects/cli_app/original/xdump.c"
FIXTURE="external_projects/cli_app/build/fixtures/sample.bin"

GOLD_A_STDOUT="external_projects/game_a/build/run.stdout"
GOLD_A_STDERR=""   # empty
GOLD_B="external_projects/game_b"
GOLD_X="external_projects/cli_app/build/runs"

# --- probe which -std spellings clang accepts --------------------------------
PROBE_LIST="c89 c90 c94 c99 c11 c17 c23 c2x gnu89 gnu99 gnu11 gnu17 gnu2x"
ACCEPTED=""
: > "$LOG/clang_std_probe.txt"
for s in $PROBE_LIST; do
  printf 'int main(void){return 0;}\n' | "$CLANG" -x c -std="$s" - -o /dev/null \
      >"$LOG/.probe.$s.out" 2>"$LOG/.probe.$s.err"
  rc=$?
  if [ "$rc" -eq 0 ]; then
    ACCEPTED="$ACCEPTED $s"
    printf '%-8s ACCEPTED\n' "$s" | tee -a "$LOG/clang_std_probe.txt"
  else
    printf '%-8s REJECTED rc=%d %s\n' "$s" "$rc" \
      "$(head -1 "$LOG/.probe.$s.err")" | tee -a "$LOG/clang_std_probe.txt"
  fi
  rm -f -- "$LOG/.probe.$s.out" "$LOG/.probe.$s.err"
done
ACCEPTED=${ACCEPTED# }
# shellcheck disable=SC2086
set -- $ACCEPTED
NSTD=$#
echo "clang-accepted std spellings ($NSTD): $ACCEPTED"

# --- helpers ------------------------------------------------------------------
# cmpfiles A B -> 0 if identical bytes, 1 otherwise (missing file = different)
cmpfiles() { "$PY" - "$1" "$2" <<'PY'
import sys
def read(p):
    try:
        with open(p, 'rb') as f: return f.read()
    except FileNotFoundError:
        return None
a, b = read(sys.argv[1]), read(sys.argv[2])
sys.exit(0 if (a is not None and b is not None and a == b) else 1)
PY
}

# emptyfile F -> 0 if file is zero bytes (or missing), 1 otherwise
emptyfile() { "$PY" - "$1" <<'PY'
import sys, os
p = sys.argv[1]
n = os.path.getsize(p) if os.path.exists(p) else 0
sys.exit(0 if n == 0 else 1)
PY
}

# 2048 stdin game: board is time-seeded, so compare fixed control-flow markers.
# Usage: markers2048 <stdout-file> <stderr-file>
markers2048() {
  "$PY" - "$1" "$2" <<'PY'
import sys
out = open(sys.argv[1], 'rb').read()
err = open(sys.argv[2], 'rb').read()
checks = [
    (b'QUIT?', True),                 # quit confirmation prompt was reached
    (b'\xe2\x86\x90,\xe2\x86\x91,\xe2\x86\x92,\xe2\x86\x93 or q', True),  # legend
    (b'Cannot read keyboard input', False),  # clean quit, not an EOF break
    (b'\x1b[?25h\x1b[m', True),      # terminal restore sequence
]
for needle, want in checks:
    got = needle in out
    if got != want:
        sys.exit(1)
sys.exit(0 if len(err) == 0 else 1)
PY
}

# cell evidence writer
write_cell() {
  # $1=mode letter  $2=project id  $3=std  $4=status  $5=source  $6=command
  # $7=compile exit $8=build stdout file $9=build stderr file ${10}=run detail file
  local mode=$1 proj=$2 std=$3 status=$4 src=$5 cmd=$6 cexit=$7
  local bstdout=$8 bstderr=$9 rdetail=${10}
  local ev="$CELLS/${mode}__${proj}__${std}.evidence.txt"
  {
    printf 'CELL: %s | %s | std=%s\n' "$mode" "$proj" "$std"
    printf 'MODE: %s\n' "$([ "$mode" = A ] && echo 'default (exact prescribed command)' || echo 'supplementary --legacy-c')"
    printf 'SOURCE: %s\n' "$src"
    printf 'SOURCE_SHA256: %s\n' "$(shasum -a 256 "$src" | awk '{print $1}')"
    printf 'COMMAND: %s\n' "$cmd"
    printf 'COMPILE_EXIT: %s\n' "$cexit"
    printf 'COMPILE_STDOUT_BYTES: %s\n' "$(wc -c < "$bstdout" | tr -d ' ')"
    printf 'COMPILE_STDOUT_BEGIN\n'
    cat -- "$bstdout"
    printf 'COMPILE_STDOUT_END\n'
    printf 'COMPILE_STDERR_BYTES: %s\n' "$(wc -c < "$bstderr" | tr -d ' ')"
    printf 'COMPILE_STDERR_BEGIN\n'
    cat -- "$bstderr"
    printf 'COMPILE_STDERR_END\n'
    printf 'RUN_DETAIL_BEGIN\n'
    if [ -f "$rdetail" ]; then cat -- "$rdetail"; else printf '(compile failed; no run attempted)\n'; fi
    printf 'RUN_DETAIL_END\n'
    printf 'STATUS: %s\n' "$status"
  } > "$ev"
  printf '%s\t%s\t%s\n' "$mode" "$proj" "$std" > "$LOG/.last_cell"
  printf '%s' "$status" > "$LOG/.last_status"
}

# Run the deterministic golden comparisons for a compiled binary.
# $1=project id  $2=binary  $3=std  $4=mode letter  $5=source  $6=command $7=cexit $8=bstdout $9=bstderr
run_cases() {
  local proj=$1 bin=$2 std=$3 mode=$4 src=$5 cmd=$6 cexit=$7 bstdout=$8 bstderr=$9
  local detail="$RUNS/${mode}__${proj}__${std}.detail.txt"
  : > "$detail"
  local allmatch=1
  local rc so se mo me mc

  # generic case runner: name, expected stdout file ("EMPTY" for empty),
  # expected stderr file ("EMPTY" for empty), expected exit, then argv...
  gencase() {
    local name=$1 gso=$2 gse=$3 gexit=$4; shift 4
    "$TIMEOUT" 30 "$@" >"$RUNS/${mode}__${proj}__${std}__${name}.out" \
                       2>"$RUNS/${mode}__${proj}__${std}__${name}.err"
    rc=$?
    so="$RUNS/${mode}__${proj}__${std}__${name}.out"
    se="$RUNS/${mode}__${proj}__${std}__${name}.err"
    mo=NO; me=NO; mc=NO
    if [ "$gso" = EMPTY ]; then emptyfile "$so" && mo=YES; else cmpfiles "$so" "$gso" && mo=YES; fi
    if [ "$gse" = EMPTY ]; then emptyfile "$se" && me=YES; else cmpfiles "$se" "$gse" && me=YES; fi
    [ "$rc" -eq "$gexit" ] && mc=YES
    printf 'CASE %s: exit=%s expected_exit=%s exit_match=%s stdout_match=%s stderr_match=%s stdout_bytes=%s stderr_bytes=%s\n' \
      "$name" "$rc" "$gexit" "$mc" "$mo" "$me" \
      "$(wc -c < "$so" | tr -d ' ')" "$(wc -c < "$se" | tr -d ' ')" >> "$detail"
    [ "$mo" = YES ] && [ "$me" = YES ] && [ "$mc" = YES ] || allmatch=0
  }

  # stdin-fed case runner (stdin from a fixed file)
  stdincase() {
    local name=$1 gso=$2 gse=$3 gexit=$4 infile=$5; shift 5
    "$TIMEOUT" 30 "$@" <"$infile" >"$RUNS/${mode}__${proj}__${std}__${name}.out" \
                                     2>"$RUNS/${mode}__${proj}__${std}__${name}.err"
    rc=$?
    so="$RUNS/${mode}__${proj}__${std}__${name}.out"
    se="$RUNS/${mode}__${proj}__${std}__${name}.err"
    mo=NO; me=NO; mc=NO
    if [ "$gso" = EMPTY ]; then emptyfile "$so" && mo=YES; else cmpfiles "$so" "$gso" && mo=YES; fi
    if [ "$gse" = EMPTY ]; then emptyfile "$se" && me=YES; else cmpfiles "$se" "$gse" && me=YES; fi
    [ "$rc" -eq "$gexit" ] && mc=YES
    printf 'CASE %s: exit=%s expected_exit=%s exit_match=%s stdout_match=%s stderr_match=%s stdout_bytes=%s stderr_bytes=%s\n' \
      "$name" "$rc" "$gexit" "$mc" "$mo" "$me" \
      "$(wc -c < "$so" | tr -d ' ')" "$(wc -c < "$se" | tr -d ' ')" >> "$detail"
    [ "$mo" = YES ] && [ "$me" = YES ] && [ "$mc" = YES ] || allmatch=0
  }

  case "$proj" in
    game_a)
      # printf 'q' | timeout 30 <bin>  -> quit on first loop iteration
      printf 'q' | "$TIMEOUT" 30 "$bin" >"$RUNS/${mode}__${proj}__${std}__snake_quit.out" \
                                        2>"$RUNS/${mode}__${proj}__${std}__snake_quit.err"
      rc=$?
      so="$RUNS/${mode}__${proj}__${std}__snake_quit.out"
      se="$RUNS/${mode}__${proj}__${std}__snake_quit.err"
      mo=NO; cmpfiles "$so" "$GOLD_A_STDOUT" && mo=YES
      me=NO; emptyfile "$se" && me=YES
      mc=NO; [ "$rc" -eq 0 ] && mc=YES
      printf 'CASE snake_quit: exit=%s expected_exit=0 exit_match=%s stdout_match=%s stderr_match=%s stdout_bytes=%s stderr_bytes=%s\n' \
        "$rc" "$mc" "$mo" "$me" "$(wc -c < "$so" | tr -d ' ')" "$(wc -c < "$se" | tr -d ' ')" >> "$detail"
      [ "$mo" = YES ] && [ "$me" = YES ] && [ "$mc" = YES ] || allmatch=0
      ;;
    game_b)
      gencase test    "$GOLD_B/run_test_stdout.log"    "$GOLD_B/run_test_stderr.log"    0 "$bin" test
      gencase version "$GOLD_B/run_version_stdout.log" "$GOLD_B/run_version_stderr.log" 0 "$bin" --version
      gencase help    "$GOLD_B/run_help_stdout.log"    "$GOLD_B/run_help_stderr.log"    0 "$bin" --help
      # randomly time-seeded board: marker-based comparison (see markers2048)
      printf 'qy' | "$TIMEOUT" 30 "$bin" >"$RUNS/${mode}__${proj}__${std}__stdin.out" \
                                         2>"$RUNS/${mode}__${proj}__${std}__stdin.err"
      rc=$?
      so="$RUNS/${mode}__${proj}__${std}__stdin.out"
      se="$RUNS/${mode}__${proj}__${std}__stdin.err"
      mm=NO; markers2048 "$so" "$se" && mm=YES
      mc=NO; [ "$rc" -eq 0 ] && mc=YES
      printf 'CASE stdin: exit=%s expected_exit=0 exit_match=%s marker_match=%s stdout_bytes=%s stderr_bytes=%s (board is time-seeded; fixed control-flow markers used)\n' \
        "$rc" "$mc" "$mm" "$(wc -c < "$so" | tr -d ' ')" "$(wc -c < "$se" | tr -d ' ')" >> "$detail"
      [ "$mm" = YES ] && [ "$mc" = YES ] || allmatch=0
      ;;
    cli_app)
      gencase version      "$GOLD_X/version.stdout"      "$GOLD_X/version.stderr"      0 "$bin" -v
      gencase help         "$GOLD_X/help.stdout"         "$GOLD_X/help.stderr"         0 "$bin" -h
      gencase file_default "$GOLD_X/file_default.stdout" "$GOLD_X/file_default.stderr" 0 "$bin" "$FIXTURE"
      gencase file_width8  "$GOLD_X/file_width8.stdout"  "$GOLD_X/file_width8.stderr"  0 "$bin" -w 8 -g 4 "$FIXTURE"
      gencase file_skip_n  "$GOLD_X/file_skip_n.stdout"  "$GOLD_X/file_skip_n.stderr"  0 "$bin" -s 32 -n 16 "$FIXTURE"
      stdincase stdin_default "$GOLD_X/stdin_default.stdout" "$GOLD_X/stdin_default.stderr" 0 "$FIXTURE" "$bin"
      ;;
  esac

  if [ "$allmatch" -eq 1 ]; then status=RUN_MATCH; else status=RUN_DIFFERENT; fi
  write_cell "$mode" "$proj" "$std" "$status" "$src" "$cmd" "$cexit" "$bstdout" "$bstderr" "$detail"
}

# Build one cell. $1=mode letter $2=project id $3=source $4=std
build_cell() {
  local mode=$1 proj=$2 src=$3 std=$4
  local extra=""
  [ "$mode" = B ] && extra=" --legacy-c"
  local bin="$BINS/${mode}__${proj}__${std}"
  local cmd="$CPLUS build $src -std=$std --sanitize=none$extra -o $bin"
  local bstdout="$RUNS/${mode}__${proj}__${std}__build.out"
  local bstderr="$RUNS/${mode}__${proj}__${std}__build.err"
  rm -f -- "$bin"
  # shellcheck disable=SC2086
  ./build/c+ build "$src" -std="$std" --sanitize=none $extra -o "$bin" >"$bstdout" 2>"$bstderr"
  local rc=$?
  if [ "$rc" -ne 0 ] || [ ! -x "$bin" ]; then
    write_cell "$mode" "$proj" "$std" COMPILE_FAIL "$src" "$cmd" "$rc" "$bstdout" "$bstderr" ""
  else
    run_cases "$proj" "$bin" "$std" "$mode" "$src" "$cmd" "$rc" "$bstdout" "$bstderr"
  fi
}

# --- run all cells ------------------------------------------------------------
: > "$LOG/results.tsv"
printf 'mode\tproject\tstd\tstatus\n' >> "$LOG/results.tsv"
for mode in A B; do
  for proj in game_a game_b cli_app; do
    case "$proj" in
      game_a) src=$SRC_GAME_A;; game_b) src=$SRC_GAME_B;; cli_app) src=$SRC_CLI_APP;;
    esac
    for std in $ACCEPTED; do
      build_cell "$mode" "$proj" "$src" "$std"
      printf '%s\t%s\t%s\t%s\n' "$mode" "$proj" "$std" "$(cat "$LOG/.last_status")" >> "$LOG/results.tsv"
      printf '[%s] %-8s %-8s %s\n' "$mode" "$proj" "$std" "$(cat "$LOG/.last_status")"
    done
  done
done

echo "Done. Matrix data: $LOG/results.tsv ; evidence: $CELLS/"
