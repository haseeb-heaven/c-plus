#!/bin/sh
# C+ automated test suite.
#
# Usage: tests/run_tests.sh [pattern]
#
# Layout:
#   tests/cases/<name>.c          positive: build, run, compare stdout
#   tests/cases/<name>.expected   expected stdout
#   tests/compile_fail/<name>.c   must be rejected by the C+ front end
#   tests/compile_fail/<name>.err expected diagnostic substring
#   tests/runtime_fail/<name>.c   must fail safely at run time
#   tests/runtime_fail/<name>.exit    expected exit code (default 134)
#   tests/runtime_fail/<name>.stderr  expected stderr substring

set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/tests/out"
PATTERN=${1:-}
PASS=0
FAIL=0
FAILED=""

mkdir -p "$OUT"

if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler not found at $CPLUS (build with cmake --build build)" >&2
  exit 1
fi

matches() {
  [ -z "$PATTERN" ] && return 0
  case "$1" in
    *"$PATTERN"*) return 0 ;;
  esac
  return 1
}

pass() {
  PASS=$((PASS + 1))
  printf '  ok   %s\n' "$1"
}

fail() {
  FAIL=$((FAIL + 1))
  FAILED="$FAILED
  - $1: $2"
  printf '  FAIL %s (%s)\n' "$1" "$2"
}

# --- positive tests --------------------------------------------------------

for src in "$ROOT"/tests/cases/*.c "$ROOT"/tests/cases/*.cp; do
  [ -e "$src" ] || continue
  name=$(basename "$src"); name=${name%.c}; name=${name%.cp}
  matches "$name" || continue
  exe="$OUT/$name"
  if ! "$CPLUS" build "$src" -o "$exe" >"$OUT/$name.build.log" 2>&1; then
    fail "$name" "compilation failed; see tests/out/$name.build.log"
    continue
  fi
  stdin_file="$ROOT/tests/cases/$name.stdin"
  if [ -f "$stdin_file" ]; then
    "$exe" <"$stdin_file" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  else
    "$exe" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  fi
  rc=$?
  if [ "$rc" -ne 0 ]; then
    fail "$name" "exited with $rc; stderr: $(head -2 "$OUT/$name.stderr" | tr '\n' ' ')"
    continue
  fi
  if [ -f "$ROOT/tests/cases/$name.expected" ]; then
    if ! diff -u "$ROOT/tests/cases/$name.expected" "$OUT/$name.stdout" \
        >"$OUT/$name.diff" 2>&1; then
      fail "$name" "stdout mismatch; see tests/out/$name.diff"
      continue
    fi
  fi
  pass "$name"
done

# --- compile-time negative tests ------------------------------------------

for src in "$ROOT"/tests/compile_fail/*.c "$ROOT"/tests/compile_fail/*.cp; do
  [ -e "$src" ] || continue
  name=$(basename "$src"); name=${name%.c}; name=${name%.cp}
  matches "$name" || continue
  if "$CPLUS" check "$src" >"$OUT/$name.log" 2>&1; then
    fail "$name" "expected compilation to fail, but it succeeded"
    continue
  fi
  if [ -f "$ROOT/tests/compile_fail/$name.err" ]; then
    want=$(cat "$ROOT/tests/compile_fail/$name.err")
    if ! grep -q "$want" "$OUT/$name.log"; then
      fail "$name" "diagnostic does not contain '$want'; see tests/out/$name.log"
      continue
    fi
  fi
  pass "$name"
done

# --- run-time negative tests ----------------------------------------------

for src in "$ROOT"/tests/runtime_fail/*.c "$ROOT"/tests/runtime_fail/*.cp; do
  [ -e "$src" ] || continue
  name=$(basename "$src"); name=${name%.c}; name=${name%.cp}
  matches "$name" || continue
  exe="$OUT/$name"
  if ! "$CPLUS" build "$src" -o "$exe" >"$OUT/$name.build.log" 2>&1; then
    fail "$name" "compilation failed; see tests/out/$name.build.log"
    continue
  fi
  # These programs abort by design; keep the shell's job message quiet.
  rm -f "$OUT/$name.exit"
  { "$exe" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr";
    echo $? >"$OUT/$name.exit"; } 2>/dev/null
  rc=$(cat "$OUT/$name.exit")
  want=134
  if [ -f "$ROOT/tests/runtime_fail/$name.exit" ]; then
    want=$(cat "$ROOT/tests/runtime_fail/$name.exit")
  fi
  if [ "$rc" -ne "$want" ]; then
    fail "$name" "expected exit $want, got $rc"
    continue
  fi
  if [ -f "$ROOT/tests/runtime_fail/$name.stderr" ]; then
    wantmsg=$(cat "$ROOT/tests/runtime_fail/$name.stderr")
    if ! grep -q "$wantmsg" "$OUT/$name.stderr"; then
      fail "$name" "stderr does not contain '$wantmsg'; see tests/out/$name.stderr"
      continue
    fi
  fi
  pass "$name"
done

# --- legacy C mode ---------------------------------------------------------

if [ -f "$ROOT/tests/legacy/plain.c" ] && matches "legacy"; then
  exe="$OUT/legacy_plain"
  if "$CPLUS" build "$ROOT/tests/legacy/plain.c" --legacy-c -o "$exe" \
      >"$OUT/legacy_plain.log" 2>&1 &&
     [ "$("$exe")" = "legacy ok" ]; then
    pass "legacy_plain"
  else
    fail "legacy_plain" "legacy C mode failed; see tests/out/legacy_plain.log"
  fi
fi

echo
echo "C+ tests: $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
  printf '%s\n' "$FAILED"
  exit 1
fi
exit 0
