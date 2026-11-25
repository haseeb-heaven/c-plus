# Apps (real programs, not demos)

| App | Library proof | Runner |
|---|---|---|
| Web server | `stdlib/web.hp` (heap-free stack server) | `apps/webserver/run.sh` |
| SQL-like database | `stdlib/sqldb.hp` | `apps/sqldb/run.sh` |
| Headless game engine | `stdlib/gameeng.hp` | `apps/gameeng/run.sh` |

Related game/GUI suites live in `examples/{games3d,gui_app,cli_app}` and
`proof/games_gui_nuklear`. Deterministic screenshots for all of them are in
`proof/all_apps_games_screenshots/`.
