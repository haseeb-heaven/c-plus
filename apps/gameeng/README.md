# gameeng — headless game-engine logic test (NOT a GUI/3D app)

This is a **headless deterministic logic test** that aggregates the
webserver, sqldb and gameeng runners. It has no window, no GPU and no
interactive gameplay.

Runner: `sh apps/gameeng/run.sh` (prints `APPS 3 3 0`).

For real GUI/3D apps, see `examples/raylib_demos/` and the SDL3 apps under
`apps/` (`games3d_real`, `gui_widgets`, `memory_lab_gui`, `sample_games_gui`).
