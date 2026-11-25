# Deterministic game cores (games_2)

## Explicit scope statement

These three `.cp` programs are **deterministic logic models, not interactive
games**. They are plain CLI filters over a fixed command sequence: no
terminal graphics/curses/ANSI UI, no keyboard or event loop, no wall clock,
no `time()`/`usleep()`, no randomness beyond a fixed seed used only for
initial setup, and no network. A given input always produces byte-identical
stdout, so they are suitable as regression fixtures.

- `pong.cp` — 1D paddle + ball on a 21-column grid (col 0 = goal, col 20 =
  right wall). Seed `0x1A2B3C4D` fixes only the initial ball direction and
  paddle offset. Commands `L`/`R`/`S` (`.` also means still). Wall reflection,
  paddle saves, goal misses with ball reset, and scoring are deterministic.
- `breakout.cp` — 4x16 brick grid, 16x12 playfield, 3-wide paddle, 3 lives.
  Seed `0x5EED1234` fixes only the initial ball direction. Commands `L`/`R`/`S`.
  Side/top-wall reflection, brick destruction (+10 points), paddle saves,
  life loss/reset, and win/game-over detection are deterministic.
- `invaders.cp` — fixed 3x5 enemy formation (15 enemies, row values
  30/20/10), player ship, bounded 16-slot bullet pool. Seed `0x13579BDF`
  fixes only the initial ship column. Commands `L`/`R`/`S`/`F` (fire).
  Projectile motion and enemy hits are deterministic.

Each model uses the C+ standard library: `stdlib/vector.hp` holds the parsed
command sequence, `stdlib/string.hp` parses argv/stdin tokens, and
`stdlib/error.hp` supplies the `error` value used for clean refusals. All
three build in `--strict --sanitize=none` mode with no diagnostics.

## Input contract (all three)

| invocation | meaning |
| --- | --- |
| `bin L R S` | commands as separate argv tokens |
| `bin LRS` | one compact argv token |
| `printf 'LRS\n' \| bin -` | one line from stdin |
| `bin` | built-in fixed default sequence |

Limits (all three): token length <= 64 characters, at most 128 ticks.
Exit status: **0** for valid input, **2** for invalid/oversized input.
Adverse inputs are refused cleanly (exit 2 and a `refused:` message on
stderr); they never time out, crash, or raise a sanitizer/runtime fatal.

## Exact build commands (from the repository root)

```sh
./build/c+ build examples/expanded/games_2/pong.cp --strict --sanitize=none -I. -o examples/expanded/games_2/bin/pong
./build/c+ build examples/expanded/games_2/breakout.cp --strict --sanitize=none -I. -o examples/expanded/games_2/bin/breakout
./build/c+ build examples/expanded/games_2/invaders.cp --strict --sanitize=none -I. -o examples/expanded/games_2/bin/invaders
```

All three exit 0 and print no diagnostics.

## Exact runner command and output

```sh
$ examples/expanded/games_2/run.sh
BUILD ok pong
REFUSE ok pong/badcmd rc=2
REFUSE ok pong/oversized rc=2
REFUSE ok pong/toomany rc=2
GAME ok pong
BUILD ok breakout
REFUSE ok breakout/badcmd rc=2
REFUSE ok breakout/oversized rc=2
REFUSE ok breakout/toomany rc=2
GAME ok breakout
BUILD ok invaders
REFUSE ok invaders/badcmd rc=2
REFUSE ok invaders/oversized rc=2
REFUSE ok invaders/toomany rc=2
GAME ok invaders
GAMES_2 3 3 0
```

`run.sh` exit status: 0. Every individual program run is
wrapped in `timeout 30`. For each game the runner compares the compact-token,
per-character-argv and stdin transcripts byte-for-byte (`cmp -s`), then checks
three adverse scenarios. Final line: `GAMES_2 3 3 0`.

## Exact fixed-input runs

### pong

```sh
$ timeout 30 examples/expanded/games_2/bin/pong 'SSSSSSSSSSRRRRRRRRRRSSSSSSSSSSSSSSSSSSSSSSSSSSSSSS'
PONG seed=0x1A2B3C4D grid=21 paddle_w=3 max_ticks=128 cmds=50
INIT ball=10 v=-1 paddle=[0,2] score=0 opp=0
  ===.......o.........|
TICK 1 cmd=S ball=9 v=-1 paddle=[0,2] event=none score=0 opp=0
  ===......o..........|
TICK 2 cmd=S ball=8 v=-1 paddle=[0,2] event=none score=0 opp=0
  ===.....o...........|
TICK 3 cmd=S ball=7 v=-1 paddle=[0,2] event=none score=0 opp=0
  ===....o............|
TICK 4 cmd=S ball=6 v=-1 paddle=[0,2] event=none score=0 opp=0
  ===...o.............|
TICK 5 cmd=S ball=5 v=-1 paddle=[0,2] event=none score=0 opp=0
  ===..o..............|
TICK 6 cmd=S ball=4 v=-1 paddle=[0,2] event=none score=0 opp=0
  ===.o...............|
TICK 7 cmd=S ball=3 v=-1 paddle=[0,2] event=none score=0 opp=0
  ===o................|
TICK 8 cmd=S ball=2 v=-1 paddle=[0,2] event=none score=0 opp=0
  ==o.................|
TICK 9 cmd=S ball=1 v=-1 paddle=[0,2] event=none score=0 opp=0
  =o=.................|
TICK 10 cmd=S ball=1 v=+1 paddle=[0,2] event=save score=1 opp=0
  =o=.................|
TICK 11 cmd=R ball=2 v=+1 paddle=[1,3] event=none score=1 opp=0
  .=o=................|
TICK 12 cmd=R ball=3 v=+1 paddle=[2,4] event=none score=1 opp=0
  ..=o=...............|
TICK 13 cmd=R ball=4 v=+1 paddle=[3,5] event=none score=1 opp=0
  ...=o=..............|
TICK 14 cmd=R ball=5 v=+1 paddle=[4,6] event=none score=1 opp=0
  ....=o=.............|
TICK 15 cmd=R ball=6 v=+1 paddle=[5,7] event=none score=1 opp=0
  .....=o=............|
TICK 16 cmd=R ball=7 v=+1 paddle=[6,8] event=none score=1 opp=0
  ......=o=...........|
TICK 17 cmd=R ball=8 v=+1 paddle=[7,9] event=none score=1 opp=0
  .......=o=..........|
TICK 18 cmd=R ball=9 v=+1 paddle=[8,10] event=none score=1 opp=0
  ........=o=.........|
TICK 19 cmd=R ball=10 v=+1 paddle=[9,11] event=none score=1 opp=0
  .........=o=........|
TICK 20 cmd=R ball=11 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........=o=.......|
TICK 21 cmd=S ball=12 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........==o.......|
TICK 22 cmd=S ball=13 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........===o......|
TICK 23 cmd=S ball=14 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........===.o.....|
TICK 24 cmd=S ball=15 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........===..o....|
TICK 25 cmd=S ball=16 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........===...o...|
TICK 26 cmd=S ball=17 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........===....o..|
TICK 27 cmd=S ball=18 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........===.....o.|
TICK 28 cmd=S ball=19 v=+1 paddle=[10,12] event=none score=1 opp=0
  ..........===......o|
TICK 29 cmd=S ball=19 v=-1 paddle=[10,12] event=wall score=1 opp=0
  ..........===......o|
TICK 30 cmd=S ball=18 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........===.....o.|
TICK 31 cmd=S ball=17 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........===....o..|
TICK 32 cmd=S ball=16 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........===...o...|
TICK 33 cmd=S ball=15 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........===..o....|
TICK 34 cmd=S ball=14 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........===.o.....|
TICK 35 cmd=S ball=13 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........===o......|
TICK 36 cmd=S ball=12 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........==o.......|
TICK 37 cmd=S ball=11 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........=o=.......|
TICK 38 cmd=S ball=10 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..........o==.......|
TICK 39 cmd=S ball=9 v=-1 paddle=[10,12] event=none score=1 opp=0
  .........o===.......|
TICK 40 cmd=S ball=8 v=-1 paddle=[10,12] event=none score=1 opp=0
  ........o.===.......|
TICK 41 cmd=S ball=7 v=-1 paddle=[10,12] event=none score=1 opp=0
  .......o..===.......|
TICK 42 cmd=S ball=6 v=-1 paddle=[10,12] event=none score=1 opp=0
  ......o...===.......|
TICK 43 cmd=S ball=5 v=-1 paddle=[10,12] event=none score=1 opp=0
  .....o....===.......|
TICK 44 cmd=S ball=4 v=-1 paddle=[10,12] event=none score=1 opp=0
  ....o.....===.......|
TICK 45 cmd=S ball=3 v=-1 paddle=[10,12] event=none score=1 opp=0
  ...o......===.......|
TICK 46 cmd=S ball=2 v=-1 paddle=[10,12] event=none score=1 opp=0
  ..o.......===.......|
TICK 47 cmd=S ball=1 v=-1 paddle=[10,12] event=none score=1 opp=0
  .o........===.......|
TICK 48 cmd=S ball=10 v=+1 paddle=[10,12] event=miss score=1 opp=1
  ..........o==.......|
TICK 49 cmd=S ball=11 v=+1 paddle=[10,12] event=none score=1 opp=1
  ..........=o=.......|
TICK 50 cmd=S ball=12 v=+1 paddle=[10,12] event=none score=1 opp=1
  ..........==o.......|
FINAL ticks=50 saves=1 misses=1 wall_hits=1 score=1 opp=1
```

This transcript shows a paddle save (tick 10), a right-wall reflection
(tick 29) and a goal miss with reset (tick 48): final line
`FINAL ticks=50 saves=1 misses=1 wall_hits=1 score=1 opp=1`.

### breakout

```sh
$ timeout 30 examples/expanded/games_2/bin/breakout 'RLSRRLSSLLRRRLSRSSLLRRRLLSRRRLS'
```

Verbatim first 30 lines:

```
BREAKOUT seed=0x5EED1234 grid=16x12 bricks=64 paddle_w=3 lives=3 cmds=31
INIT ball=(8,9) v=(+1,-1) paddle=[0,2] score=0 bricks=64
  ################
  ################
  ################
  ################
  ................
  ................
  ................
  ................
  ................
  ........o.......
  ................
  ===.............
TICK 1 cmd=R ball=(9,8) v=(+1,-1) paddle=[1,3] event=none score=0 bricks=64 lives=3
  ################
  ################
  ################
  ################
  ................
  ................
  ................
  ................
  .........o......
  ................
  ................
  .===............
TICK 2 cmd=L ball=(10,7) v=(+1,-1) paddle=[0,2] event=none score=0 bricks=64 lives=3
  ################
  ################
```

Verbatim final line:

```
FINAL outcome=incomplete ticks=31 score=20 bricks=62 paddle_hits=1 wall_hits=2 brick_hits=2 lives=2
```

The complete byte-exact 418-line transcript is regenerated by `run.sh` at
`examples/expanded/games_2/bin/breakout.compact.out`.

### invaders

```sh
$ timeout 30 examples/expanded/games_2/bin/invaders 'LFRFLFSRFLFLSFRFLFFFF'
```

Verbatim first 30 lines:

```
INVADERS seed=0x13579BDF grid=16x10 formation=3x5 enemies=15 max_ticks=128 cmds=21
INIT ship=9 score=0 enemies=15 bullets=0
  .....EEEEE......
  .....EEEEE......
  .....EEEEE......
  ................
  ................
  ................
  ................
  ................
  ................
  .........A......
TICK 1 cmd=L ship=8 bullets=0 event=none score=0 enemies=15
  .....EEEEE......
  .....EEEEE......
  .....EEEEE......
  ................
  ................
  ................
  ................
  ................
  ................
  ........A.......
TICK 2 cmd=F ship=8 bullets=1 event=fire score=0 enemies=15
  .....EEEEE......
  .....EEEEE......
  .....EEEEE......
  ................
  ................
  ................
```

Verbatim final line:

```
FINAL outcome=incomplete ticks=21 score=100 enemies=9 fired=11 hits=6 shots_lost=0
```

The complete byte-exact 244-line transcript is regenerated by `run.sh` at
`examples/expanded/games_2/bin/invaders.compact.out`.

## Safety-refusal evidence

Nine adverse cases (3 per game). Every one exited **2** (not 124/134/139),
i.e. a clean refusal rather than a timeout or crash. The stderr is quoted
verbatim; stdout was empty in each case.

| game | exact command | rc | stderr (verbatim) |
| --- | --- | --- | --- |
| pong | `bin/pong X` | 2 | `pong: refused: invalid command token (expected L/R/S)` |
| pong | `bin/pong '<200 R chars>'` | 2 | `pong: refused: token is oversized (max 64 characters)` |
| pong | `bin/pong L L ... L  (129 tokens)` | 2 | `pong: refused: too many commands (max 128 ticks)` |
| breakout | `bin/breakout X` | 2 | `breakout: refused: invalid command token (expected L/R/S)` |
| breakout | `bin/breakout '<200 R chars>'` | 2 | `breakout: refused: token is oversized (max 64 characters)` |
| breakout | `bin/breakout L L ... L  (129 tokens)` | 2 | `breakout: refused: too many commands (max 128 ticks)` |
| invaders | `bin/invaders X` | 2 | `invaders: refused: invalid command token (expected L/R/S/F)` |
| invaders | `bin/invaders '<200 R chars>'` | 2 | `invaders: refused: token is oversized (max 64 characters)` |
| invaders | `bin/invaders L L ... L  (129 tokens)` | 2 | `invaders: refused: too many commands (max 128 ticks)` |

Oversized **stdin** lines are refused by the same guard: feeding a 200-character
line to `bin/<game> -` exits 2 with `<game>: refused: stdin line is oversized`.
A 64-character stdin line (the documented limit, including its trailing
newline) is accepted and exits 0.

## Verification summary

- [x] All three `--strict --sanitize=none -I.` builds: exit 0, no output.
- [x] Fixed-input argv runs: exit 0 under `timeout 30`.
- [x] Compact-token, per-character-argv and stdin runs: byte-identical output.
- [x] 3 adverse scenarios per game: all exit 2 with `refused:` messages.
- [x] `run.sh` final line: `GAMES_2 3 3 0`, exit status 0.
