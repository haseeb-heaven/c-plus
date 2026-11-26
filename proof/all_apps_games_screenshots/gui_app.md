# gui_app — headless GUI-command suite proof (text transcripts, NOT a native GUI screenshot)

Runner: `apps/gui_app/run.sh`. Generated from its actual stdout (bounded by timeout).

Honest scope: this suite exercises GUI-style commands and renders their
transcript to SVG. It is **not** a native window with widgets (textbox, list,
dropdown). For actual widget-layout rendering, see the Nuklear scratch adapter
proof at `proof/games_gui_nuklear/screenshots/nuklear_ui.svg` (also headless SVG).
Verified verdict: `GUI_APP 30 6 0 24`. App: [`apps/gui_app`](../../apps/gui_app/README.md).

![gui_app](../../games_gui_nuklear/screenshots/gui_app_real.png)

Transcript: [`screenshots/gui_app.out`](screenshots/gui_app.out).


The previous `gui_app.svg` was a CLI transcript rendering and is superseded by this real framebuffer image.
