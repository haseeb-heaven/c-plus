# Proof screenshots — real GUI/3D vs CLI transcripts

This folder contains two clearly separated kinds of evidence. Do not treat the
CLI folder as GUI or 3D proof.

## Real GUI / 3D framebuffer captures (`real/`)

| App / game | Image | Runner |
|---|---|---|
| GUI widget app | [gui_app_real.png](real/gui_app_real.png) | `apps/gui_widgets/run.sh` |
| 3D wireframe scene | [games3d_real.png](real/games3d_real.png) | `apps/games3d_real/run.sh` |
| Interactive memory lab | [memory_lab.png](real/memory_lab.png) | `apps/memory_lab_gui/run.sh` |
| Nuklear widget frame | [gui_widgets.png](real/gui_widgets.png) | `proof/games_gui_nuklear/run.sh` |

These are PNG images captured from an SDL3 renderer framebuffer (real windows,
widgets and projected geometry). They are not ASCII art and not CLI transcript
renderings. Headless verification uses `SDL_VIDEODRIVER=dummy`; the renderer and
framebuffer are still real.

## CLI transcript renderings (`cli_transcripts/`, NOT GUI/3D)

The SVG files under `cli_transcripts/` are transcript renderings for `webserver`,
`sqldb`, `gameeng`, `cli_app`, `ctl_demo`, `goose_ports`, etc. They prove stdout
behavior only. They are explicitly **not** GUI screenshots and **not** 3D game
renders.

Do not cite them as GUI or graphics evidence.
