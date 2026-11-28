# Original-source evidence — game_b

 This directory preserves a **second** real, already-made, self-contained
 terminal C game, downloaded unmodified from a public GitHub repository.
 All data below was observed directly; nothing is reconstructed from memory.

 ## 1. Provenance

 | Field | Value |
 |---|---|
 | Game | 2048.c — console version of the game "2048" for GNU/Linux (single self-contained C file) |
 | Author (upstream) | Maurits van der Schee |
 | Repository | `https://github.com/mevdschee/2048.c` |
 | API metadata | `GET https://api.github.com/repos/mevdschee/2048.c` (HTTP 200) → `full_name=mevdschee/2048.c`, `default_branch=main`, `license.spdx_id=MIT`, `pushed_at=2026-10-07T09:38:05Z` |
 | Pinned commit (git SHA) | `90343eb209e84fa9687d596f5d5eecfdf7d4df51` |
 | Commit metadata | `GET https://api.github.com/repos/mevdschee/2048.c/commits/main` → sha `90343eb209e84fa9687d596f5d5eecfdf7d4df51`, author date `2026-10-07T09:37:19Z`, message `update` |
 | Raw source URL (pinned) | `https://raw.githubusercontent.com/mevdschee/2048.c/90343eb209e84fa9687d596f5d5eecfdf7d4df51/2048.c` |
 | Raw license URL (pinned) | `https://raw.githubusercontent.com/mevdschee/2048.c/90343eb209e84fa9687d596f5d5eecfdf7d4df51/LICENSE` |
 | License | **MIT** (permissive — allowed). Verified two ways: (a) GitHub API `license.spdx_id = MIT`; (b) the `LICENSE` file downloaded from the pinned commit is the standard MIT text, "The MIT License (MIT) / Copyright (c) 2024 Maurits van der Schee", with no additional or restrictive clauses. NO GPL. |
 | Retrieval date | 2026-11-23 |
 | Not a snake clone | It is a terminal 2048 sliding-tile puzzle (explicitly an allowed example in the task). |

 ## 2. Preserved original files and hashes

 Downloaded with `curl -fsS` from the pinned raw URLs into `original/` and never edited.

 | File | Bytes | SHA-256 |
 |---|---|---|
 | `original/2048.c` | 12762 | `90f794c675a696aa10980dd120d1f0503ee4ed33a61fef92c7df8f443f5b18a8` |
 | `original/LICENSE` | 1088 | `593783abd95546a2817d104b2656dd985a979a9e90da895729f211fc844b6402` |

 Re-verification (streaming `curl … | cmp - original/<file>`, no intermediate file):

 ```
 2048.c: IDENTICAL to pinned upstream bytes
 LICENSE: IDENTICAL to pinned upstream bytes
 ```

 **Original-source-unchanged statement:** The files under `external_projects/game_b/original/`
 are byte-for-byte copies of the upstream files at the pinned commit. They were written
 once at download time and were not modified afterward; the `cmp` check above confirms
 this against fresh downloads from the pinned raw URLs. No game source was edited, ported,
 or adapted to produce the build/run results below.

 ## 3. Build (plain C99, no extra flags)

 Compiler:

 ```
 $ /usr/bin/clang --version
 Apple clang version 16.0.0 (clang-1600.0.26.6)
 Target: arm64-apple-darwin27.2.0
 Thread model: posix
 ```

 Exact command (run from `external_projects/game_b/`):

 ```
 /usr/bin/clang -std=c99 -o 2048_build original/2048.c
 ```

 * Exit status: **0**
 * stdout: empty
 * stderr: empty (**no warnings, no errors**)
 * Produced binary: `file 2048_build` → `Mach-O 64-bit executable arm64`, 51592 bytes

 No extra flags were required: the source is self-contained and defines
 `_XOPEN_SOURCE 500` itself for `usleep`, and includes only standard C/POSIX headers
 (`stdio.h`, `stdlib.h`, `string.h`, `unistd.h`, `termios.h`, `stdbool.h`, `stdint.h`,
 `time.h`, `signal.h`). No third-party libraries (e.g. ncurses) are used.

 ## 4. Run results (all under `timeout 30`)

 The game ships a deterministic, non-interactive self-test mode (`test`) plus
 `--version` / `--help`, and supports stdin-driven play. Each command below was
 run from `external_projects/game_b/` with stdout/stderr captured to log files.

 ### 4.1 Deterministic self-test

 ```
 $ timeout 30 ./2048_build test
 ```
 Exit: **0**
 stdout:
 ```
 All 13 tests executed successfully
 ```
 stderr: empty

 ### 4.2 Version

 ```
 $ timeout 30 ./2048_build --version
 ```
 Exit: **0**
 stdout:
 ```
 2048.c version 1.0.3
 ```
 stderr: empty

 ### 4.3 Help

 ```
 $ timeout 30 ./2048_build --help
 ```
 Exit: **0**
 stdout:
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
 stderr: empty

 ### 4.4 Stdin-driven interactive session (clean quit)

 Input file `quit_input.txt` contains the two bytes `qy` (no trailing newline): `q`
 selects quit, `y` confirms. The board is randomly seeded from `time(NULL)`, so the
 rendered tile positions vary per run; the control flow is deterministic.

 ```
 $ printf 'qy' > quit_input.txt
 $ timeout 30 ./2048_build < quit_input.txt
 ```
 Exit: **0**
 stdout (ANSI escape codes present; key visible content): initial board with two
 `2` tiles rendered, control legend `←,↑,→,↓ or q`, the prompt `QUIT? (y/n)` appeared,
 then the terminal restore sequence `\033[?25h\033[m` (cursor visible / modes reset).
 No `Error! Cannot read keyboard input!` line was emitted (clean quit, not an EOF break).
 stderr: empty.

 Raw stdout/stderr for every run are saved in this directory as
 `run_test_stdout.log`, `run_test_stderr.log`, `run_version_stdout.log`,
 `run_version_stderr.log`, `run_help_stdout.log`, `run_help_stderr.log`,
 `run_game_stdin_stdout.log`, `run_game_stdin_stderr.log`; build streams are in
 `build_stdout.log` / `build_stderr.log`.

 ## 5. Exported symbols (`nm`)

 This is a standalone executable, not a shared library, so it has no dynamic export
 table. The closest analog is the set of global (`T`) symbols reported by `nm -g`;
 undefined (`U`) symbols are libc/POSIX imports.

 Command: `nm -g ./2048_build` (35 lines, reproduced verbatim):

 ```
                  U ___stack_chk_fail
                  U ___stack_chk_guard
 0000000100000000 T __mh_execute_header
 0000000100002ce0 T _addRandom
 0000000100002b6c T _countEmpty
 0000000100002170 T _drawBoard
                  U _exit
 0000000100002a8c T _findPairDown
 00000001000024ac T _findTarget
 0000000100002c18 T _gameEnded
 0000000100001fc8 T _getColors
 0000000100002120 T _getDigitCount
                  U _getchar
 0000000100002ebc T _initBoard
 0000000100003464 T _main
                  U _memcpy
 00000001000029cc T _moveDown
 000000010000296c T _moveLeft
 0000000100002a2c T _moveRight
 00000001000028d8 T _moveUp
                  U _printf
                  U _puts
                  U _rand
 0000000100002718 T _rotateBoard
 0000000100002f60 T _setBufferedInput
                  U _signal
 0000000100003428 T _signal_callback_handler
 0000000100002598 T _slideArray
                  U _srand
                  U _strcmp
                  U _tcgetattr
                  U _tcsetattr
 000000010000306c T _testSucceed
                  U _time
                  U _usleep
 ```

 Defined globals (game's own functions): `getColors`, `getDigitCount`, `drawBoard`,
 `findTarget`, `slideArray`, `rotateBoard`, `moveUp`, `moveLeft`, `moveDown`,
 `moveRight`, `findPairDown`, `countEmpty`, `gameEnded`, `addRandom`, `initBoard`,
 `setBufferedInput`, `testSucceed`, `signal_callback_handler`, `main` (plus the
 Mach-O `__mh_execute_header`). Imports (`U`): `printf`, `puts`, `getchar`, `rand`,
 `srand`, `strcmp`, `time`, `signal`, `exit`, `memcpy`, `usleep`, `tcgetattr`,
 `tcsetattr`, and the compiler stack-protector symbols `__stack_chk_fail` /
 `__stack_chk_guard`. This matches the unmodified upstream source's function set,
 confirming the ABI/symbol surface of the original was preserved.

 ## 6. Summary

 * Real permissively-licensed (MIT) public GitHub C game: **mevdschee/2048.c**.
 * Source + LICENSE preserved byte-for-byte with recorded SHA-256 hashes.
 * Built with plain `/usr/bin/clang -std=c99` (exit 0, zero warnings/errors).
 * Ran successfully under `timeout 30`: self-test passed (13/13), version and help
   printed, and an stdin-driven session quit cleanly — all exit 0.
 * Symbol list captured with `nm -g`.
 * Original source explicitly unchanged (verified by `cmp` against pinned upstream).
