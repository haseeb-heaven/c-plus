# Real-world C+ examples (game engine, games, desktop/web/server, database)

`apps/legacy_examples/real_world/` contains deterministic, runnable C+ examples showing
the modern library layer in realistic shapes:

| Program | Demonstrates | Safety contract shown |
|---|---|---|
| `game_engine.cp` | allocator-aware ECS storage (arena-backed vector), spawn/damage/tick | invalid spawn coords/HP, bad entity, negative damage → refuse (-1), state intact |
| `snake_game.cp` | deterministic board-step logic for a playable game | wall collision refuses (-1) |
| `desktop_app.cp` | desktop menu/form state machine with `string` | out-of-range menu selection refuses |
| `web_app.cp` | HTTP-style route matching (`/`, `/health`, 404) | oversized/empty path refuses |
| `server_app.cp` | bounded request queue (`vector` + capacity) | full queue / negative id refuses |
| `database_engine.cp` | in-memory table (`intmap` + `vector`), CRUD + sum | duplicate key and out-of-range value refuse |

`generate.sh` recreates the deterministic sources; `run_all.sh` builds all six
with `--default --sanitize=none`, requires exit 0 and exact output, then runs
100 hostile-input variants end-to-end:

```
REAL_WORLD_EXAMPLES built=6 pass=6 fail=0 hostile_variants=100 clean=100
```

These are teaching models, not production servers/games/DBs. Refusals are
runtime evidence via `error*`, not a proof of full memory safety.
