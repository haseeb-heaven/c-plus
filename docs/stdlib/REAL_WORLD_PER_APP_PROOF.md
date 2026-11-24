# Per-app verification proof (real-world examples)

Each of the six apps was built and run **individually** with an explicit
command, bounded timeout, exit-code check, stdout capture and stderr check.
Host: macOS arm64, Apple clang 16, C+ 0.1.0, `--default --sanitize=none -I.`.

| App | Build cmd | Build rc / diagnostics | Run rc | Exact stdout | stderr | 10x distinct outputs |
|---|---|---:|---:|---|---:|---:|
| game_engine | `./build/c+ build examples/real_world/game_engine.cp --default --sanitize=none -I. -o <bin>` | 0 / 0 bytes | 0 | `GAME_ENGINE 1 70 1 -1 -1 -1` | 0 | 1 |
| snake_game | same pattern | 0 / 0 bytes | 0 | `SNAKE_GAME 28 -1` | 0 | 1 |
| desktop_app | same pattern | 0 / 0 bytes | 0 | `DESKTOP_APP 8 2 -1` | 0 | 1 |
| web_app | same pattern | 0 / 0 bytes | 0 | `WEB_APP 200 204 404 404` | 0 | 1 |
| server_app | same pattern | 0 / 0 bytes | 0 | `SERVER_APP 0 0 2 -1` | 0 | 1 |
| database_engine | same pattern | 0 / 0 bytes | 0 | `DATABASE_ENGINE 0 0 -1 -1 30` | 0 | 1 |

Interpretation of the refusal tokens (`-1`):
- game_engine: bad spawn coords (2000), nonexistent entity (999), negative damage → all refused.
- snake_game: wall collision → refused.
- desktop_app: out-of-range menu selection (9 of 4) → refused.
- web_app: oversized/empty path → refused (404), valid routes 200/204.
- server_app: negative request id → refused; queue holds 2 valid.
- database_engine: duplicate key and out-of-range value → refused; sum 30 intact.

Hostile stdin per app: `printf 'HOSTILE_INPUT_999999\n' | <bin>` → in every
case exit 0, identical stdout to the clean run, 0 stderr bytes — the
deterministic cores ignore untyped hostile stdin (they take no interactive
input), so no crash/corruption. 100-variant aggregate (`run_all.sh`):
`built=6 pass=6 fail=0 hostile_variants=100 clean=100`.

Scope: runtime refusals on these fixed inputs, not a proof of full safety.
