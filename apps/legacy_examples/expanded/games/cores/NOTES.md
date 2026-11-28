# Deterministic game cores

## What these are (and are not)

These three `.cp` programs are **deterministic logic models, not interactive
games**. There is no terminal graphics/curses UI, no keyboard/event loop, no
wall clock, no `time()`/`usleep()`, and no network. Every run is a pure
function of its fixed inputs:

- `game2048.cp` — 4x4 board, fixed xorshift64* seed `0x9E3779B97F4A7C15`,
  slide/merge/collision scoring, one spawned tile per effective move.
- `maze.cp` — hard-coded 9x7 layout, wall collision, start/goal detection.
- `tictactoe.cp` — 3x3 board, two players with fixed move plans, win/draw
  detection.

Each uses the C+ standard library where it fits: `stdlib/vector.hp` holds the
parsed move sequence, `stdlib/string.hp` parses argv/stdin tokens, and
`stdlib/error.hp` supplies the `error` value used to report invalid input.
All three build in `--strict --sanitize=none` mode and print byte-identical
stdout for identical input.

Input contract (all three):

| invocation | meaning |
| --- | --- |
| `bin U D L R` | moves as separate argv tokens |
| `bin UDLR` | one compact argv token |
| `printf 'UDLR\n' \| bin -` | one line from stdin |
| `bin` | built-in fixed default sequence |

Exit status: **0** for valid input (including a completed win/goal/draw),
**2** for invalid input (bad token, out-of-range cell, occupied cell, too many
moves, or `-` with extra arguments).

## How they differ from `apps/legacy_examples/games/*.c`

The existing games are interactive C compatibility demos; these cores are
non-interactive C+ regression fixtures:

| existing example | it is | this core instead |
| --- | --- | --- |
| `apps/legacy_examples/games/2048.c` | ncurses UI, raw keyboard, ANSI colours, `time`/`usleep`/`rand()` tile spawns, C17 | `game2048.cp`: C+ `class`, fixed seed, plain-text transcript + final board/score, no curses/time |
| `apps/legacy_examples/games/tetris.c` | ncurses/PDCurses falling-piece game, real-time gravity, wall kicks, key handling | `maze.cp` / `tictactoe.cp`: no graphics, no gravity/timing, one deterministic transcript from a fixed move list |
| `apps/legacy_examples/games/tiles.c` | termios raw mode, `rand()` spawns, interactive WASD sliding puzzle | `game2048.cp`: no raw terminal mode, no `rand()`, deterministic spawn from the fixed seed |
| `apps/legacy_examples/games/snake.c` | POSIX termios + `select()` `_kbhit` shim, `time.h`, interactive arrow keys | all three cores: no key polling, no clock, argv/stdin only |

The existing games are built by `apps/legacy_examples/build_examples.sh` and are
**not run** in automated verification because they need a terminal. These
cores are built and executed under a 30-second bound by `run.sh`, with exit
codes and argv-vs-stdin output equality checked.

## Exact build commands (from the repository root)

```sh
./build/c+ build apps/legacy_examples/expanded/games/cores/game2048.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games/cores/bin/game2048
./build/c+ build apps/legacy_examples/expanded/games/cores/maze.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games/cores/bin/maze
./build/c+ build apps/legacy_examples/expanded/games/cores/tictactoe.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games/cores/bin/tictactoe
```

All three compile with no diagnostics in strict mode.

## Exact runner command and output

```sh
$ apps/legacy_examples/expanded/games/cores/run.sh
BUILD ok game2048
RUN ok game2048
BUILD ok maze
RUN ok maze
BUILD ok tictactoe
RUN ok tictactoe
GAME_CORES 3 3 0
```

(`run.sh` exit status 0; `timeout 30` wraps every individual run.)

## Exact fixed-input runs

### 2048

```sh
$ timeout 30 apps/legacy_examples/expanded/games/cores/bin/game2048 R D L U R D
GAME2048 seed=0x9E3779B97F4A7C15 moves= 6
START score=0
     2    0    0    0
     0    0    0    0
     0    0    2    0
     0    0    0    0
MOVE 1 R changed=1 gained=0 score=0 spawn=r2c0:2
     0    0    0    2
     0    0    0    0
     2    0    0    2
     0    0    0    0
MOVE 2 D changed=1 gained=4 score=4 spawn=r2c3:2
     0    0    0    0
     0    0    0    0
     0    0    0    2
     2    0    0    4
MOVE 3 L changed=1 gained=0 score=4 spawn=r1c2:2
     0    0    0    0
     0    0    2    0
     2    0    0    0
     2    4    0    0
MOVE 4 U changed=1 gained=4 score=8 spawn=r0c3:2
     4    4    2    2
     0    0    0    0
     0    0    0    0
     0    0    0    0
MOVE 5 R changed=1 gained=12 score=20 spawn=r1c0:2
     0    0    8    4
     2    0    0    0
     0    0    0    0
     0    0    0    0
MOVE 6 D changed=1 gained=0 score=20 spawn=r0c0:2
     2    0    0    0
     0    0    0    0
     0    0    0    0
     2    0    8    4
FINAL score= 20 moves= 6 effective= 6 noops= 0 spawns= 8
FINAL_BOARD
     2    0    0    0
     0    0    0    0
     0    0    0    0
     2    0    8    4
```

`game2048 RDLURD` and `printf 'RDLURD\n' | game2048 -` print the same bytes.

### maze

```sh
$ timeout 30 apps/legacy_examples/expanded/games/cores/bin/maze U D D D D R R R R R
MAZE 9x7 start=(1,1) goal=(5,6)
INITIAL
  #########
  #@..#...#
  #.#.#.#.#
  #.#...#.#
  #.#####.#
  #.....G.#
  #########
STEP 1 U blocked pos=(1,1)
  #########
  #@..#...#
  #.#.#.#.#
  #.#...#.#
  #.#####.#
  #.....G.#
  #########
STEP 2 D moved pos=(2,1)
  #########
  #S..#...#
  #@#.#.#.#
  #.#...#.#
  #.#####.#
  #.....G.#
  #########
STEP 3 D moved pos=(3,1)
  #########
  #S..#...#
  #.#.#.#.#
  #@#...#.#
  #.#####.#
  #.....G.#
  #########
STEP 4 D moved pos=(4,1)
  #########
  #S..#...#
  #.#.#.#.#
  #.#...#.#
  #@#####.#
  #.....G.#
  #########
STEP 5 D moved pos=(5,1)
  #########
  #S..#...#
  #.#.#.#.#
  #.#...#.#
  #.#####.#
  #@....G.#
  #########
STEP 6 R moved pos=(5,2)
  #########
  #S..#...#
  #.#.#.#.#
  #.#...#.#
  #.#####.#
  #.@...G.#
  #########
STEP 7 R moved pos=(5,3)
  #########
  #S..#...#
  #.#.#.#.#
  #.#...#.#
  #.#####.#
  #..@..G.#
  #########
STEP 8 R moved pos=(5,4)
  #########
  #S..#...#
  #.#.#.#.#
  #.#...#.#
  #.#####.#
  #...@.G.#
  #########
STEP 9 R moved pos=(5,5)
  #########
  #S..#...#
  #.#.#.#.#
  #.#...#.#
  #.#####.#
  #....@G.#
  #########
STEP 10 R GOAL pos=(5,6)
  #########
  #S..#...#
  #.#.#.#.#
  #.#...#.#
  #.#####.#
  #.....@.#
  #########
GOAL step=10 pos=(5,6)
FINAL steps=9 blocked=1 goal=1 pos=(5,6) moves=10
STOP reason=goal at step=10
```

`maze UDDDDRRRRR` and `printf 'UDDDDRRRRR\n' | maze -` print the same bytes.

### tic-tac-toe

```sh
$ timeout 30 apps/legacy_examples/expanded/games/cores/bin/tictactoe 0 1 4 5 8
TICTACTOE players=X:fixed,O:fixed moves= 5
INITIAL
  . | . | .
  --+---+--
  . | . | .
  --+---+--
  . | . | .
STEP 1 X cell=0 pos=(0,0)
  X | . | .
  --+---+--
  . | . | .
  --+---+--
  . | . | .
STEP 2 O cell=1 pos=(0,1)
  X | O | .
  --+---+--
  . | . | .
  --+---+--
  . | . | .
STEP 3 X cell=4 pos=(1,1)
  X | O | .
  --+---+--
  . | X | .
  --+---+--
  . | . | .
STEP 4 O cell=5 pos=(1,2)
  X | O | .
  --+---+--
  . | X | O
  --+---+--
  . | . | .
STEP 5 X cell=8 pos=(2,2)
  X | O | .
  --+---+--
  . | X | O
  --+---+--
  . | . | X
RESULT win detail=diag-main step=5
FINAL result=win moves_played=5 x=3 o=2
```

`tictactoe 01458` and `printf '01458\n' | tictactoe -` print the same bytes.
With no arguments, `tictactoe` uses its built-in X/O plans and produces the
same win on the main diagonal.

## Verification summary

- All three `--strict --sanitize=none -I.` builds succeeded with no output.
- Each fixed-input argv run exited 0 under `timeout 30`.
- Each compact-token/stdin run exited 0 and its output was byte-identical to
  the argv run (`cmp -s`).
- Invalid inputs exited 2: `game2048 X`, `maze X`, `tictactoe 9`; additionally
  `tictactoe x`, `tictactoe 00` (occupied cell) and `tictactoe 0123456789`
  (too many moves) all exited 2.
- `run.sh` final line: `GAME_CORES 3 3 0`, exit status 0.
