# game_b — real C game converted to modern C+

**Status: COMPLETE (verified real run, no fabrication).**
`CONVERT game_b 14 14 0` — 14 checks, 14 matched, 0 differed.

---

## 1. Provenance

| item | value |
| --- | --- |
| Original source | `external_projects/game_b/original/2048.c` (577 lines, 12 762 bytes) |
| Program | Console "2048" v1.0.3, by Maurits van der Schee (LICENSE in `original/`) |
| Reference executable | `external_projects/game_b/2048_build` (Mach-O arm64, provided with the download) |
| `ORIGINAL_EVIDENCE.md` | **not present** in this checkout. No evidence was fabricated; the real downloaded source and the real prebuilt binary were used instead. |
| Modern output | `external_projects/game_b/cplus/2048_modern.cp` (converted from the original by copy, then modernized) |
| Byte-for-byte source diff | `external_projects/game_b/cplus/evidence/original_vs_modern.diff` |
| Full evidence | `external_projects/game_b/cplus/evidence/results/` (`summary.txt`, per-case `*.stdout`, `*.exit`, `build.log`) |
| Verification driver | `external_projects/game_b/cplus/verify.sh` — prints `CONVERT game_b <checks> <matched> <differed>`, exits nonzero on any mismatch |
| Conversion date | 2026-11-23 |

Reference cross-check: the original C source was also rebuilt with
`clang -isysroot "$(xcrun --show-sdk-path)" -O2 -o 2048_ref original/2048.c`.
Its `--help`, `--version` and `test` output matched the provided `2048_build`
byte-for-byte, confirming the prebuilt binary is the correct reference.

Exact refresh command (if `original/` is refreshed later):

```sh
cp external_projects/game_b/original/2048.c \
   /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/game_b_ref/2048_refresh.c
bash external_projects/game_b/cplus/verify.sh
```

---

## 2. Build and verification

Exact build command used (from the repository root, as required):

```sh
./build/c+ build external_projects/game_b/cplus/2048_modern.cp \
  --strict --sanitize=none -I. \
  -o external_projects/game_b/cplus/2048_modern
```

Result: **exit 0, no diagnostics** (`evidence/results/build.log` is empty).
The unmodified original copied as `.cp` fails `--strict` with 21 errors
(14 FFI, 6 lifetime, 1 arithmetic); log:
`evidence/baseline_strict.log` reproduced from the baseline probe.

Run under `timeout 30` with identical fixed inputs for both binaries:

```sh
bash external_projects/game_b/cplus/verify.sh
# CONVERT game_b 14 14 0
```

Stable across three consecutive runs. The deterministic checks are exact
stdout/stderr/exit comparisons. The RNG-driven board checks launch both
binaries concurrently so `srand(time(NULL))` sees the same wall-clock second;
their complete stdout streams (up to ~35 KB, including all ANSI colour codes)
are then compared byte-for-byte.

---

## 3. Line-level changes

Original lines → modern lines. Only the listed sites changed; everything else
is textually identical to the original.

| # | Original | Modern | Change | Why (strict rule / modernization) |
| --- | --- | --- | --- | --- |
| 1 | 12–20 | 12–33 | Added `#include "cplus/cplus.h"`, `stdlib/error.hp`, `stdlib/vector.hp`, `stdlib/string.hp`; added `<stdio.h>` comment for `putchar`, `<unistd.h>` for `STDOUT_FILENO`/`write` | modern C+ stdlib surface; no new link flags (header-only) |
| 2 | — | 44–84 | Added `static` helpers `gb_byte`, `gb_arg`, `gb_echo`, `gb_make_arg`, `gb_pow2` | strict-mode proof helpers; all `static`, so no exported symbol is added |
| 3 | 51 | 135 | `uint8_t x, y, fg, bg;` → `uint8_t x, y, fg = 0, bg = 0;` | `CPLUS-LIFETIME`: uninitialized read. `getColors()` overwrites both before every read, so output is unchanged |
| 4 | 72 | 159 | `1 << board[x][y]` → `gb_pow2(board[x][y])` | `CPLUS-FFI`: unproven variable shift count. Doubling loop is bit-identical for exponents 0..31 |
| 5 | 152 | 243 | `1 << array[t]` → `gb_pow2(array[t])` | same shift proof in `slideArray` scoring |
| 6 | 273–307 | 365–411 | `addRandom`: raw `uint8_t list[16][2]` replaced with checked `vector empties` of linear cell indices `x*SIZE+y`; modulus divisor hoisted to `empty_count` with an early return when zero; `cell` initialized to 0 | modernization (`stdlib/vector.hp`, `error.hp`) + `CPLUS-ARITH` remainder-by-zero proof + `CPLUS-LIFETIME`. Scan order and both `rand()` calls are in the same order, so the same cell and same tile value are chosen |
| 7 | 431–438 | 536–548 | SIGINT handler: two `printf` calls → `fflush(stdout)` + two async-signal-safe `write(STDOUT_FILENO, ...)` calls with the identical byte strings | `CPLUS-FFI`: printf is not async-signal-safe. Explicit flush preserves the original `exit()`-time flush ordering |
| 8 | 440–489 | 553–620 | `main`: argv obtained with `gb_arg()` sized builtin copies; all `strcmp(argv[1], ...)` comparisons replaced by checked C+ `string::equals`; invalid-option printf replaced by three printfs + `gb_echo()` byte echoes of `argv[1]`/`argv[0]` | `CPLUS-FFI`: argv parameter has unverifiable extent. Output bytes are identical for any argument length; allocation failure is recorded in an `error` record but never changes CLI output |

All game logic — `getColors`, `getDigitCount`, `findTarget`, `slideArray`,
`rotateBoard`, all four `move*` functions, `findPairDown`, `countEmpty`,
`gameEnded`, `initBoard`, `setBufferedInput`, `testSucceed`, the move loop,
key codes, `usleep(150ms)`, prompts and cursor/color escape sequences — is
textually unchanged (apart from the four shift/lifetime fixes above).

---

## 4. Byte-identical parts

- All 19 function declarations (names, parameter types, return types) — exact text match.
- All colour tables in `getColors` and all colour escape strings — exact match.
- `testSucceed()`'s 13 test vectors, iteration and all failure-print statements — exact match; prints `All 13 tests executed successfully`.
- Every board-rendering `printf` format string and escape sequence (`\033[H`, `\033[38;5;...m`, `\033[m`, `   ·   `, arrow legend, `\033[A`, cursor show/hide).
- Key-code mapping for all move keys (`4/a/h/←`, `6/d/l/→`, `8/w/k/↑`, `2/s/j/↓`), quit (`q`, `y`) and restart (`r`, `y`).
- Screen-erasing prefix `\033[?25l\033[2J`, all prompts (`QUIT?`, `RESTART?`, `GAME OVER`, `Error! Cannot read keyboard input!`) and final cursor restore `\033[?25h\033[m`.
- Complete stdout streams for all 14 verification cases (see `evidence/results/`).

---

## 5. ABI statement

- All 19 exported symbols of the original (18 functions plus `main`: `getColors`, `getDigitCount`,
  `drawBoard`, `findTarget`, `slideArray`, `rotateBoard`, `moveUp`, `moveLeft`,
  `moveDown`, `moveRight`, `findPairDown`, `countEmpty`, `gameEnded`,
  `addRandom`, `initBoard`, `setBufferedInput`, `testSucceed`,
  `signal_callback_handler`, plus `main`) are all present with **identical
  names and identical C signatures** — verified by `nm -gU` and by a literal
  diff of the declaration lines (Section 4).
- No exported symbol was removed or renamed. The new helpers are all `static`.
- The modern binary additionally exports the C+ runtime/stdlib support
  symbols (`_cplus_*`, `_stdlib_*`) required by `--strict` and the `.hp`
  headers; these are additive and do not alter any original symbol.
- `SIZE == 4`, `VERSION "1.0.3"`, all `uint8_t`/`uint32_t`/`bool` field and
  parameter widths, and the x86/arm64 C ABI conventions are unchanged. No raw
  game-memory offsets were introduced.

---

## 6. Before / after outputs (recorded, byte-identical)

`BEFORE` = output of the original `2048_build`; `AFTER` = output of
`2048_modern.cp`. Both were captured under `timeout 30` with the same inputs.
`argv[0]` in the invalid-option case is normalized to `./2048` in both.

**`--help` / `-h` — exit 0, 338 bytes, identical**

```
Usage: 2048 [OPTION] | [MODE]
Play the game 2048 in the console

Options:
  -h,  --help       Show this help message.
  -v,  --version    Show version number.

Modes:
  bluered      Use a blue-to-red color scheme (requires 256-color terminal support).
  blackwhite   The black-to-white color scheme (requires 256-color terminal support).
```

**`--version` / `-v` — exit 0, 21 bytes, identical**

```
2048.c version 1.0.3
```

**`test` — exit 0, 35 bytes, identical**

```
All 13 tests executed successfully
```

**`--bogus` — exit 1, 63 bytes, identical**

```
Invalid option: --bogus

Try './2048 --help' for more options.
```

**`-x` — exit 1, 58 bytes, identical** (same template with the shorter token).

**Board/RNG/signal cases** — identical byte-for-byte, including all ANSI
escape bytes. Summary of the 14 checks (`evidence/results/summary.txt`):

```
build_exit=0
MATCH cli_help exit=0
MATCH cli_-h exit=0
MATCH cli_version exit=0
MATCH cli_-v exit=0
MATCH cli_test exit=0
MATCH cli_bogus exit=1
MATCH cli_-x exit=1
MATCH mode_default_xqy exit=0
MATCH mode_default_move exit=0
MATCH mode_blackwhite exit=0
MATCH mode_bluered exit=0
MATCH mode_whiteblack exit=0
MATCH mode_restart exit=0
MATCH sigint exit=2
CONVERT game_b 14 14 0
```

Fixed inputs for the board cases: `xqy`, `aaaaaassssddddwwww`,
`aaaaaassss` (three colour modes), `ryy`, and a SIGINT delivered while the
game blocks on `getchar()`. Each output stream is stored in
`evidence/results/<case>.stdout`, with its exit status in `<case>.exit`.

---

## 7. Files created (only inside my owned directory)

```
external_projects/game_b/cplus/2048_modern.cp        # converted source
external_projects/game_b/cplus/2048_modern            # strict-built binary
external_projects/game_b/cplus/verify.sh              # verification driver
external_projects/game_b/cplus/CONVERSION.md          # this report
external_projects/game_b/cplus/evidence/build.log     # strict build log (empty)
external_projects/game_b/cplus/evidence/baseline_strict.log  # original-as-.cp: 21 strict errors
external_projects/game_b/cplus/evidence/original_vs_modern.diff
external_projects/game_b/cplus/evidence/results/summary.txt
external_projects/game_b/cplus/evidence/results/*.stdout, *.exit, build.log, reference_build.log
```

No file under `original/` was modified. No `git add` / `git commit` was run.
