# Screenshots folder: copied PNGs and CLI SVG renderings

This folder holds two different kinds of artifacts. They are labeled here so
that neither is mistaken for the other, and neither is cited as something it
is not.

## PNGs are copies of the canonical real captures

The four PNGs here — [games3d_real.png](games3d_real.png),
[gui_widgets.png](gui_widgets.png), [memory_lab.png](memory_lab.png) and
[sample_games.png](sample_games.png) — are **byte-for-byte copies**, not new or
independent captures. The canonical SDL3 framebuffer captures live in
[`proof/all_apps_games_screenshots/real/`](../../all_apps_games_screenshots/real/)
and are documented in the
[canonical README](../../all_apps_games_screenshots/README.md). Cite that
canonical path, not these copies.

Each copy has the same SHA-256 as the canonical file of the same name:

| Copy in this folder | Canonical file | SHA-256 |
|---|---|---|
| [games3d_real.png](games3d_real.png) | [real/games3d_real.png](../../all_apps_games_screenshots/real/games3d_real.png) | `51fcb2d269813185786280171acaa19fbe739f0c84c1e7a64290e04785578985` |
| [gui_widgets.png](gui_widgets.png) | [real/gui_widgets.png](../../all_apps_games_screenshots/real/gui_widgets.png) | `4a5047ae3d500172bf199ff5cbc7db992ae3e74c6531596de9d8aa3ab3ff4299` |
| [memory_lab.png](memory_lab.png) | [real/memory_lab.png](../../all_apps_games_screenshots/real/memory_lab.png) | `61d26a21176f3619a4daceeef8846ddd04dab496a93a162317f096e8e454cecf` |
| [sample_games.png](sample_games.png) | [real/sample_games.png](../../all_apps_games_screenshots/real/sample_games.png) | `a131836e1673d6183a6d6edaee57e834f3e85c322e19c24d955ab47fbe7e0ce0` |

The `.bmp` files beside them are raw framebuffer exports with the same pixel
content as the corresponding PNGs; they are not separate evidence either.

## SVGs are CLI renderings, not GUI screenshots

[snake.svg](snake.svg), [maze.svg](maze.svg) and [nuklear_ui.svg](nuklear_ui.svg)
are deterministic CLI transcript renderings produced by `game_suite.cp` (see
[`../README.md`](../README.md) and [`../run.sh`](../run.sh)). They prove stdout
behavior only. They are **not** GUI screenshots, **not** SDL3 framebuffer
captures and **not** 3D renders. Do not cite them as GUI or graphics evidence.
