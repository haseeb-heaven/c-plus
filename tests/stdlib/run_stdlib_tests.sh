#!/bin/sh
# C+ standard-library test suite.
#
# Usage: tests/stdlib/run_stdlib_tests.sh [pattern]
#
# Layout:
#   tests/stdlib/<name>.cp            positive/negative program
#   tests/stdlib/<name>.stdin         optional standard input
#   tests/stdlib/<name>.expected      optional expected stdout
#   tests/stdlib/multi_tu/            one program built from three TUs
#
# External libraries are compiled once here (third_party/yyjson) and linked,
# never re-lowered per test.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/build/stdlib-tests"
EXT="$ROOT/build/ext"
PATTERN=${1:-}
PASS=0
FAIL=0
FAILED=""

mkdir -p "$OUT" "$EXT"
cd "$ROOT" || exit 1

if [ ! -x "$CPLUS" ]; then
  echo "error: C+ compiler not found at $CPLUS (build with cmake --build build)" >&2
  exit 1
fi

CXX=${CPLUS_CLANG:-clang}

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

# --- external libraries (compiled once) ------------------------------------

YYJSON_LIB="$EXT/libyyjson.a"
if [ ! -f "$YYJSON_LIB" ] || [ "$ROOT/third_party/yyjson/yyjson.c" -nt "$YYJSON_LIB" ]; then
  if ! "$CXX" -c "$ROOT/third_party/yyjson/yyjson.c" -o "$EXT/yyjson.o" \
       -O2 -I"$ROOT/third_party/yyjson" >"$OUT/yyjson.build.log" 2>&1; then
    echo "error: cannot build third_party/yyjson/yyjson.c (see $OUT/yyjson.build.log)" >&2
    exit 1
  fi
  ar rcs "$YYJSON_LIB" "$EXT/yyjson.o"
  printf 'built external library: %s\n' "$YYJSON_LIB"
fi

# --- single-translation-unit tests -----------------------------------------

for src in "$ROOT"/tests/stdlib/*.cp; do
  [ -e "$src" ] || continue
  name=$(basename "$src"); name=${name%.cp}
  matches "$name" || continue
  case "$name" in bench_*) continue ;; esac
  exe="$OUT/$name"

  libs=""
  if grep -q 'stdlib/json.hp' "$src"; then
    libs="-I$ROOT/third_party/yyjson -L$EXT -lyyjson"
  fi

  if ! "$CPLUS" build "$src" $libs -o "$exe" >"$OUT/$name.build.log" 2>&1; then
    fail "$name" "compilation failed; see build/stdlib-tests/$name.build.log"
    continue
  fi

  stdin_file="$ROOT/tests/stdlib/$name.stdin"
  if [ -f "$stdin_file" ]; then
    "$exe" <"$stdin_file" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  else
    "$exe" </dev/null >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  fi
  rc=$?
  if [ "$rc" -ne 0 ]; then
    fail "$name" "exited with $rc; stderr: $(head -2 "$OUT/$name.stderr" | tr '\n' ' ')"
    continue
  fi
  if [ -f "$ROOT/tests/stdlib/$name.expected" ]; then
    if ! diff -u "$ROOT/tests/stdlib/$name.expected" "$OUT/$name.stdout" \
        >"$OUT/$name.diff" 2>&1; then
      fail "$name" "stdout mismatch; see build/stdlib-tests/$name.diff"
      continue
    fi
  fi
  pass "$name"
done

# --- multiple translation units --------------------------------------------

if matches "multi_tu"; then
  if "$CPLUS" build "$ROOT/tests/stdlib/multi_tu/main.cp" \
      "$ROOT/tests/stdlib/multi_tu/part_a.cp" \
      "$ROOT/tests/stdlib/multi_tu/part_b.cp" \
      -o "$OUT/multi_tu" >"$OUT/multi_tu.build.log" 2>&1 &&
     "$OUT/multi_tu" >"$OUT/multi_tu.stdout" 2>"$OUT/multi_tu.stderr"; then
    if [ -f "$ROOT/tests/stdlib/multi_tu.expected" ] &&
       ! diff -u "$ROOT/tests/stdlib/multi_tu.expected" "$OUT/multi_tu.stdout" \
         >"$OUT/multi_tu.diff" 2>&1; then
      fail "multi_tu" "stdout mismatch; see build/stdlib-tests/multi_tu.diff"
    else
      pass "multi_tu"
    fi
  else
    fail "multi_tu" "build or run failed; see build/stdlib-tests/multi_tu.build.log"
  fi
fi

# --- documented known gap: class-bearing .hp in two TUs --------------------

if matches "class_linkage_gap"; then
  if "$CPLUS" build "$ROOT/tests/stdlib/known_gaps/class_linkage/main.cp" \
      "$ROOT/tests/stdlib/known_gaps/class_linkage/part_a.cp" \
      "$ROOT/tests/stdlib/known_gaps/class_linkage/part_b.cp" \
      -o "$OUT/class_linkage_gap" \
      >"$OUT/class_linkage_gap.log" 2>&1; then
    fail "class_linkage_gap" "expected a duplicate-symbol link failure (gap fixed? update the test and docs/MODERN_STDLIB.md)"
  else
    if grep -q "duplicate symbol" "$OUT/class_linkage_gap.log"; then
      pass "class_linkage_gap (expected failure: duplicate class symbols)"
    else
      fail "class_linkage_gap" "failed for an unexpected reason; see build/stdlib-tests/class_linkage_gap.log"
    fi
  fi
fi

echo
echo "C+ stdlib tests: $PASS passed, $FAIL failed"
if [ "$FAIL" -ne 0 ]; then
  printf '%s\n' "$FAILED"
  exit 1
fi
exit 0
