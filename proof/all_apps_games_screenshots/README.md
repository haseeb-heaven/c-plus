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
| 3D scene app (real, verified) | [games3d.svg](screenshots/games3d.svg) — `SCENE3D 1 4 0 13` | `apps/games3d/run.sh` |
| GUI app (real, verified) | [gui_app.svg](screenshots/gui_app.svg) — `GUI_APP 30 6 0 24` | `apps/gui_app/run.sh` |
| Calculator CLI app (real, verified) | [cli_app.svg](screenshots/cli_app.svg) — `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok` | `apps/cli_app/run.sh` |
| Goose-memory demos | [scoped_memory.svg](screenshots/scoped_memory.svg) | `examples/scoped_memory/run.sh` |
| CTL + C+ demos | [ctl_demo.svg](screenshots/ctl_demo.svg) | `examples/ctl_demo/run.sh` |
| Nuklear/Goose games | [nuklear.svg](screenshots/nuklear.svg) | `proof/games_gui_nuklear/run.sh` |
| Modern-C Goose ports | [modern_c_ports.svg](screenshots/modern_c_ports.svg) | `proof/modern_c_ports/run.sh` |
| Snake game board | [snake.svg](screenshots/snake.svg) | game-suite |
| Maze explorer | [maze.svg](screenshots/maze.svg) | game-suite |
| Nuklear UI frame | [nuklear_ui.svg](screenshots/nuklear_ui.svg) | game-suite |

The three moved apps have per-app proof pages:
[games3d.md](games3d.md), [gui_app.md](gui_app.md) and [cli_app.md](cli_app.md).
Their canonical sources/runners are in `apps/`; the `examples/` entries are
compatibility shims.

Honest scope: these are deterministic SVG renderings of transcripts and
headless game state, not pixel captures of native OS windows. This generator
uses the dummy/hidden SDL driver and does not claim an interactive display.
Separately, [proof/real_apps_cross_platform](../real_apps_cross_platform/README.md)
records genuine PE32+ runs under Highball/Wine (`CROSS_PROOF 58 58 0`); that
evidence is host/engine-specific. No fabricated output.
