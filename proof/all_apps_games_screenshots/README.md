# Screenshots of all apps and games (generated from real runner output)

`sh proof/all_apps_games_screenshots/generate.sh` runs each app/game proof
under a bounded timeout and renders its **actual stdout** into an SVG card.
Each app also has a per-app `.md` page with an embedded screenshot and a link
to the raw transcript. The three game SVGs from
`proof/games_gui_nuklear/screenshots/` are copied here unchanged.

| App / game | Screenshot | Proof runner |
|---|---|---|
| Web server (curl-verified) | [webserver.svg](screenshots/webserver.svg) | `apps/webserver/run.sh` |
| SQL-like database app | [sqldb.svg](screenshots/sqldb.svg) | `apps/sqldb/run.sh` |
| Headless game engine | [gameeng.svg](screenshots/gameeng.svg) | `apps/gameeng/run.sh` |
| 3D games suite | [games3d.svg](screenshots/games3d.svg) | `examples/games3d/run.sh` |
| GUI app suite | [gui_app.svg](screenshots/gui_app.svg) | `examples/gui_app/run.sh` |
| CLI app suite | [cli_app.svg](screenshots/cli_app.svg) | `examples/cli_app/run.sh` |
| Goose-memory demos | [goose_memory.svg](screenshots/goose_memory.svg) | `examples/goose_memory/run.sh` |
| CTL + C+ demos | [ctl_demo.svg](screenshots/ctl_demo.svg) | `examples/ctl_demo/run.sh` |
| Nuklear/Goose games | [nuklear.svg](screenshots/nuklear.svg) | `proof/games_gui_nuklear/run.sh` |
| Modern-C Goose ports | [goose_ports.svg](screenshots/goose_ports.svg) | `proof/goose_samples/run.sh` |
| Snake game board | [snake.svg](screenshots/snake.svg) | game-suite |
| Maze explorer | [maze.svg](screenshots/maze.svg) | game-suite |
| Nuklear UI frame | [nuklear_ui.svg](screenshots/nuklear_ui.svg) | game-suite |

Honest scope: these are deterministic SVG renderings of transcripts and
headless game state, not pixel captures of native OS windows (GUI runtime is
conservatively reported as BLOCKED in verbose mode). No fabricated output.
