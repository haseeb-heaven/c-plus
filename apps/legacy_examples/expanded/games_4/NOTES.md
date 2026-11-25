# games_4 — deterministic board-game cores

Three single-file C+ `.cp` programs, each a non-interactive logic core plus a
plain-text CLI driver. All files live only in `apps/legacy_examples/expanded/games_4/`.

| File | Model | Board |
| --- | --- | --- |
| `connect4.cp` | drop discs with gravity; 4-in-a-row win; column-bounds refusal | 6x7 |
| `checkers.cp` | men/kings, diagonal moves and single jumps, promotion at far row | 8x8 |
| `chesslite.cp` | **subset** — pawn/rook/knight only, legality and collision checks | 8x8 |

## Teaching model and explicit non-proof statement

**These are deterministic teaching models, not games, not a chess engine, and
not safety proofs.** The word "proof" in this campaign means only a *bounded
reasoning sketch* (below) about where checks occur in these files. There is no
model checker, theorem prover, fuzzer campaign, formal specification or
machine-checked invariant here. There is no claim of 100% memory safety, no
claim of Rust equivalence, and no claim that identical output for the tested
inputs says anything about untested inputs, longer executions, concurrency or
other platforms.

Concretely:

- A passing transcript is **runtime evidence for exactly the listed inputs**,
  not a universally quantified guarantee.
- `error` records and `REFUSE` lines are **recoverable refusals** observed at
  run time; they are not static proof obligations being discharged.
- `chesslite.cp` is explicitly **not full chess**: no king, queen or bishop; no
  castling, en passant, pawn promotion, check, checkmate, stalemate distinction,
  draw rule, clock, book or search. "Win" means only *opponent has no pieces or
  no legal move in this subset*.
- `checkers.cp` is an explicit rule subset: men move only forward, jumps capture
  one adjacent opponent, promotion occurs at the far row, and there are **no
  forced captures and no multi-jump chains**. A player with no pieces or no
  legal move loses.
- No wall clock, terminal UI, key loop, network, hidden seed or address entropy
  exists in any core. `vector`, `string` and `error` come from
  `stdlib/vector.hp`, `stdlib/string.hp` and `stdlib/error.hp`.

## Input contract

| Invocation | Meaning |
| --- | --- |
| `bin <tokens...>` | one column (`connect4`) or one `fr fc tr tc` move (other two) per token |
| `bin <compact-token>` | `connect4`: digits `0`..`6`, one column each; other two: concatenated 4-digit groups |
| `printf '<tokens>\n' \| bin -` | same tokens from one stdin line (`-` must be the only argument) |
| `bin` | built-in fixed default sequence |

Exit **0** for a valid sequence (including a completed win/draw), **2** for any
refusal. `connect4` also stops early on a win or a full board; checkers and
chesslite stop early on a win.

`connect4` compact rule: a multi-character token made only of digits `0`..`6`
is split into single-digit columns; any token containing `7`..`9`, a sign or
another character is parsed as one signed integer. This keeps `7` and `-1`
refusable as out-of-range columns instead of silently splitting them.

## Refusal taxonomy (all three cores)

| Code | Name | Meaning | `error.code()` | `error.category()` |
| --- | --- | --- | --- | --- |
| 1 | `E_OVERSIZE` | token exceeds parse bound (15 digits; or non-whole 4-digit group) | 101 (`CPLUS_EXIT_BOUNDS`) | 5 (`bounds`) |
| 2 | `E_RANGE` | value/coordinate outside the board | 101 | 5 |
| 3 | `E_ILLEGAL` | in-range but rule violation (full column, occupied/blocked path, wrong piece/direction) | 108 (`CPLUS_EXIT_UNSUPPORTED`) | 1 (`invalid`) |
| 4 | `E_SYNTAX` | malformed token (wrong group length, bad character) | 108 | 1 |
| 5 | `E_COUNT` | empty sequence, too many moves, or `-` with extra arguments | 108 | 1 |

Each refusal prints one `REFUSE step=... code=... name=... errcode=...
category=... detail=...` line on stdout and one
`<core>: error code=... category=... message=...` line on stderr, then exits 2.

## Bounded reasoning sketch (informal, not a proof)

- **connect4** — every board read goes through `tile()`, which returns `-1`
  for out-of-range coordinates instead of indexing the array. `drop()` refuses
  `col < 0 || col >= COLS` before scanning the column; the scan starts at
  `ROWS - 1` and only decrements. Win detection reads neighbours only through
  `tile()`. Numeric tokens are capped at 15 digits and value 1,000,000 *before*
  the `value * 10 + digit` step, so the int cannot wrap.
- **checkers** — `tile()` guards reads; `dark()` and explicit bounds checks gate
  `apply()`. A jump midpoint is read only after `|dr| == |dc| == 2` and the
  destination was accepted. `probe`-style validation completes before any
  mutation, so a refusal leaves state unchanged. `has_move()` also uses only
  `tile()` and `dark()`.
- **chesslite** — `apply()` bounds-checks all four coordinates, then runs the
  pure `probe()` completely before mutating; own-piece destinations and
  non-piece sources are refused. Rook path scanning is bounded by
  `steps < SIZE`. Pawn double-steps are allowed only from the home row with an
  empty intermediate square.

This is a description of check placement in the source, not a verified
property. A rule bug outside the exercised sequences could still exist.

## How these differ from `apps/legacy_examples/games/*.c`

Those are interactive C17 compatibility demos built by
`apps/legacy_examples/build_examples.sh` and not run in automated verification because they
need a terminal. These cores are C+ regression fixtures built in strict mode and
executed under `timeout 30`, with exit codes, repeat-run byte equality and
argv-vs-stdin byte equality checked by `run.sh`.

| Existing example | It is | These cores instead |
| --- | --- | --- |
| `apps/legacy_examples/games/2048.c` | ncurses UI, raw keyboard, ANSI colours, `time`/`usleep`/`rand()` spawns | `connect4.cp` / `checkers.cp` / `chesslite.cp`: no curses, no clock, no randomness; plain-text transcript only |
| `apps/legacy_examples/games/tetris.c` | ncurses falling pieces, real-time gravity, wall kicks, key handling | fixed 6x7/8x8 boards, one deterministic transcript from argv/stdin; no graphics or gravity |
| `apps/legacy_examples/games/tiles.c` | termios raw mode, `rand()` spawns, interactive WASD sliding puzzle | no raw terminal mode, no `rand()`, no hidden spawn state |
| `apps/legacy_examples/games/snake.c` | POSIX termios + `select()` `_kbhit`, `time.h`, arrow keys | no key polling, no clock; every move is an explicit token |
| Common style | raw C arrays, C17, libcurses/termios | C+ `class`, `vector`/`string`/`error` from the checked stdlib, `--strict --sanitize=none -I.` |

## Exact build commands and result (from the repository root)

```sh
./build/c+ build apps/legacy_examples/expanded/games_4/connect4.cp  --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games_4/bin/connect4
./build/c+ build apps/legacy_examples/expanded/games_4/checkers.cp  --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games_4/bin/checkers
./build/c+ build apps/legacy_examples/expanded/games_4/chesslite.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/games_4/bin/chesslite
```

All three exited 0 with **no compiler diagnostics** in strict mode.

## Exact runner command and verbatim output

```sh
$ timeout 120 sh apps/legacy_examples/expanded/games_4/run.sh
BUILD ok connect4
REFUSE ok connect4/col7 code=2 name=E_RANGE
REFUSE ok connect4/colminus1 code=2 name=E_RANGE
REFUSE ok connect4/oversize code=1 name=E_OVERSIZE
REFUSE ok connect4/fullcol code=3 name=E_ILLEGAL
RUN ok connect4
BUILD ok checkers
REFUSE ok checkers/coord8 code=2 name=E_RANGE
REFUSE ok checkers/oversize code=1 name=E_OVERSIZE
REFUSE ok checkers/illegal code=3 name=E_ILLEGAL
REFUSE ok checkers/badchar code=2 name=E_RANGE
RUN ok checkers
BUILD ok chesslite
REFUSE ok chesslite/coord8 code=2 name=E_RANGE
REFUSE ok chesslite/oversize code=1 name=E_OVERSIZE
REFUSE ok chesslite/blockedrook code=3 name=E_ILLEGAL
REFUSE ok chesslite/badchar code=2 name=E_RANGE
RUN ok chesslite
GAMES_4 3 3 0 12
```

Runner exit status: **0**. `GAMES_4 <games> <passed> <failed> <refused>` =
`3` cores, `3` passed, `0` failed, `12` hostile refusals correctly observed.
Every individual run is wrapped in `timeout 30`. `run.sh` also saves the full
verbatim argv, repeat-argv and stdin transcripts and all refusal logs in
`apps/legacy_examples/expanded/games_4/bin/`.

## Exact fixed-input runs (verbatim excerpts from the saved transcripts)

### connect4

```sh
$ timeout 30 apps/legacy_examples/expanded/games_4/bin/connect4 3 2 3 2 3 2 3
CONNECT4 rows=6 cols=7 moves=7
INITIAL
  0123456
  .......
  .......
  .......
  .......
  .......
  .......
STEP 1 player=R col=3 row=5
...
STEP 7 player=R col=3 row=2
  0123456
  .......
  .......
  ...R...
  ..YR...
  ..YR...
  ..YR...
RESULT win detail=vertical step=7
FINAL result=win played=7 moves=7 r=4 y=3
```

Full 67-line transcript: `apps/legacy_examples/expanded/games_4/bin/connect4.argv.out`.
`connect4 3232323` (compact) and `printf '3 2 3 2 3 2 3\n' | connect4 -` were
byte-identical to the argv run.

### checkers

```sh
$ timeout 30 apps/legacy_examples/expanded/games_4/bin/checkers 2130 5041 1021 4132 2334 3223 0110 2301
CHECKERS size=8 moves=8
INITIAL
  01234567
0  r r r r
1 r r r r 
2  r r r r
3 . . . . 
4  . . . .
5 w w w w 
6  w w w w
7 w w w w 
STEP 1 side=red from=(2,1) to=(3,0) capture=0 promote=0
...
STEP 8 side=white from=(2,3) to=(0,1) capture=1 promote=1
  01234567
0  W r r r
1 r . r r 
2  r . r r
3 r . r . 
4  . . . .
5 . w w w 
6  w w w w
7 w w w w 
RESULT incomplete detail=none step=8
FINAL result=incomplete plies=8 moves=8 side=red red=11 white=12
```

Full 93-line transcript: `apps/legacy_examples/expanded/games_4/bin/checkers.argv.out`.
Move 8 is a jump (`capture=1`) and a white promotion (`promote=1`, `w` -> `W`).
The compact and stdin forms were byte-identical to the argv run.

### chesslite (SUBSET)

```sh
$ timeout 30 apps/legacy_examples/expanded/games_4/bin/chesslite 0120 6343 1323 6151 0625 6757 2544 6656 1434 4334
CHESSLITE subset=pawn,rook,knight size=8 moves=10
INITIAL
  01234567
0 RN....NR
1 PPPPPPPP
2 ........
3 ........
4 ........
5 ........
6 pppppppp
7 rn....nr
STEP 1 side=white piece=N from=(0,1) to=(2,0) capture=0
...
STEP 10 side=black piece=p from=(4,3) to=(3,4) capture=1
  01234567
0 R......R
1 PPP..PPP
2 N..P....
3 ....p...
4 ....N...
5 .p....pp
6 p.p.pp..
7 rn....nr
RESULT incomplete detail=none step=10
FINAL result=incomplete plies=10 moves=10 side=white white=15 black=16 captures=1
```

Full 113-line transcript: `apps/legacy_examples/expanded/games_4/bin/chesslite.argv.out`.
Move 10 is a pawn capture. The compact and stdin forms were byte-identical.

## Exact hostile cases (all REFUSED, exit 2 each)

Verbatim stdout `REFUSE` line followed by the verbatim stderr `error` line:

```sh
$ timeout 30 bin/connect4 3 2 7 2 3 2 3
REFUSE step=3 code=2 name=E_RANGE errcode=101 category=5 detail=column outside 0..6
connect4: error code=101 category=5 message=column outside 0..6

$ timeout 30 bin/connect4 -1
REFUSE step=1 code=2 name=E_RANGE errcode=101 category=5 detail=column outside 0..6
connect4: error code=101 category=5 message=column outside 0..6

$ timeout 30 bin/connect4 99999999999999999999
REFUSE step=1 code=1 name=E_OVERSIZE errcode=101 category=5 detail=invalid column token
connect4: error code=101 category=5 message=invalid column token

$ timeout 30 bin/connect4 3 3 3 3 3 3 3
REFUSE step=7 code=3 name=E_ILLEGAL errcode=108 category=1 detail=column is full
connect4: error code=108 category=1 message=column is full

$ timeout 30 bin/checkers 2188
REFUSE step=1 code=2 name=E_RANGE errcode=101 category=5 detail=invalid move token
checkers: error code=101 category=5 message=invalid move token

$ timeout 30 bin/checkers 213045
REFUSE step=1 code=1 name=E_OVERSIZE errcode=101 category=5 detail=invalid move token
checkers: error code=101 category=5 message=invalid move token

$ timeout 30 bin/checkers 2131
REFUSE step=1 code=3 name=E_ILLEGAL errcode=108 category=1 detail=illegal checkers move
checkers: error code=108 category=1 message=illegal checkers move

$ timeout 30 bin/checkers 99zz
REFUSE step=1 code=2 name=E_RANGE errcode=101 category=5 detail=invalid move token
checkers: error code=101 category=5 message=invalid move token

$ timeout 30 bin/chesslite 0008
REFUSE step=1 code=2 name=E_RANGE errcode=101 category=5 detail=invalid move token
chesslite: error code=101 category=5 message=invalid move token

$ timeout 30 bin/chesslite 000000000
REFUSE step=1 code=1 name=E_OVERSIZE errcode=101 category=5 detail=invalid move token
chesslite: error code=101 category=5 message=invalid move token

$ timeout 30 bin/chesslite 0002
REFUSE step=1 code=3 name=E_ILLEGAL errcode=108 category=1 detail=illegal chesslite move
chesslite: error code=108 category=1 message=illegal chesslite move

$ timeout 30 bin/chesslite 99zz
REFUSE step=1 code=2 name=E_RANGE errcode=101 category=5 detail=invalid move token
chesslite: error code=101 category=5 message=invalid move token
```

`bin/` above means `apps/legacy_examples/expanded/games_4/bin`. The stderr line may appear
before the stdout line when a terminal interleaves the two streams; `run.sh`
captures both into one file with `>... 2>&1` and checks the stable `REFUSE`
fields, not the interleaving order.

Additional manual probes (not part of the 12 counted refusals) also exited 2:
`connect4 -99999999999999999999` and a 500-digit `connect4` token (both
`E_OVERSIZE`), `connect4 ""` / `checkers ""` (`E_COUNT`), and a 500-digit
`chesslite` token (`E_COUNT`, bounded move count).

## Verification summary

- Three `--strict --sanitize=none -I.` builds: exit 0, no diagnostics.
- Three argv runs under `timeout 30`: exit 0; repeat argv runs byte-identical
  (`cmp -s`); stdin runs byte-identical to argv (`cmp -s`).
- Twelve hostile runs: exit 2, expected `code`/`name` present in stdout.
- `run.sh`: `GAMES_4 3 3 0 12`, exit status 0.
- Only files under `apps/legacy_examples/expanded/games_4/` were created or modified.
  Nothing was staged, committed, or built outside that directory.

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh -n apps/legacy_examples/expanded/games_4/run.sh
timeout 120 sh apps/legacy_examples/expanded/games_4/run.sh
```

Environment observed: macOS arm64, Apple clang via `./build/c+` (`C+ 0.1.0`),
`/usr/bin/timeout`, `/bin/sh`. No other OS or architecture is claimed.
