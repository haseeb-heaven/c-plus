# Standard graphics libraries available with C+

C+ ships two real, vendored immediate-mode GUI libraries as first-class
standard library components. Both keep their upstream licenses; no upstream
source was modified except where noted.

| Library | C+ binding | Vendored source | License |
| --- | --- | --- | --- |
| **Nuklear** | `stdlib/gui.hp`, `stdlib/nuklear_adapt.hp`, `stdlib/nuklear_cpp.hp` | `third_party/nuklear/` (`nuklear.h`, `nuklear_impl.c`, official SDL3 renderer) | MIT / public domain — `third_party/nuklear/LICENSE` |
| **microui** | `stdlib/gfx_microui.hp` (`microui_gui` class) | `stdlib/microui/` (`microui.c/.h`, SDL3 backend, 6x6 font) | MIT — `stdlib/microui/LICENSE` |

## Using Nuklear from C+

```cpp
#include "cplus/cplus.h"
#include "stdlib/gui.hp"   // gui class + SDL3 renderer
```

Build `third_party/nuklear/nuklear_impl.c` once and link SDL3.
`examples/nuklear_gui/` is a complete working app with buttons, slider,
checkbox and editable text.

## Using microui from C+

```cpp
#include "cplus/cplus.h"
#include "stdlib/gfx_microui.hp"   // microui_gui class

int main(void) {
    microui_gui ui = microui_gui();
    if (!ui.open()) return 1;
    // poll(), frame(), capture(), drop()
    ui.drop();
    return 0;
}
```

Compile `stdlib/microui/microui.c` and
`stdlib/microui/microui_sdl3_backend.c` once and link SDL3. The C+ header
includes only the plain-C ABI header, so the safety analyser never has to
parse third-party UI headers. `examples/microui/` is a complete working
calculator + notes app.

## Guardrails

- Both libraries are linked as prebuilt C objects through a C ABI boundary;
  no standard C struct/union/pointer layout is changed.
- Licenses are kept verbatim next to the vendored sources.
- The GUI demos are real SDL3 windows with real framebuffer PNG proof
  (`examples/nuklear_gui/screenshots/`, `examples/microui/screenshots/`).
