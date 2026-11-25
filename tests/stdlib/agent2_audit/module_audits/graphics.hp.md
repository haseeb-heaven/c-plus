# Safety audit — `stdlib/graphics.hp` (2D rendering via SDL3)

> **LABEL: STATIC SOURCE INSPECTION ONLY — NOT EXECUTED.** No test runner, compiler, or
> host process was run to produce this document. All statements derive from reading
> `stdlib/graphics.hp` and the static inventory `docs/stdlib/REGRESSION_INVENTORY.md`. No
> behavior is claimed to be verified by execution. See §7.

## 1. Provenance and method

- Header inspected: `stdlib/graphics.hp` (242 lines). Scope comment at
  `stdlib/graphics.hp:1-6`; includes at `stdlib/graphics.hp:10-13` (SDL3 at line 10,
  `stdlib/window.hp` at line 13).
- Exact counts/unexercised names from `docs/stdlib/REGRESSION_INVENTORY.md`:
  - §3 total **16 public callable entries** (`docs/stdlib/REGRESSION_INVENTORY.md:46,56`).
  - §4 exercised **7 of 16** (`docs/stdlib/REGRESSION_INVENTORY.md:75,85`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:118`.
  - §5 not exercised **9 of 16** (`docs/stdlib/REGRESSION_INVENTORY.md:150,160`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:189`.
- Static, not execution — `docs/stdlib/REGRESSION_INVENTORY.md:6-15,232-234`.

## 2. Public API list with counts

The inventory's 16 entries, with header line numbers:

| Class | Entries (header lines) | Count |
|---|---|---|
| `surface` | `surface` ctor `:18-20`; `valid` `:22-24`; `width` `:26-28`; `height` `:30-32`; `pixel` `:35-100`; `drop` `:102-107` | 6 |
| `renderer` | `renderer` ctor `:114-126`; `valid` `:128-130`; `native` `:132-134`; (set_color omitted — below); `clear` `:150-161`; `fill_rect` `:163-179`; `line` `:181-192`; `present` `:194-205`; `set_vsync` `:207-218`; `read` `:221-232`; `drop` `:234-239` | 10 |
| **Total** | | **16** |

**Parser omission found by source inspection:** `renderer.set_color(unsigned char r,
unsigned char g, unsigned char b, unsigned char a, error *)` at
`stdlib/graphics.hp:136-148` (signature spans lines 136-137, no `(` on the final line).
Source inspection therefore sees **17 public callables**, while the audited headline count
is the inventory's **16**.

## 3. Public surface safety analysis (STATIC — source inspection only)

- **`surface`:** constructor wraps a borrowed `SDL_Surface *` without taking ownership
  semantics explicitly in its signature (`:18-20`); `drop` calls `SDL_DestroySurface`
  (`:102-107`). `width`/`height` are NULL-guarded (`:26-32`). **No copy/move control** is
  declared; a shallow copy would double-free in `drop()`.
- **`surface.pixel` — the most bounds-sensitive routine:** validates surface/out pointers
  (`:44-47`), checks `0 <= x < w`, `0 <= y < h` (`:48-51`), requires pixel format with
  `bpp >= 32` (`:52-58`), checks `pixels != NULL` (`:59-63`), and checks
  `pitch > 0` and `(size_t)x * bytes_per_pixel + 4 <= pitch` (`:64-70`). It then locks the
  surface when `SDL_MUSTLOCK` (`:71-88`), reads 4 bytes at
  `pixels + y*pitch + x*bytes_per_pixel` (`:89-95`), and unlocks on success. Static
  observations: the `bpp >= 32` check allows formats wider than 4 bytes while the code
  always reads exactly 4 bytes (`:64,92-95`) — for >32 bpp the channel order/byte layout
  is not validated; the row check adds a fixed `+4` rather than `bytes_per_pixel`
  (`:66`), so a >4-byte format could read past the row boundary. Lock/unlock is not
  exception-safe (`out` writes cannot throw in C+, so this is a structural note only).
  **This function is unexercised** (§5), so none of these checks are deep-tested.
- **`renderer`:** constructor takes `SDL_Window *` and driver name, checks NULL window and
  maps `SDL_CreateRenderer` failure to `CPLUS_EXIT_RESOURCE` with `SDL_GetError()`
  (`:114-126`). `native` returns the borrowed renderer pointer (`:132-134`). All draw
  methods validate `handle != NULL` and propagate `SDL_*` failure as `CPLUS_EXIT_IO` with
  `SDL_GetError()`: `set_color` (`:136-148`), `clear` (`:150-161`), `fill_rect` (`:163-179`),
  `line` (`:181-192`), `present` (`:194-205`), `set_vsync` (`:207-218`). `fill_rect` copies
  caller floats into `SDL_FRect` with no finiteness or range check (`:170-173`).
- **`set_vsync(int enabled, ...)`:** passes `enabled` to `SDL_SetRenderVSync` as-is
  (`:207-218`); no validation that it is a supported mode — left to SDL.
- **`renderer.read`:** allocates `surface shot = surface(NULL)`, calls
  `SDL_RenderReadPixels(handle, NULL)`, and returns it; failure is `CPLUS_EXIT_IO`
  (`:220-232`). The returned `surface` is owned and must be dropped by the caller;
  there is no null-ownership marker other than `valid()`.
- **Lifetime ordering:** `renderer` borrows an `SDL_Window *`; `graphics.hp` includes
  `stdlib/window.hp` (`:13`), whose `sdl`/`window` RAII types own the SDL init/window. If
  the window is dropped before the renderer, the renderer holds a stale native pointer;
  the header does not enforce order.

All above is read from source; none of it has been exercised here.

## 4. External dependency requirements

- **SDL3** (zlib licence): `#include <SDL3/SDL.h>` at `stdlib/graphics.hp:10`; "Link with
  pkg-config sdl3" at `stdlib/graphics.hp:5`. Uses `SDL_Surface`, `SDL_Renderer`,
  `SDL_Window`, `SDL_GetMasksForPixelFormat`, `SDL_MUSTLOCK`, `SDL_LockSurface`,
  `SDL_UnlockSurface`, `SDL_GetError`, `SDL_DestroySurface`, `SDL_CreateRenderer`,
  `SDL_SetRenderDrawColor`, `SDL_RenderClear`, `SDL_RenderFillRect`, `SDL_RenderLine`,
  `SDL_RenderPresent`, `SDL_SetRenderVSync`, `SDL_RenderReadPixels`, `SDL_DestroyRenderer`.
- **`stdlib/window.hp`** at `stdlib/graphics.hp:13` — which itself includes SDL3 and owns
  `sdl` (SDL_Init/Quit) and `window` (SDL_Window) lifetimes. A graphics test must set up a
  valid `window` before constructing a `renderer` (`stdlib/graphics.hp:114,118-121`).
- **C+ runtime / `cplus/cplus.h`** and `stdlib/error.hp` at `stdlib/graphics.hp:11-12`.
- **Not a dependency of this module:** libuv, libcurl, stb, ffmpeg, miniaudio. ffmpeg is
  `stdlib/video.hp`; miniaudio is `stdlib/audio.hp`.
- **Host availability (static dependency query only, not a test):**
  `pkg-config --modversion sdl3` returned `3.4.18`, with headers at
  `/opt/homebrew/Cellar/sdl3/3.4.18/include/SDL3/SDL.h`. An existing build log for
  `graphics_test` is empty (no recorded compile output), and there is no
  `tests/stdlib/graphics_test.cp` source in the repo. Dependency presence is established;
  no executed behavior is implied.

## 5. APIs lacking any deep test

Per inventory §5 (`docs/stdlib/REGRESSION_INVENTORY.md:160,189`), the **9** unexercised
entries are: `surface.surface`, `surface.width`, `surface.height`, `surface.pixel`,
`renderer.renderer`, `renderer.fill_rect`, `renderer.present`, `renderer.set_vsync`,
`renderer.drop`.

Additionally, source inspection finds `renderer.set_color` (`:136-148`) absent from the
inventory and therefore uncredited (parser omission, §2).

**Stronger source-inspection caveat:** no deep-corpus file includes `stdlib/graphics.hp`;
grep finds the token `surface` in only one deep file (`deep_fs_task5.cp`, unrelated prose)
and no deep file contains `renderer`. The 7 "exercised" credits
(`docs/stdlib/REGRESSION_INVENTORY.md:118`: `surface.valid`, `surface.drop` [D],
`renderer.valid`, `renderer.native`, `renderer.clear`, `renderer.line`, `renderer.read`)
are bare-name lexical collisions on generic `valid(`, `native(`, `clear(`, `line(`, `read(`
tokens and the `surface` owner token in unrelated tests — the D2/D5/D7 bias documented at
`docs/stdlib/REGRESSION_INVENTORY.md:222,225,227`. In particular, `surface.pixel`, the only
function here with explicit bounds/pitch/locking logic, has **no** deep test, and the
constructors (`surface.surface`, `renderer.renderer`) are unexercised. There is no shallow
`graphics_test.cp` and no graphics demo in `tests/stdlib/` either, so the deep corpus is the
only inventory corpus and it contains no real use of this module
(`docs/stdlib/REGRESSION_INVENTORY.md:12,230`).

## 6. Environment blockers for execution on this host

- **No deep test to execute:** no `deep_*.cp` includes the header, and there is no
  `graphics_test.cp` or graphics demo to exercise it even shallowly.
- **SDL3 is installed** (`3.4.18`, §4), so a missing library is not the blocker.
- **Display/video subsystem:** SDL3 `SDL_Init(SDL_INIT_VIDEO)` (in `stdlib/window.hp`) and
  `SDL_CreateRenderer` (`stdlib/graphics.hp:122`) require a working video backend. On a
  headless host without a display, construction fails unless an offscreen/dummy video
  driver is configured (e.g. `SDL_VIDEODRIVER=dummy`) — that configuration is not present
  in this header. This is the principal runtime blocker for executing graphics code here.
- **Valid window required:** a `renderer` needs a live `SDL_Window *`
  (`stdlib/graphics.hp:114-126`), which in turn needs a live `sdl`/`window` RAII setup from
  `stdlib/window.hp`; `renderer` with NULL window is rejected (`:118-121`).
- **Pixel-format dependence:** `surface.pixel` requires a format with `bpp >= 32`
  (`stdlib/graphics.hp:52-58`); a test snapshot from `renderer.read` must yield such a
  format or the call fails — not controllable from this header alone.

I did not run any test or host process that executes the module; no executed behavior is
claimed.

## 7. What cannot be guaranteed

- Static inspection cannot guarantee SDL_Init/window/renderer creation or any draw call
  succeeds on a headless host.
- It cannot guarantee `surface.pixel` is memory-safe for all formats: the fixed `+4` row
  bound and `bpp >= 32` allowance for >4-byte formats are static concerns
  (`stdlib/graphics.hp:52-70,89-95`) that have not been executed or fuzzed.
- It cannot guarantee the borrowed `SDL_Window *` outlives the `renderer`
  (`stdlib/graphics.hp:111,118-122`), or that a shallow copy of `surface`/`renderer` is not
  double-dropped (`:102-107,234-239`).
- It cannot guarantee `fill_rect`/`set_vsync` accept arbitrary float/int inputs
  (`stdlib/graphics.hp:163-179,207-218`) — validation is delegated to SDL and untested.
- It cannot guarantee any "exercised" credit reflects a real call: all 7 are lexical
  collisions and no deep test includes the header
  (`docs/stdlib/REGRESSION_INVENTORY.md:222,225,227,234`).
- No execution-based safety, regression, or runtime-success claim is made for this module.
