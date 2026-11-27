# Proof screenshots — real GUI/3D vs CLI transcripts

This folder contains two clearly separated kinds of evidence. Do not treat the
CLI folder as GUI or 3D proof.

## Real GUI / 3D framebuffer captures (`real/`)

| App / game | Image | Runner |
|---|---|---|
| GUI widget app | [gui_widgets.png](real/gui_widgets.png) | `apps/gui_widgets/run.sh` |
| 3D wireframe scene | [games3d_real.png](real/games3d_real.png) | `apps/games3d_real/run.sh` |
| Interactive memory lab | [memory_lab.png](real/memory_lab.png) | `apps/memory_lab_gui/run.sh` |
| Sample games GUI | [sample_games.png](real/sample_games.png) | `apps/sample_games_gui/run.sh` |
| Nuklear widget frame | [gui_widgets.png](real/gui_widgets.png) | `proof/games_gui_nuklear/run.sh` |

These are PNG images captured from an SDL3 renderer framebuffer (real windows,
widgets and projected geometry). They are not ASCII art and not CLI transcript
renderings. Headless verification uses `SDL_VIDEODRIVER=dummy`; the renderer and
framebuffer are still real.

## CLI transcript renderings (`cli_transcripts/`, NOT GUI/3D)

The SVG files under `cli_transcripts/` are transcript renderings for `webserver`,
`sqldb`, `gameeng`, `cli_app`, `ctl_demo`, `scoped_ports`, etc. They prove stdout
behavior only. They are explicitly **not** GUI screenshots and **not** 3D game
renders.

Do not cite them as GUI or graphics evidence.

## How the real captures are made

Each PNG is converted from a BMP written by the app itself with
`SDL_RenderReadPixels(renderer, NULL)` and `SDL_SaveBMP_IO`. No CLI stdout,
no SVG transcript and no desktop screenshot is used. Captures are made with a
real SDL3 window/renderer (headless runs use `SDL_VIDEODRIVER=dummy`); the
framebuffer content is genuine rendered geometry/widgets.

| Image | SHA-256 |
|---|---|
| games3d_real.png | `51fcb2d269813185786280171acaa19fbe739f0c84c1e7a64290e04785578985` |
| gui_widgets.png | `4a5047ae3d500172bf199ff5cbc7db992ae3e74c6531596de9d8aa3ab3ff4299` |
| memory_lab.png | `61d26a21176f3619a4daceeef8846ddd04dab496a93a162317f096e8e454cecf` |
| sample_games.png | `a131836e1673d6183a6d6edaee57e834f3e85c322e19c24d955ab47fbe7e0ce0` |
