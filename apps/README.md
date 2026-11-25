# Apps (real programs, not demos)

Verified C+ applications live here. Each runner is bounded and prints an exact
final verdict. The former `apps/legacy_examples/{games3d,gui_app,cli_app}` directories now
contain compatibility shims only; the real apps are below.

| App | Runner | Verified verdict | Screenshot | Evidence |
| --- | --- | --- | --- | --- |
| Web server (`stdlib/web.hp`) | `apps/webserver/run.sh` | included in `APPS 3 3 0` (aggregate) | [webserver.svg](../proof/all_apps_games_screenshots/screenshots/webserver.svg) | [proof page](../proof/all_apps_games_screenshots/webserver.md) |
| SQL-like database (`stdlib/sqldb.hp`) | `apps/sqldb/run.sh` | included in `APPS 3 3 0` (aggregate) | [sqldb.svg](../proof/all_apps_games_screenshots/screenshots/sqldb.svg) | [proof page](../proof/all_apps_games_screenshots/sqldb.md) |
| Headless game engine (`stdlib/gameeng.hp`) | `apps/gameeng/run.sh` | included in `APPS 3 3 0` (aggregate) | [gameeng.svg](../proof/all_apps_games_screenshots/screenshots/gameeng.svg) | [proof page](../proof/all_apps_games_screenshots/gameeng.md) |
| 3D scene app (`games3d`) | `apps/games3d/run.sh` | `SCENE3D 1 4 0 13` | [games3d.svg](../proof/all_apps_games_screenshots/screenshots/games3d.svg) | [app README](games3d/README.md) · [proof](../proof/all_apps_games_screenshots/games3d.md) |
| GUI app (`gui_app`) | `apps/gui_app/run.sh` | `GUI_APP 30 6 0 24` | [gui_app.svg](../proof/all_apps_games_screenshots/screenshots/gui_app.svg) | [app README](gui_app/README.md) · [proof](../proof/all_apps_games_screenshots/gui_app.md) |
| Calculator CLI (`cli_app`) | `apps/cli_app/run.sh` | `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok` | [cli_app.svg](../proof/all_apps_games_screenshots/screenshots/cli_app.svg) | [app README](cli_app/README.md) · [proof](../proof/all_apps_games_screenshots/cli_app.md) |

The first three runners are verified together by `sh apps/run_all.sh` →
`APPS 3 3 0` (3 apps, 3 passed, 0 failed). The other three are verified by
their own runners; see [proof/real_apps_cross_platform](../proof/real_apps_cross_platform/README.md)
(`CROSS_PROOF 58 58 0`) for the separate macOS arm64 + Windows PE32+ capture.

Related real game/UI work lives in [proof/games_gui_nuklear](../proof/games_gui_nuklear/README.md)
(`GAME_SUITE 4 4 0`) and modern-C ports in
[proof/modern_c_ports](../proof/modern_c_ports/README.md) (`PORTS_MODERN_C 16 16 0`).
All deterministic screenshots are indexed in
[proof/all_apps_games_screenshots](../proof/all_apps_games_screenshots/README.md).
