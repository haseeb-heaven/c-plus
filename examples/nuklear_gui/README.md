# Real Nuklear GUI app in modern C+

A genuine interactive GUI application using the **real Nuklear UI library**
(vendored in `third_party/nuklear/`) with its official SDL3 renderer backend.
No hand-rolled fake rectangles: the window below has real buttons, a slider,
a checkbox and an editable text field.

![Real Nuklear GUI](screenshots/nuklear_gui.png)

## Source layout

- `nuklear_gui.cp` - modern C+ app: a `class` wraps the GUI state, and all
  widgets are driven from methods. Compiled with `--default` (not `--legacy-c`).
- `stdlib/gfx_nuklear.hp` - the C+ standard-library Nuklear binding. The app
  includes this facade, not a local wrapper header.
- `stdlib/nuklear/nuklear_gui_backend.c` / `nuklear_gui_api.h` - the stdlib
  plain-C ABI backend around SDL3 and Nuklear (`third_party/nuklear/`).

## Works with prebuilt C libraries

SDL3 and Nuklear are existing C libraries. The stdlib facade includes only the
plain-C ABI header, so the C+ analyser never parses third-party headers; the
backend objects are passed to the backend linker unchanged. This is the
supported pattern for using any already-compiled static (`.a`) or dynamic
(`.dylib`/`.so`) C library without changing its ABI. Including
`stdlib/gui.hp` directly would pull SDL3's headers into the C+ translation
unit and trip warnings-as-errors inside third-party code, so this example uses
the plain-C-ABI facade instead (same pattern as `examples/microui`).

## Run

```sh
sh examples/nuklear_gui/run.sh
# NUKLEAR_GUI_TEST 7 7 0
```

Without `GUI_PROOF`, the app opens a real 800x600 desktop window. Click the
buttons, drag the slider, edit the name; `Esc` or window close exits.

## Exact commands (from repository root)

Prerequisites: Nuklear is vendored in `third_party/nuklear/` with its official
SDL3 renderer; SDL3 comes from Homebrew. The C sources are compiled once.

```sh
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:$PATH
unset SDKROOT
C=build/c+
OUT=$(mktemp -d)

# 1) compile the vendored C implementation once:
/usr/bin/clang -std=c11 -O2 -Ithird_party/nuklear -I/opt/homebrew/include \
  -DNK_INCLUDE_STANDARD_VARARGS -c third_party/nuklear/nuklear_impl.c \
  -o "$OUT/nuklear_impl.o"

# 2) compile the stdlib ABI backend once:
/usr/bin/clang -std=c11 -O2 -I. -Ithird_party/nuklear -I/opt/homebrew/include \
  -c stdlib/nuklear/nuklear_gui_backend.c -o "$OUT/backend.o"

# 3) build the modern C+ app (stdlib/gfx_nuklear.hp):
$C build examples/nuklear_gui/nuklear_gui.cp --default --sanitize=none \
  -I. -Istdlib/nuklear -o "$OUT/app" -- "$OUT/nuklear_impl.o" "$OUT/backend.o" \
  -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3 \
  -framework Cocoa -framework OpenGL -framework GLUT \
  -framework CoreVideo -framework IOKit -framework QuartzCore

GUI_PROOF=1 "$OUT/app"   # deterministic framebuffer BMP
"$OUT/app"               # real interactive window
sh examples/nuklear_gui/run.sh   # NUKLEAR_GUI_TEST 7 7 0
```

The C+ source includes only `stdlib/gfx_nuklear.hp` (plain-C ABI header);
Nuklear/SDL3 headers stay in the C translation units.
