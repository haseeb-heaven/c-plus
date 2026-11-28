# Game/app proof pack — C+ modern + safer libs with SVG screenshots

`sh proof/games_gui_nuklear/run.sh` rebuilds `game_suite.cp` with the C+ driver
and verifies stdout plus **three SVG screenshots** byte-for-byte against
`expected/`. Result on this host: `GAME_SUITE 4 4 0`.

| App/game | Library evidence | Artifact |
|---|---|---|
| Snake (8 scripted steps, score 9) | `stdlib/sqldb.hp` high-score row | `screenshots/snake.svg` |
| Maze explorer (2 frames, pos 2,1) | `stdlib/gameeng.hp` world/step | `screenshots/maze.svg` |
| Nuklear UI frame (12 widgets, 384 scratch bytes, one-store rewind) | `stdlib/nuklear_adapt.hp` + VM data stack | `screenshots/nuklear_ui.svg` |

Sources: `game_suite.cp`. Binaries: `bin/game_suite` (committed beside source).
Transcript: `bin/stdout.txt`, golden copy in `expected/stdout.txt`.

**Honest scope:** these are deterministic headless renderings to SVG, not
native GUI window screenshots; no raylib/SDL/Allegro/bgfx/GTK/LVGL code is
included. The Nuklear context is not kept across scope rewind. See
`docs/stdlib/GAME_GUI_UPSTREAM_ADOPTION.md` for the 8-library plan and
non-claims.
