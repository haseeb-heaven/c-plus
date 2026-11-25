# Deterministic puzzle cores (games_3)

## Teaching model — NOT a safety proof

These three `.cp` programs are **teaching models of deterministic puzzle
cores, not formal proofs and not interactive games**. They have no terminal
graphics, no keyboard/event loop, no wall clock, no randomness and no network.
Every run is a pure function of fixed static data plus its argv tokens.

The "safety" shown here is **observed refusal evidence, not a proof**:

- each out-of-range / oversized / illegal input is detected by an explicit
  run-time bounds check and reported through `stdlib/error.hp` (`error` value)
  with a stable symbolic code and a nonzero process exit;
- the checks are ordinary C+ conditionals, not a proof engine, model checker
  or verified contract. They demonstrate that the hostile inputs in `run.sh`
  are refused on this toolchain; they do **not** establish absence of all
  memory errors for all possible inputs, nor any formal memory-safety claim.

No `git add`, commit, or edit to any file outside
`examples/expanded/games_3/` was made by this work. (A stray directory created
by an early `run.sh` path bug outside the repository was removed.)

## The three cores

| file | model | fixed data | win/lose |
| --- | --- | --- | --- |
| `sudoku.cp` | 9x9 Sudoku, place/remove/validate | 30 givens + 1 fixed solution | `solved=1` when every row/col/box is 1..9 |
| `minesweeper.cp` | 9x9 Minesweeper, reveal/flag/status | 11 fixed mines, 70 safe cells | flood reveal; win when all safe cells revealed, lose on a mine |
| `nim.cp` | 3-heap Nim, two deterministic players | heaps `[3,4,5]` | A plays optimal nim-sum, B take-away; last remover wins |

Common library use: `stdlib/vector.hp` stores the parsed argv program (read
through `at_checked`, an explicit bounds check), `stdlib/string.hp` parses
argv tokens without unchecked pointer walks, and `stdlib/error.hp` supplies the
`error` record behind each refusal. Integer tokens are parsed by a strict,
overflow-aware helper that guards **before** multiplying, so oversized input
is refused as `E_PARSE` rather than wrapping. Cell/heap indices are checked
against their range **before** any multiply or array access.

## Exact build commands (from the repository root)

```
./build/c+ build examples/expanded/games_3/sudoku.cp --strict --sanitize=none -I. -o examples/expanded/games_3/bin/sudoku
./build/c+ build examples/expanded/games_3/minesweeper.cp --strict --sanitize=none -I. -o examples/expanded/games_3/bin/minesweeper
./build/c+ build examples/expanded/games_3/nim.cp --strict --sanitize=none -I. -o examples/expanded/games_3/bin/nim
```

All three build with exit 0 and **zero bytes** on stdout/stderr (the `.build.log`
files in `bin/` are empty). `--strict --sanitize=none` is exactly the mandated
mode.

## Valid runs (exit 0, deterministic)

```
timeout 30 examples/expanded/games_3/bin/sudoku                 # default solve
timeout 30 examples/expanded/games_3/bin/sudoku P 0 2 4 V       # argv place + validate
timeout 30 examples/expanded/games_3/bin/minesweeper            # default clear-the-board
timeout 30 examples/expanded/games_3/bin/minesweeper R 4 4 V    # argv reveal + status
timeout 30 examples/expanded/games_3/bin/nim                    # default A vs B game
timeout 30 examples/expanded/games_3/bin/nim O T 0 1 O T 1 1 S  # argv moves + status
```

Exact final lines observed:

```
SUDOKU_DONE status=OK solved=1          # default; FINAL solved=1 changes=51 steps=52
SUDOKU_DONE status=OK solved=0          # argv P 0 2 4 V (one move, not yet solved)
MINESWEEPER_DONE status=OK state=win    # default; RESULT win step=63 revealed=70
MINESWEEPER_DONE status=OK state=playing
NIM_DONE status=OK winner=A             # default; RESULT winner=A moves=11
NIM_DONE status=OK winner=none          # argv ended before the board was empty
```

Default Nim transcript (exact, all 11 moves):

```
NIM heaps=[3,4,5] heap_count=3 mode=builtin
INITIAL heaps=[3,4,5] xor=2
MOVE 1 player=A strategy=optimal heap=0 amount=2 bound=ok xor_before=2 xor_after=0 heaps=[1,4,5]
MOVE 2 player=B strategy=takeaway heap=0 amount=1 bound=ok xor_before=0 xor_after=1 heaps=[0,4,5]
MOVE 3 player=A strategy=optimal heap=2 amount=1 bound=ok xor_before=1 xor_after=0 heaps=[0,4,4]
MOVE 4 player=B strategy=takeaway heap=1 amount=1 bound=ok xor_before=0 xor_after=7 heaps=[0,3,4]
MOVE 5 player=A strategy=optimal heap=2 amount=1 bound=ok xor_before=7 xor_after=0 heaps=[0,3,3]
MOVE 6 player=B strategy=takeaway heap=1 amount=1 bound=ok xor_before=0 xor_after=1 heaps=[0,2,3]
MOVE 7 player=A strategy=optimal heap=2 amount=1 bound=ok xor_before=1 xor_after=0 heaps=[0,2,2]
MOVE 8 player=B strategy=takeaway heap=1 amount=1 bound=ok xor_before=0 xor_after=3 heaps=[0,1,2]
MOVE 9 player=A strategy=optimal heap=2 amount=1 bound=ok xor_before=3 xor_after=0 heaps=[0,1,1]
MOVE 10 player=B strategy=takeaway heap=1 amount=1 bound=ok xor_before=0 xor_after=1 heaps=[0,0,1]
MOVE 11 player=A strategy=optimal heap=2 amount=1 bound=ok xor_before=1 xor_after=0 heaps=[0,0,0]
RESULT winner=A moves=11 heaps=[0,0,0]
FINAL winner=A moves=11
NIM_DONE status=OK winner=A
```

`bound=ok` is emitted only after the shared `take(i, amount)` guard has
accepted the move; that guard rejects `amount < 1` and `amount > heaps[i]`,
so neither the optimal nor the take-away strategy can remove more than a heap.
This is an observed run-time check, not a proof.

## Hostile cases — all REFUSED (exit code 2)

Every case below was run with `timeout 30` and produced exit 2 plus a REFUSE
line with the shown code. Exact REFUSE lines:

```
# sudoku
P 9 0 5            -> REFUSE step=1 cmd=P r=9 c=0 d=5 code=E_BOUNDS reason=cell index or digit out of range (0..8 / 1..9)
P -1 0 5           -> REFUSE step=1 cmd=P r=-1 c=0 d=5 code=E_BOUNDS reason=cell index or digit out of range (0..8 / 1..9)
P 100000000 0 5    -> REFUSE step=0 arg=100000000 code=E_PARSE reason=numeric token is oversized
P 0 0 4            -> REFUSE step=1 cmd=P r=0 c=0 d=4 code=E_GIVEN reason=cell is a fixed given
P 0 2 5            -> REFUSE step=1 cmd=P r=0 c=2 d=5 code=E_CONFLICT reason=digit conflicts with row, column or box
R 0 0              -> REFUSE step=1 cmd=R r=0 c=0 code=E_GIVEN reason=cell is a fixed given

# minesweeper
R 9 0              -> REFUSE step=1 cmd=R r=9 c=0 code=E_BOUNDS reason=cell coordinate out of range (0..8)
R -1 0             -> REFUSE step=1 cmd=R r=-1 c=0 code=E_BOUNDS reason=cell coordinate out of range (0..8)
F 3 9              -> REFUSE step=1 cmd=F r=3 c=9 code=E_BOUNDS reason=cell coordinate out of range (0..8)
R 0 999999999      -> REFUSE step=0 arg=999999999 code=E_PARSE reason=numeric token is oversized
R 4 4 F 4 4        -> REFUSE step=2 cmd=F r=4 c=4 code=E_REVEALED reason=cell is already revealed
F 4 4 R 4 4        -> REFUSE step=2 cmd=R r=4 c=4 code=E_FLAGGED reason=cell is flagged; unflag it first

# nim
T 3 1              -> REFUSE step=1 cmd=T i=3 a=1 code=E_BOUNDS reason=heap index out of range (0..2)
T -1 1             -> REFUSE step=1 cmd=T i=-1 a=1 code=E_BOUNDS reason=heap index out of range (0..2)
T 0 99             -> REFUSE step=1 cmd=T i=0 a=99 code=E_AMOUNT reason=amount must be in [1, heap size]
T 0 0              -> REFUSE step=1 cmd=T i=0 a=0 code=E_AMOUNT reason=amount must be in [1, heap size]
T 0 100000000000   -> REFUSE step=0 arg=100000000000 code=E_PARSE reason=numeric token is oversized
T 0 1 T 0 1 T 0 1 T 0 1
                   -> REFUSE step=4 cmd=T i=0 a=1 code=E_AMOUNT reason=amount must be in [1, heap size]
```

Each of these also prints a `<CORE>_DONE status=REFUSED code=<CODE>` line, so
`run.sh` checks both the REFUSE code and the REFUSED status plus the nonzero
exit.

## `run.sh` — full verification

```
sh examples/expanded/games_3/run.sh
```

It rebuilds all three cores from the repo root, runs each default and argv
case under `timeout 30`, compares two default runs byte-for-byte, and runs
all 18 hostile cases under `timeout 30`. Exact final line:

```
GAMES_3 3 3 0 18
```

(3 games, 3 passed, 0 failed, 18 hostile cases refused.) The script exits 0.
All intermediate logs and transcripts are kept in
`examples/expanded/games_3/bin/`.

## Re-verification from a clean state

```
rm -f examples/expanded/games_3/bin/*
sh examples/expanded/games_3/run.sh
```

Expected: build lines for all three, `GAME ok` for all three, 18 `REFUSE ok`
lines, the `GAMES_3 3 3 0 18` line, and exit 0.
