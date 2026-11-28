# EDGE100 examples — program manifest

Scope: the 123 example programs present under `examples/` when this task began
(`90 .cp`, `33 .c`). Four additional example files appeared later from parallel
agents (`apps/cli_app/cli_calc.cp`, `apps/games3d/scene3d_demo.cp`,
`apps/gui_app/gui_app.cp`, `apps/gui_app/win_portability_probe.c`) and are
deliberately outside this locked scope.

Every row below is built by `run.sh` from the repo root with the listed flags
and run under `timeout 30` against the edge-case battery. `INTER` marks the five
interactive programs that receive their documented quit key on stdin; all others
receive empty/huge/hostile stdin and hostile argv. The `EDGE` contract column is
the observable oracle: byte-exact baseline output, an explicit refusal token, or
an explicit completion sentinel.

| # | program | lang | build flags (after required base flags) | class | oracle |
|---:|---|---|---|---|---|
| 1 | examples/apps/cgame.c | c | (base only) | INTER | baseline/refusal/sentinel |
| 2 | examples/apps/student.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 3 | examples/crt/calloc_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 4 | examples/crt/fclose_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 5 | examples/crt/fopen_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 6 | examples/crt/fprintf_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 7 | examples/crt/fread_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 8 | examples/crt/free_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 9 | examples/crt/fscanf_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 10 | examples/crt/fwrite_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 11 | examples/crt/malloc_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 12 | examples/crt/memcmp_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 13 | examples/crt/memcpy_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 14 | examples/crt/memmove_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 15 | examples/crt/memset_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 16 | examples/crt/memzero_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 17 | examples/crt/printf_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 18 | examples/crt/realloc_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 19 | examples/crt/scanf_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 20 | examples/crt/snprintf_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 21 | examples/crt/sprintf_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 22 | examples/crt/strcat_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 23 | examples/crt/strcmp_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 24 | examples/crt/strcpy_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 25 | examples/crt/strncat_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 26 | examples/crt/strncpy_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 27 | examples/crt/strnlen_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 28 | examples/crt/vsnprintf_s.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 29 | examples/expanded/apps_2/csv_tool.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 30 | examples/expanded/apps_2/json_query.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 31 | examples/expanded/apps_2/matrix.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 32 | examples/expanded/apps_2/vault.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 33 | examples/expanded/cli/_probe.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 34 | examples/expanded/cli/_probe2.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 35 | examples/expanded/cli/calculator.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 36 | examples/expanded/cli/file_tools.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 37 | examples/expanded/cli/text_tools.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 38 | examples/expanded/cli/todo.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 39 | examples/expanded/data_2/hash_tool.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 40 | examples/expanded/data_2/regex_lite.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 41 | examples/expanded/data_2/rle.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 42 | examples/expanded/data_2/sort_tool.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 43 | examples/expanded/database/kv_store.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 44 | examples/expanded/database/safety_demo.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 45 | examples/expanded/database/table_query.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 46 | examples/expanded/engine/ecs.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 47 | examples/expanded/engine/engine_loop.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 48 | examples/expanded/engine/resource_cache.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 49 | examples/expanded/games/cores/game2048.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 50 | examples/expanded/games/cores/maze.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 51 | examples/expanded/games/cores/tictactoe.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 52 | examples/expanded/games/snake/snake_cli.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 53 | examples/expanded/games/snake/snake_core.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 54 | examples/expanded/games_2/breakout.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 55 | examples/expanded/games_2/invaders.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 56 | examples/expanded/games_2/pong.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 57 | examples/expanded/games_3/minesweeper.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 58 | examples/expanded/games_3/nim.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 59 | examples/expanded/games_3/sudoku.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 60 | examples/expanded/games_4/checkers.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 61 | examples/expanded/games_4/chesslite.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 62 | examples/expanded/games_4/connect4.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 63 | examples/expanded/math_2/base64.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 64 | examples/expanded/math_2/bigint.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 65 | examples/expanded/math_2/checksum.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 66 | examples/expanded/math_2/prng.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 67 | examples/expanded/safety/bounds_demo.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 68 | examples/expanded/safety/lifetime_demo.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 69 | examples/expanded/safety/memcpy_s_demo.cp | cp |  | post:-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a | STDIN/ARGV | baseline/refusal/sentinel |
| 70 | examples/expanded/safety/sync_demo.c | c | -Istdlib/safe build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 71 | examples/expanded/systems_2/event_bus.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 72 | examples/expanded/systems_2/lru_cache.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 73 | examples/expanded/systems_2/ring_buffer.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 74 | examples/expanded/systems_2/scheduler.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 75 | examples/games/2048.c | c | (base only) | INTER | baseline/refusal/sentinel |
| 76 | examples/games/snake.c | c | (base only) | INTER | baseline/refusal/sentinel |
| 77 | examples/games/tetris.c | c | -lncurses | INTER | baseline/refusal/sentinel |
| 78 | examples/games/tiles.c | c | (base only) | INTER | baseline/refusal/sentinel |
| 79 | examples/modern_gui/dashboard/dashboard.cp | cp | -I/opt/homebrew/include -Ithird_party/nuklear -L/opt/homebrew/lib -Lbuild/ext -lSDL3 -lnuklear -Wl,-rpath,/opt/homebrew/lib | STDIN/ARGV | baseline/refusal/sentinel |
| 80 | examples/modern_gui/games/breakout_ui/breakout_ui.cp | cp | -I/opt/homebrew/include -Ithird_party/nuklear -L/opt/homebrew/lib -Lbuild/ext -lSDL3 -lnuklear -Wl,-rpath,/opt/homebrew/lib | STDIN/ARGV | baseline/refusal/sentinel |
| 81 | examples/modern_gui/games/invaders_ui/invaders_ui.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 82 | examples/modern_gui/settings/settings_panel.cp | cp | -I/opt/homebrew/include -Ithird_party/nuklear -L/opt/homebrew/lib -Lbuild/ext -lSDL3 -lnuklear -Wl,-rpath,/opt/homebrew/lib | STDIN/ARGV | baseline/refusal/sentinel |
| 83 | examples/real_world/database_engine.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 84 | examples/real_world/desktop_app.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 85 | examples/real_world/game_engine.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 86 | examples/real_world/server_app.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 87 | examples/real_world/snake_game.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 88 | examples/real_world/web_app.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 89 | examples/secure_apps/auditlog/auditlog.c | c | -D_POSIX_C_SOURCE=200809L -Ilibc/install-include build/libc/libcplus_hardened.a -pthread -lm | STDIN/ARGV | baseline/refusal/sentinel |
| 90 | examples/secure_apps/firewall/firewall.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 91 | examples/secure_apps/ratelimit/ratelimit.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 92 | examples/secure_apps/vault/vault.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 93 | examples/system/macpulse.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 94 | examples/utilities/data_tools/csvstat.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 95 | examples/utilities/data_tools/iniparse.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 96 | examples/utilities/data_tools/jsonfmt.cp | cp | -Ithird_party/yyjson -Lbuild/ext -lyyjson | STDIN/ARGV | baseline/refusal/sentinel |
| 97 | examples/utilities/data_tools/logparse.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 98 | examples/utilities/data_tools/uriparse.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 99 | examples/utilities/dev_tools/envfilter.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 100 | examples/utilities/dev_tools/hexdump.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 101 | examples/utilities/dev_tools/numconv.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 102 | examples/utilities/dev_tools/sha_lite.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 103 | examples/utilities/dev_tools/timeconv.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 104 | examples/utilities/file_tools/fcat.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 105 | examples/utilities/file_tools/fcopy.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 106 | examples/utilities/file_tools/fdiff.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 107 | examples/utilities/file_tools/fhash.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 108 | examples/utilities/file_tools/fsize.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 109 | examples/utilities/net_tools/emailcheck.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 110 | examples/utilities/net_tools/httpline.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 111 | examples/utilities/net_tools/ipcheck.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 112 | examples/utilities/net_tools/portcheck.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 113 | examples/utilities/net_tools/urlcheck.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 114 | examples/utilities/security_tools/bounds_check.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 115 | examples/utilities/security_tools/checksum.cp | cp |  | post:-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a | STDIN/ARGV | baseline/refusal/sentinel |
| 116 | examples/utilities/security_tools/pwdcheck.cp | cp |  | post:-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a | STDIN/ARGV | baseline/refusal/sentinel |
| 117 | examples/utilities/security_tools/safe_copy.cp | cp |  | post:-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a | STDIN/ARGV | baseline/refusal/sentinel |
| 118 | examples/utilities/security_tools/safe_token.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 119 | examples/utilities/text_tools/tbase64.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 120 | examples/utilities/text_tools/tgrep.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 121 | examples/utilities/text_tools/thead.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 122 | examples/utilities/text_tools/ttr.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |
| 123 | examples/utilities/text_tools/twc.cp | cp | (base only) | STDIN/ARGV | baseline/refusal/sentinel |

Required base flags (prepended by `run.sh`):

- `.cp`: `./build/c+ build <source> --default --sanitize=none -I. [flags] -o <bin> [post]`
- `.c`: `/usr/bin/clang -std=c11 -O1 -I. <source> [flags] -o <bin>`

Post-build arguments (`post`) are the safecrt/hardened-libc archives passed after
`--` to the C+ driver. SDL3/Nuklear, yyjson, curl and libuv flags are supplied
where the program's headers require them.

Interactive quit inputs: `apps/cgame.c` = `q\n`, `games/snake.c` = `x`,
`games/tetris.c` = `q`, `games/tiles.c` = `q`, `games/2048.c` = `q`.

Determinism exception: `examples/games/snake.c` seeds its fruit with
`srand(time(NULL))`; its oracle is the final `Game Over! Your Score:` line, not
the full board. All other built programs were byte-identical across two runs.
