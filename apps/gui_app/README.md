# gui_app — headless widget-core test (NOT a native GUI)

This is a **headless deterministic widget-core test**. It builds a widget
tree, runs layout and hit-testing, and prints a transcript with a CRC32. The
`render` mode creates a hidden SDL3 dummy-driver window for one frame only;
it is not an interactive GUI application.

Runner: `sh apps/gui_app/run.sh` (prints `GUI_APP ...`).

For **real interactive GUI apps**, see `apps/gui_widgets`,
`apps/memory_lab_gui` and `examples/raylib_demos/shapes2d.cp` / `text2d.cp`.
