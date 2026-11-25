#!/bin/sh
# Deterministic build-and-run harness for the four expanded CLI examples.
#
#   sh examples/expanded/cli/run.sh
#
# Every compiler and program invocation is bounded by `timeout 30`. Fixtures
# are generated here with fixed bytes; no clock, network, interactive prompt
# or user path is involved. A scenario passes only when the exit code matches,
# stderr is empty and stdout matches the expected bytes exactly.
#
# Final line: CLI_EXAMPLES <apps> <passed> <failed>
set -u

DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd) || exit 2
ROOT=$(CDPATH= cd -- "$DIR/../../.." && pwd) || exit 2
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$DIR/.run"
KILL_AFTER=30

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
export PATH

mkdir -p "$OUT" || exit 2
if ! command -v timeout >/dev/null 2>&1; then
  echo "error: 'timeout' utility not found; refusing unbounded tests" >&2
  exit 2
fi
if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler missing: $CPLUS" >&2
  exit 2
fi

cd "$ROOT" || exit 2

apps=4
passed=0
failed=0
app_status=1

build_app() {
  name=$1
  timeout -k 5 "$KILL_AFTER" "$CPLUS" build "$DIR/$name.cp" \
    --strict --sanitize=none -I. -o "$OUT/$name" \
    >"$OUT/$name.build.log" 2>&1
  brc=$?
  if [ "$brc" -ne 0 ]; then
    echo "BUILD-FAIL $name (rc=$brc)"
    tail -n 8 "$OUT/$name.build.log" | sed 's/^/           /'
    app_status=0
    return 1
  fi
  echo "build ok   $name"
  return 0
}

verify() {
  label=$1
  want_rc=$2
  want_file=$3
  got_rc=$4
  ok=1
  [ "$got_rc" -eq "$want_rc" ] || ok=0
  if [ -s "$OUT/$label.err" ]; then ok=0; fi
  if ! cmp -s "$OUT/$label.out" "$want_file"; then ok=0; fi
  if [ "$ok" -eq 1 ]; then
    echo "ok         $label"
  else
    echo "FAIL       $label (rc=$got_rc want=$want_rc)"
    diff -u "$want_file" "$OUT/$label.out" 2>/dev/null | head -n 20 \
      | sed 's/^/           /'
    head -n 5 "$OUT/$label.err" | sed 's/^/           stderr: /'
    app_status=0
  fi
}

check() {
  label=$1
  want_rc=$2
  want_file=$3
  shift 3
  rm -f "$OUT/$label.out" "$OUT/$label.err"
  timeout -k 5 "$KILL_AFTER" "$@" \
    >"$OUT/$label.out" 2>"$OUT/$label.err" </dev/null
  verify "$label" "$want_rc" "$want_file" "$?"
}

finish_app() {
  if [ "$app_status" -eq 1 ]; then
    passed=$((passed + 1))
  else
    failed=$((failed + 1))
  fi
  app_status=1
}

# ---- deterministic fixtures ----------------------------------------------
printf 'alpha beta gamma\ndelta alpha\nomega\n' >"$OUT/text.in"
printf 'hello file tools\n' >"$OUT/src.txt"
printf 'different\n' >"$OUT/other.txt"
head -c 1024 /dev/zero >"$OUT/exact.bin"
head -c 1025 /dev/zero >"$OUT/over.bin"

# ---- todo -----------------------------------------------------------------
echo "== todo =="
if build_app todo; then
  TODO_BIN="$OUT/todo"
  printf '%s\n' \
    "TODO add 1 buy_milk" \
    "TODO add 2 call_dad" \
    "TODO done 1" \
    "TODO item 1 buy_milk 1" \
    "TODO item 2 call_dad 0" \
    "TODO list 2" \
    "TODO save 2 $OUT/todo.db" \
    "TODO remove 2" \
    "TODO item 1 buy_milk 1" \
    "TODO list 1" \
    "TODO load 1 $OUT/todo.db" \
    "TODO item 1 buy_milk 1" \
    "TODO item 2 call_dad 0" \
    "TODO list 2" >"$OUT/exp_todo_flow"
  check todo_flow 0 "$OUT/exp_todo_flow" "$TODO_BIN" \
    add 1 buy_milk -- add 2 call_dad -- done 1 -- list \
    -- save "$OUT/todo.db" -- remove 2 -- list -- load "$OUT/todo.db"

  printf '%s\n' \
    "TODO add 1 x" \
    "TODO error duplicate" \
    "TODO error not-found" \
    "TODO error unknown-command" >"$OUT/exp_todo_invalid"
  check todo_invalid 1 "$OUT/exp_todo_invalid" "$TODO_BIN" \
    add 1 x -- add 1 y -- done 99 -- bogus

  printf '%s\n' "TODO error usage" >"$OUT/exp_todo_usage"
  check todo_usage 2 "$OUT/exp_todo_usage" "$TODO_BIN"
fi
finish_app

# ---- calculator -----------------------------------------------------------
echo "== calculator =="
if build_app calculator; then
  CALC_BIN="$OUT/calculator"

  printf '%s\n' "CALC 1+2*3 = 7" "CALC ops 2" >"$OUT/exp_calc_prec"
  check calc_prec 0 "$OUT/exp_calc_prec" "$CALC_BIN" "1+2*3"

  printf '%s\n' "CALC (1+2)*3 = 9" "CALC ops 2" >"$OUT/exp_calc_paren"
  check calc_paren 0 "$OUT/exp_calc_paren" "$CALC_BIN" "(1+2)*3"

  printf '%s\n' "CALC 7/(2+2) = 1" "CALC ops 2" >"$OUT/exp_calc_div"
  check calc_div 0 "$OUT/exp_calc_div" "$CALC_BIN" "7/(2+2)"

  printf '%s\n' "CALC 10/3 = 3" "CALC ops 1" >"$OUT/exp_calc_trunc"
  check calc_trunc 0 "$OUT/exp_calc_trunc" "$CALC_BIN" "10/3"

  printf '%s\n' "CALC ERROR divide-by-zero" >"$OUT/exp_calc_zero"
  check calc_zero 1 "$OUT/exp_calc_zero" "$CALC_BIN" "1/0"

  printf '%s\n' "CALC ERROR syntax" >"$OUT/exp_calc_syntax"
  check calc_syntax 1 "$OUT/exp_calc_syntax" "$CALC_BIN" "1+"

  printf '%s\n' "CALC 4*(3+1) = 16" "CALC ops 2" >"$OUT/exp_calc_stdin"
  rm -f "$OUT/calc_stdin.out" "$OUT/calc_stdin.err"
  printf '4*(3+1)\n' | timeout -k 5 "$KILL_AFTER" "$CALC_BIN" \
    >"$OUT/calc_stdin.out" 2>"$OUT/calc_stdin.err"
  verify calc_stdin 0 "$OUT/exp_calc_stdin" "$?"
fi
finish_app

# ---- text_tools -----------------------------------------------------------
echo "== text_tools =="
if build_app text_tools; then
  TEXT_BIN="$OUT/text_tools"
  IN="$OUT/text.in"

  printf '%s\n' "TEXT words 6" >"$OUT/exp_text_words"
  check text_words 0 "$OUT/exp_text_words" "$TEXT_BIN" "$IN" words

  printf '%s\n' "TEXT lines 3 3" >"$OUT/exp_text_lines"
  check text_lines 0 "$OUT/exp_text_lines" "$TEXT_BIN" "$IN" lines

  printf '%s\n' \
    "TEXT rev 2 omega" \
    "TEXT rev 1 delta alpha" \
    "TEXT rev 0 alpha beta gamma" >"$OUT/exp_text_rev"
  check text_rev 0 "$OUT/exp_text_rev" "$TEXT_BIN" "$IN" rev

  printf '%s\n' \
    "TEXT find 0 0 alpha beta gamma" \
    "TEXT find 1 6 delta alpha" \
    "TEXT find hits 2" >"$OUT/exp_text_find"
  check text_find 0 "$OUT/exp_text_find" "$TEXT_BIN" "$IN" find alpha

  printf '%s\n' "TEXT find hits 0" >"$OUT/exp_text_nofind"
  check text_nofind 0 "$OUT/exp_text_nofind" "$TEXT_BIN" "$IN" find zzz

  printf '%s\n' "TEXT error cannot-open" >"$OUT/exp_text_missing"
  check text_missing 1 "$OUT/exp_text_missing" "$TEXT_BIN" \
    "$OUT/does-not-exist" words
fi
finish_app

# ---- file_tools -----------------------------------------------------------
echo "== file_tools =="
if build_app file_tools; then
  FT_BIN="$OUT/file_tools"

  printf '%s\n' "FILETOOLS size 17" >"$OUT/exp_ft_size"
  check ft_size 0 "$OUT/exp_ft_size" "$FT_BIN" size "$OUT/src.txt"

  printf '%s\n' "FILETOOLS size 1024" >"$OUT/exp_ft_exact"
  check ft_exact 0 "$OUT/exp_ft_exact" "$FT_BIN" size "$OUT/exact.bin"

  printf '%s\n' "FILETOOLS error one-over" >"$OUT/exp_ft_over"
  check ft_over 1 "$OUT/exp_ft_over" "$FT_BIN" size "$OUT/over.bin"

  printf '%s\n' "FILETOOLS copy 17" >"$OUT/exp_ft_copy"
  check ft_copy 0 "$OUT/exp_ft_copy" "$FT_BIN" \
    copy "$OUT/src.txt" "$OUT/dst.txt"

  printf '%s\n' "FILETOOLS cmp 1" >"$OUT/exp_ft_cmp_same"
  check ft_cmp_same 0 "$OUT/exp_ft_cmp_same" "$FT_BIN" \
    cmp "$OUT/src.txt" "$OUT/dst.txt"

  printf '%s\n' "FILETOOLS cmp 0" >"$OUT/exp_ft_cmp_diff"
  check ft_cmp_diff 0 "$OUT/exp_ft_cmp_diff" "$FT_BIN" \
    cmp "$OUT/src.txt" "$OUT/other.txt"
fi
finish_app

printf 'CLI_EXAMPLES %d %d %d\n' "$apps" "$passed" "$failed"
[ "$failed" -eq 0 ]
