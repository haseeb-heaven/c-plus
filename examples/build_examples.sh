#!/bin/sh
# Build the complex example programs. Interactive programs are NOT run here.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
OUT="$ROOT/examples/out"
mkdir -p "$OUT"

build() {
  name=$1
  src=$2
  shift 2
  echo "== $name =="
  if "$CPLUS" build "$src" -o "$OUT/$name" "$@" >"$OUT/$name.build.log" 2>&1; then
    echo "  ok   $name"
  else
    echo "  FAIL $name (see examples/out/$name.build.log)"
    FAIL=$((FAIL + 1))
  fi
}

FAIL=0
build game_2048 "$ROOT/examples/games/2048.c" -- -lncurses
build snake "$ROOT/examples/games/snake.c"
build tetris "$ROOT/examples/games/tetris.c" -- -lncurses
build tiles "$ROOT/examples/games/tiles.c"
build cgame "$ROOT/examples/apps/cgame.c"
build students "$ROOT/examples/apps/student.cp"
build macpulse "$ROOT/examples/system/macpulse.cp"

echo
echo "examples build: $FAIL failed"
[ "$FAIL" -eq 0 ]
