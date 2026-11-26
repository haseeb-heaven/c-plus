# Proof folders (index)

- `games_apps/` — original evidence for the apps now in `apps/` (`PROOF_GAMES 10 0`)
- `real_apps_cross_platform/` — Mach-O + PE32+ binaries (`CROSS_PROOF 58 58 0`)
- `modern_c_ports/` — 16 independent modern-C the inspiration language workloads + binaries (`PORTS_MODERN_C 16 16 0`)
- `games_gui_nuklear/` — snake, maze and Nuklear UI with SVG screenshots (`GAME_SUITE 4 4 0`)
- `all_apps_games_screenshots/` — screenshots and per-app `.md` pages for every suite (`SCREENSHOTS 24 24 0`)

## Verified apps (moved from `examples/` into `apps/`)

These are real verified applications, not demos. The old `examples/.../run.sh`
entries are compatibility shims; the canonical sources and runners are in
`apps/`.

| App | Runner | Verified verdict | Screenshot | App README |
| --- | --- | --- | --- | --- |
| 3D scene app (`games3d`) | `apps/games3d/run.sh` | `SCENE3D 1 4 0 13` | [games3d.svg](all_apps_games_screenshots/cli_transcripts/games3d.svg) | [apps/games3d](../apps/games3d/README.md) |
| GUI app (`gui_app`) | `apps/gui_app/run.sh` | `GUI_APP 30 6 0 24` | [gui_app.svg](all_apps_games_screenshots/cli_transcripts/gui_app.svg) | [apps/gui_app](../apps/gui_app/README.md) |
| Calculator CLI (`cli_app`) | `apps/cli_app/run.sh` | `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok` | [cli_app.svg](all_apps_games_screenshots/cli_transcripts/cli_app.svg) | [apps/cli_app](../apps/cli_app/README.md) |

The macOS arm64 + Windows PE32+ capture for these and five other programs is
[real_apps_cross_platform/](real_apps_cross_platform/README.md)
(`CROSS_PROOF 58 58 0`).

Generated binaries/logs are build artifacts; canonical sources and golden
outputs are committed beside their runners.
