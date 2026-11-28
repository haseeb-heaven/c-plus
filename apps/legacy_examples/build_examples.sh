#!/bin/sh
# Build the complex example programs. Interactive programs are NOT run here.
set -u
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
CPLUS=${CPLUS_BIN:-$ROOT/build/c+}
OUT="$ROOT/apps/legacy_examples/out"
mkdir -p "$OUT"

build() {
  name=$1
  src=$2
  shift 2
  echo "== $name =="
  if "$CPLUS" build "$src" -o "$OUT/$name" "$@" >"$OUT/$name.build.log" 2>&1; then
    echo "  ok   $name"
  else
    echo "  FAIL $name (see apps/legacy_examples/out/$name.build.log)"
    FAIL=$((FAIL + 1))
  fi
}

FAIL=0
build game_2048 "$ROOT/apps/legacy_examples/games/2048.c" -- -lncurses
build snake "$ROOT/apps/legacy_examples/games/snake.c"
build tetris "$ROOT/apps/legacy_examples/games/tetris.c" -- -lncurses
build tiles "$ROOT/apps/legacy_examples/games/tiles.c"
build cgame "$ROOT/apps/legacy_examples/apps/cgame.c"
build students "$ROOT/apps/legacy_examples/apps/student.cp"
build macpulse "$ROOT/apps/legacy_examples/system/macpulse.cp"

echo
echo "examples build: $FAIL failed"
[ "$FAIL" -eq 0 ]
