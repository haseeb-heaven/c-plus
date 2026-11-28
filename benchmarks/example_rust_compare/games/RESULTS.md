# Game examples — C99 vs safe-Rust deterministic-core comparison

Branch: `agent/modern-stdlib`. Scope: the four sources in `examples/games/`
(`2048.c`, `snake.c`, `tetris.c`, `tiles.c`). All files produced by this task
live only in `benchmarks/example_rust_compare/games/`; no source under
`examples/` was edited, and no git add/commit was performed by this agent.

## Toolchains (exact, from the recorded run)

```
$ /usr/bin/clang --version | head -2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

$ command -v timeout
/usr/bin/timeout

$ uname -m
arm64

$ echo "$TERM"
xterm-256color
```

Archives on the C link line (required by the harness workflow, not used by
these games' code paths):

```
$ROOT/build/libc/libcplus_hardened.a
$ROOT/build/safe/libcplus_safecrt.a
```

`tetris.c` additionally needs `-lncurses` (verified available on this host:
a `/usr/bin/clang -std=c99 t.c -lncurses` smoke build succeeded).

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/games/compare.sh
```

`compare.sh` prints one `EXAMPLE_COMPARE` line per example on stdout, writes
build/run diagnostics to stderr, and leaves exact stdout/stderr artifacts and
sha256 hashes under `benchmarks/example_rust_compare/games/out/`.
Environment overrides: `GAME_CLANG`, `GAME_RUSTC`, `GAME_ARCHIVES`,
`GAME_TIMEOUT`, `GAME_OUT`.

## Method

Each original `.c` file is compiled **unmodified** into a driver:

```
/usr/bin/clang -std=c99 -I"$ROOT/examples/games" <name>_harness.c \
  "$ROOT/build/libc/libcplus_hardened.a" \
  "$ROOT/build/safe/libcplus_safecrt.a" -lncurses -o out/<name>_c

rustc -O <name>.rs -o out/<name>_rs

timeout 30 out/<name>_c > out/<name>.c.out 2> out/<name>.c.err
timeout 30 out/<name>_rs > out/<name>.rs.out 2> out/<name>.rs.err
```

The harness `#include`s the real example source with `main` renamed and
`rand`/`srand` redirected to a fixed-seed LCG
(`state = state*1103515245 + 12345 mod 2^32`, value `(state>>16)&0x7fff`,
seed constant `12345`; every `srand()` in game code resets to that constant,
exactly mirrored in Rust). This replaces only the clock-entropy source; every
board/move/collision/score operation is the game's own function. Each Rust
file is a from-scratch safe model of those same functions (no `unsafe`,
no FFI, no terminal I/O). Comparison is byte-exact stdout + exit status.

Fixed inputs/traces:

| example | deterministic trace |
|---|---|
| 2048 | board after `initBoard()` (two LCG spawns), then moves `a d w s a d s w` with an LCG spawn after each successful move, `ENDED`/`countEmpty`/`findPairDown`, then the game's own 13 `slideArray` self-tests |
| snake | LCG fruit setup, forced fruit 3 cells ahead of head at `(10,10)`, script `dddwwaaaaaaaaaaaa`, per-step head/tail/fruit/score/gameover |
| tetris | all 28 generated tetromino rotations, two 7-bags (14 pieces), script `ROT LEFT SOFT SOFT DROP ROT RIGHT DROP SOFT DROP`, full 200-cell board after every op, two `clear_lines` cases, gravity levels 1..12 |
| tiles | board after two LCG spawns, then directions `L U R D L L R U` with LCG spawn after each successful slide, final score |

## Recorded result

`sh benchmarks/example_rust_compare/games/compare.sh` (exit 0), stdout:

```
EXAMPLE_COMPARE 2048 MATCH deterministic-core seed=12345; interactive UI excluded
EXAMPLE_COMPARE snake MATCH deterministic-core seed=12345; interactive UI excluded
EXAMPLE_COMPARE tetris MATCH deterministic-core seed=12345; interactive UI excluded
EXAMPLE_COMPARE tiles MATCH deterministic-core seed=12345; interactive UI excluded
```

Build logs were empty (no compiler diagnostics). Both sides exited 0 for all
four runs. Exact stdout identity was confirmed by `cmp -s` and sha256:

| example | C/Rust stdout bytes | lines | sha256 (both sides) | exit C / Rust | status |
|---|---:|---:|---|---|---|
| `2048.c` ↔ `2048.rs` | 453 | 13 | `4885675e2d907ada51a76f0fc6183b33d2c395098ef56edd6fd5152e9b7e17e1` | 0 / 0 | MATCH |
| `snake.c` ↔ `snake.rs` | 984 | 21 | `4288b770f8d4b0a640a30069ce2d8de01012225c54a996d7248c596a8bff00ad` | 0 / 0 | MATCH |
| `tetris.c` ↔ `tetris.rs` | 4614 | 66 | `00a576d62081f42ca9ebd74a9005445d1446aa653adb20c55d3c745310673dd5` | 0 / 0 | MATCH |
| `tiles.c` ↔ `tiles.rs` | 405 | 11 | `7e77a4aa9fe9a6fda38eb720eb3370e250a7352c7f13e33de956313d348f5f55` | 0 / 0 | MATCH |

**Totals: 4 MATCH, 0 DIFFERENT, 0 BLOCKED (deterministic-core scope).**
The four traces contain 111 lines / 6456 bytes of identical output; all board
states, scores and exit codes agree.

Standalone originals also compile with the plain documented commands:

```
/usr/bin/clang -std=c99 examples/games/2048.c  -o out/orig_2048    # ok
/usr/bin/clang -std=c99 examples/games/snake.c  -o out/orig_snake   # ok
/usr/bin/clang -std=c99 examples/games/tiles.c  -o out/orig_tiles   # ok
/usr/bin/clang -std=c99 examples/games/tetris.c -lncurses -o out/orig_tetris  # ok
```

`2048.c` has deterministic CLI modes that were also checked directly:

```
$ timeout 10 out/orig_2048 --version
2048.c version 1.0.3                       # exit 0

$ timeout 10 out/orig_2048 test
All 13 tests executed successfully         # exit 0
```

The same `All 13 tests executed successfully` line and `TESTS_OK 1` marker are
present in both harness traces, so the game's own unit tests pass identically
on the Rust model.

## Interactive-only aspects — BLOCKED (with evidence)

These are *not* claimed equivalent; they are outside the deterministic-core
comparison above. The original programs are human/terminal/real-time driven
and seed from `time(NULL)` (and, for tetris, `getpid()`), so a live end-to-end
byte comparison is not reproducible:

* **2048 / tiles — time-seeded randomness and raw-terminal input.**
  ```
  for i in 1 2; do printf 'aaaa' | timeout 10 out/orig_2048 > run$i.out; sleep 2; done
  # run1 sha=e91bcc740dcd91ae, run2 sha=912a1355634af22f -> DIFFERENT
  for i in 1 2; do printf 'aaaa' | timeout 10 out/orig_tiles > t$i.out; sleep 2; done
  # t1 sha=79e8539734769ea3, t2 sha=b2c3937c30cf46a3 -> DIFFERENT; exit 124 (still waiting for keys)
  ```
  `srand((unsigned)time(NULL))` at `2048.c:282` and `tiles.c:56` makes the
  initial board and every spawn differ between runs with identical stdin.

* **snake — real-time loop and platform shell calls.**
  ```
  printf 'x' | timeout 20 out/orig_snake
  # exit 0, stdout tail "Game Over! Your Score: 0",
  # stderr head "sh: cls: command not found" (Draw() calls system("cls"))
  ```
  `main` sleeps 10 seconds per frame (`snake.c:194`) and `Setup`/fruit
  placement reseed from `time(NULL)` (`snake.c:56,181`). The harness bypasses
  `Draw()` and `Input()` and exercises `Logic()` directly.

* **tetris — full-screen ncurses UI, wall-clock gravity and pid/time RNG.**
  ```
  printf 'q' | timeout 10 out/orig_tetris > ui.out
  # exit 0, ui.out begins with ncurses escape sequences
  # "\e[?1049h\e[1;24r... T E T R I S ... Mode < Marathon >"
  ```
  `main` seeds with `srand(time(NULL) ^ getpid())` (`tetris.c:1402`), the game
  loop uses `CLOCK_MONOTONIC` and `napms(16)` (`tetris.c:1306-1341`), and
  `load_scores()/save_scores()` touch a user score directory. The harness
  exercises only the rules functions; no curses rendering, menu, timing or
  score-file I/O was modeled.

## Explicit non-equivalence statement

The four `.rs` files are **safe models of the deterministic game cores only**.
They are not drop-in replacements for the original programs: they do not
implement terminal/raw-mode handling, ANSI/ncurses rendering, sleep/gravity
timing, keyboard event loops, time/pid-seeded randomness, or tetris score-file
persistence. Byte-exact equality is established only for the fixed-seed
board/score traces described above. No claim of full-program behavioral
equivalence is made, and no live-game success is inferred from these builds or
emulated runs.
