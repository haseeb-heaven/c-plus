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
# External libraries are compiled once here (third_party/yyjson, stb,
# miniaudio) or linked from the system (SDL3, libcurl, libuv, FFmpeg), never
# re-lowered per test.
set -u

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-"$ROOT/build/c+"}
OUT="$ROOT/build/stdlib-tests"
EXT="$ROOT/build/ext"
PATTERN=${1:-}
PASS=0
FAIL=0
SKIPPED=0
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

skip() {
  SKIPPED=$((SKIPPED + 1))
  printf '  skip %s (%s)\n' "$1" "$2"
}

fail() {
  FAIL=$((FAIL + 1))
  FAILED="$FAILED
  - $1: $2"
  printf '  FAIL %s (%s)\n' "$1" "$2"
}

# --- external libraries (compiled once each) --------------------------------

build_ext() { # name source include-dir
  _lib="$EXT/lib$1.a"
  _obj="$EXT/$1.o"
  if [ -f "$_lib" ] && [ ! "$2" -nt "$_lib" ]; then
    return 0
  fi
  if ! "$CXX" -c "$2" -o "$_obj" -O2 -I"$3" >"$OUT/$1.build.log" 2>&1; then
    return 1
  fi
  ar rcs "$_lib" "$_obj"
  printf 'built external library: %s\n' "$_lib"
  return 0
}

build_ext yyjson "$ROOT/third_party/yyjson/yyjson.c" "$ROOT/third_party/yyjson" || {
  echo "error: cannot build third_party/yyjson (see $OUT/yyjson.build.log)" >&2; exit 1; }
build_ext stb "$ROOT/third_party/stb/stb_impl.c" "$ROOT/third_party/stb" || {
  echo "error: cannot build third_party/stb (see $OUT/stb.build.log)" >&2; exit 1; }
build_ext miniaudio "$ROOT/third_party/miniaudio/miniaudio_impl.c" "$ROOT/third_party/miniaudio" || {
  echo "error: cannot build third_party/miniaudio (see $OUT/miniaudio.build.log)" >&2; exit 1; }
# Nuklear needs the SDL3 headers and links against SDL3.
if [ ! -f "$EXT/libnuklear.a" ] || [ "$ROOT/third_party/nuklear/nuklear_impl.c" -nt "$EXT/libnuklear.a" ]; then
  if "$CXX" -c "$ROOT/third_party/nuklear/nuklear_impl.c" -o "$EXT/nuklear.o" -O2 \
      -I"$ROOT/third_party/nuklear" $(pkg-config --cflags sdl3) >"$OUT/nuklear.build.log" 2>&1; then
    ar rcs "$EXT/libnuklear.a" "$EXT/nuklear.o"
    printf 'built external library: %s\n' "$EXT/libnuklear.a"
  else
    echo "error: cannot build third_party/nuklear (see $OUT/nuklear.build.log)" >&2; exit 1
  fi
fi

pkg_libs() {
  pkg-config --cflags --libs "$@" 2>/dev/null || true
}

# --- generated fixtures (never vendored, never committed) -------------------

CLIP="$OUT/clip.mp4"
if [ ! -f "$CLIP" ] && command -v ffmpeg >/dev/null 2>&1; then
  if ffmpeg -loglevel error -y -f lavfi \
      -i testsrc=size=64x48:rate=10:duration=1 -pix_fmt yuv420p \
      "$CLIP" >"$OUT/clip.build.log" 2>&1; then
    printf 'generated fixture: %s\n' "$CLIP"
  else
    rm -f "$CLIP"
  fi
fi

# Libraries a test needs, decided by the C+ headers it includes.
libs_for() {
  _src="$1"
  _out=""
  if grep -q 'stdlib/json.hp' "$_src"; then
    _out="$_out -I$ROOT/third_party/yyjson -L$EXT -lyyjson"
  fi
  if grep -q 'stdlib/image.hp' "$_src"; then
    _out="$_out -I$ROOT/third_party/stb -L$EXT -lstb"
  fi
  if grep -q 'stdlib/audio.hp' "$_src"; then
    # -Wl, form: the driver's CLI would treat a bare "-framework X" operand as
    # an input file, so pass the pair through to the linker as one argument.
    _out="$_out -I$ROOT/third_party/miniaudio -L$EXT -lminiaudio \
      -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio \
      -Wl,-framework,AudioToolbox"
  fi
  if grep -qE 'stdlib/(window|graphics|input|gui)\.hp' "$_src"; then
    _out="$_out $(pkg_libs sdl3)"
  fi
  if grep -q 'stdlib/gui.hp' "$_src"; then
    _out="$_out -I$ROOT/third_party/nuklear -L$EXT -lnuklear"
  fi
  if grep -q 'stdlib/http.hp' "$_src"; then
    _out="$_out $(pkg_libs libcurl)"
  fi
  if grep -q 'stdlib/async.hp' "$_src"; then
    _out="$_out $(pkg_libs libuv)"
  fi
  if grep -q 'stdlib/video.hp' "$_src"; then
    _out="$_out $(pkg_libs libavformat libavcodec libavutil)"
  fi
  echo "$_out"
}

# --- single-translation-unit tests -----------------------------------------

for src in "$ROOT"/tests/stdlib/*.cp; do
  [ -e "$src" ] || continue
  name=$(basename "$src"); name=${name%.cp}
  matches "$name" || continue
  case "$name" in bench_*) continue ;; esac
  exe="$OUT/$name"

  # A test whose dependency is unavailable is skipped, never silently passed.
  case "$name" in
    video_test)
      [ -f "$CLIP" ] || { skip "$name" "ffmpeg CLI unavailable for the fixture"; continue; } ;;
    sdl_test|gui_test)
      pkg-config --exists sdl3 || { skip "$name" "SDL3 development files missing"; continue; } ;;
    http_test|http_issue63_test)
      pkg-config --exists libcurl || { skip "$name" "libcurl development files missing"; continue; } ;;
    async_test)
      pkg-config --exists libuv || { skip "$name" "libuv development files missing"; continue; } ;;
  esac

  libs=$(libs_for "$src")
  if ! "$CPLUS" build "$src" $libs -o "$exe" >"$OUT/$name.build.log" 2>&1; then
    fail "$name" "compilation failed; see build/stdlib-tests/$name.build.log"
    continue
  fi

  stdin_file="$ROOT/tests/stdlib/$name.stdin"
  if [ -f "$stdin_file" ]; then
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$exe" <"$stdin_file" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  else
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
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

# --- examples (build and run; every demo must exit 0) ----------------------

EXOUT="$OUT/examples"
mkdir -p "$EXOUT"
for src in "$ROOT"/tests/stdlib/examples/*.cp; do
  [ -e "$src" ] || continue
  base=$(basename "$src"); base=${base%.cp}
  name="demo_$base"
  matches "$name" || continue
  case "$base" in
    video_demo)
      [ -f "$CLIP" ] || { skip "$name" "ffmpeg CLI unavailable for the fixture"; continue; } ;;
    sdl_demo|gui_demo)
      pkg-config --exists sdl3 || { skip "$name" "SDL3 development files missing"; continue; } ;;
    http_demo)
      pkg-config --exists libcurl || { skip "$name" "libcurl development files missing"; continue; } ;;
    async_demo)
      pkg-config --exists libuv || { skip "$name" "libuv development files missing"; continue; } ;;
  esac
  exe="$EXOUT/$base"
  libs=$(libs_for "$src")
  if ! "$CPLUS" build "$src" $libs -o "$exe" >"$OUT/$name.build.log" 2>&1; then
    fail "$name" "compilation failed; see build/stdlib-tests/$name.build.log"
    continue
  fi
  stdin_file="$ROOT/tests/stdlib/examples/$base.stdin"
  if [ -f "$stdin_file" ]; then
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$exe" <"$stdin_file" >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  else
    SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy \
      "$exe" </dev/null >"$OUT/$name.stdout" 2>"$OUT/$name.stderr"
  fi
  rc=$?
  if [ "$rc" -ne 0 ]; then
    fail "$name" "exited with $rc; stderr: $(head -2 "$OUT/$name.stderr" | tr '\n' ' ')"
    continue
  fi
  pass "$name"
done

# --- per-library API coverage (>= 80% each) --------------------------------

if matches "coverage"; then
  if sh "$ROOT/tests/stdlib/run_coverage.sh" >"$OUT/coverage.txt" 2>&1; then
    pass "coverage (API surface reachability; execution coverage: coverage_exec.sh)"
    sed 's/^/       /' "$OUT/coverage.txt"
  else
    fail "coverage" "below threshold; see build/stdlib-tests/coverage.txt"
    sed 's/^/       /' "$OUT/coverage.txt"
  fi
fi

# --- multi-TU tests (function-bearing and class-bearing .hp across TUs) ------

if matches "multi_tu"; then
  if "$CPLUS" build "$ROOT/tests/stdlib/multi_tu/main.cp" \
      "$ROOT/tests/stdlib/multi_tu/part_a.cp" \
      "$ROOT/tests/stdlib/multi_tu/part_b.cp" \
      -o "$OUT/multi_tu" >"$OUT/multi_tu.build.log" 2>&1 && \
      printf '7\none\ntwo\nthree\n' | "$OUT/multi_tu" >"$OUT/multi_tu.out" 2>&1; then
    pass "multi_tu"
  else
    fail "multi_tu" "build or run failed; see build/stdlib-tests/multi_tu.build.log"
  fi
fi

if matches "class_linkage"; then
  if "$CPLUS" build "$ROOT/tests/stdlib/known_gaps/class_linkage/main.cp" \
      "$ROOT/tests/stdlib/known_gaps/class_linkage/part_a.cp" \
      "$ROOT/tests/stdlib/known_gaps/class_linkage/part_b.cp" \
      -o "$OUT/class_linkage" >"$OUT/class_linkage.log" 2>&1 && \
      "$OUT/class_linkage" >"$OUT/class_linkage.out" 2>&1; then
    pass "class_linkage"
  else
    fail "class_linkage" "build or run failed; see build/stdlib-tests/class_linkage.log"
  fi
fi

echo
echo "C+ stdlib tests: $PASS passed, $FAIL failed, $SKIPPED skipped"
if [ "$FAIL" -ne 0 ]; then
  printf '%s\n' "$FAILED"
  exit 1
fi
exit 0
