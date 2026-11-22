# Safety audit — `stdlib/window.hp`

> **STATIC — SOURCE INSPECTION ONLY. NOT EXECUTED.** Nothing in this document was
> compiled, linked, injected, or run. No test runner was invoked. No runtime,
> live-game, or "safe in practice" claim is made anywhere in this audit.

## 1. Provenance and scope

| Item | Value |
| --- | --- |
| Branch | `agent/modern-stdlib` |
| Header inspected | `stdlib/window.hp` (114 lines, 11 definitions by source inspection — matches the inventory count) |
| Count authority | `docs/stdlib/REGRESSION_INVENTORY.md` (snapshot generated 2026-11-23 04:24 IST) |
| Enumeration method | `tests/stdlib/coverage.py::entries()` imported verbatim by the inventory (inventory §1, items 1–2) |
| Corpus | only `tests/stdlib/deep_*.cp` (28 files) + 5 sibling `deep_*.stdin` (inventory §1 item 3, §6 D8) |

All counts and exercised/unexercised names below are copied from the inventory, not
recomputed. The safety observations in §4 are STATIC reading of `stdlib/window.hp`
and the SDL3 contracts it references; they are not verified behavior.

## 2. Public API surface — exact counts from the inventory

Inventory §3: `window.hp` = **11 public callable entries**.
Inventory §4: **5 exercised**. Inventory §5: **6 not exercised**.

| # | Entry | Source | Status (inventory) |
| --- | --- | --- | --- |
| 1 | `sdl.sdl` (constructor) | `stdlib/window.hp:19-30` | NOT exercised |
| 2 | `sdl.valid` | `stdlib/window.hp:32-34` | EXERCISED `[L]` |
| 3 | `sdl.subsystem_flags` | `stdlib/window.hp:36-38` | NOT exercised |
| 4 | `sdl.drop` | `stdlib/window.hp:40-45` | NOT exercised |
| 5 | `sdl_backend_error` (free) | `stdlib/window.hp:48-50` | NOT exercised |
| 6 | `window.window` (constructor) | `stdlib/window.hp:55-67` | NOT exercised |
| 7 | `window.valid` | `stdlib/window.hp:69-71` | EXERCISED `[L]` |
| 8 | `window.native` | `stdlib/window.hp:74-76` | EXERCISED `[L]` |
| 9 | `window.size` | `stdlib/window.hp:78-91` | EXERCISED `[L]` |
| 10 | `window.set_title` | `stdlib/window.hp:93-104` | NOT exercised |
| 11 | `window.drop` | `stdlib/window.hp:106-111` | EXERCISED `[D]` |

The source has 11 definitions and the inventory has 11 entries; no multi-line
signature discrepancy was found in this header.

## 3. Which APIs lack any deep test

- **6 of 11 entries are listed as unexercised** (inventory §5, `window.hp` row,
  reproduced in §2 above): `sdl.sdl`, `sdl.subsystem_flags`, `sdl.drop`,
  `sdl_backend_error`, `window.window`, `window.set_title`.
- The **5 credited as exercised are not evidence of real `window.hp` use**:
  - No deep-test file includes `stdlib/window.hp` — a search of the 28-file corpus
    for the header and for the tokens `sdl` and `window` as code finds no use.
  - `sdl.valid`, `window.valid`, `window.native`, `window.size` (`:32`, `:69`,
    `:74`, `:78`) are **bare-name matches**. `valid(`, `native(`, and `size(` occur
    in deep tests on other classes (e.g. `tests/stdlib/deep_log_task5.cp:200`), and
    `coverage.py:87-88` matches unqualified names — the D2 upward bias.
  - `window.drop` is a `[D]` compiler-inserted-destructor credit, **not** a literal
    call. The inventory's §6 D5 explicitly names `window` as one of three owner
    tokens that occur only in prose comments; the deep corpus confirms this — every
    `window` token is in a comment (e.g.
    `tests/stdlib/deep_vector_task4.cp:517`, `tests/stdlib/deep_string_view.cp:4`).
    Under the stricter reading described in §6 D7, this credit would not qualify.
- Therefore, by source inspection, **all 11 entries lack a genuine deep test that
  constructs an `sdl` or `window` or calls `SDL_*` through this module**. Neither
  SDL initialization/teardown nor window creation/resize/title setting was observed.

## 4. STATIC safety analysis — not executed

**Positive source-level guards observed**

- `sdl` constructor (`:19-30`) resets the error and reports `SDL_Init` failure with
  `SDL_GetError()` (`:24-27`); `drop` (`:40-45`) is idempotent via `initialized`.
- `window` constructor (`:55-67`) rejects non-positive width/height with a
  categorized error (`:58-61`) and reports `SDL_CreateWindow` failure (`:64-65`).
- `window::size` (`:78-91`) NULL-checks the handle and both out-parameters,
  zero-initializes them (`:84-85`), and reports `SDL_GetWindowSize` failure.
- `window::set_title` (`:93-104`) NULL-checks the handle and title and reports
  `SDL_SetWindowTitle` failure.
- `window::drop` (`:106-111`) destroys and NULLs the handle (idempotent).
- Error plumbing tolerates a NULL `error *` and copies messages into a bounded buffer
  (`stdlib/error.hp:581-589`, `:607-624`), so the `SDL_GetError()` pointers passed at
  `:25`, `:65`, `:87`, `:100` are not retained as dangling references.

**Conditions the wrapper does not check — relied upon from SDL3 or the caller**

- **`sdl` is process-wide with no cross-instance refcount.** `SDL_Init`/`SDL_Quit`
  (`:24`, `:42`) are global. Two live `sdl` objects are not coordinated: dropping
  one calls `SDL_Quit()` and invalidates subsystems the other still believes are
  available (its `initialized` flag stays 1). The header documents that "windows
  must be dropped before it" (`:14`) but does not enforce it.
- `sdl_backend_error` (`:48-50`) returns SDL's internal error string pointer. SDL
  may overwrite that buffer on a later SDL call; STATIC observation only.
- `window::native` (`:74-76`) returns the raw `SDL_Window *` unconditionally,
  including NULL after a failed constructor or after `drop`. Callers must apply
  `valid()`.
- `window` constructor does not NULL-check `title`; it is forwarded to
  `SDL_CreateWindow` (`:62-63`). Any NULL handling is SDL's responsibility.
- `flags` values are cast to `SDL_InitFlags`/`SDL_WindowFlags` (`:24`, `:63`) without
  validation; unknown bit combinations are left to SDL.
- The `window` handle is borrowed by `gui`/Nuklear (`stdlib/gui.hp:37-58`). The
  ordering requirement (drop `gui` before `window`, drop `window` before `sdl`) is
  documented but not enforced by this header.
- SDL window APIs are main/UI-thread constrained. No synchronization or
  thread-affinity enforcement exists in `stdlib/window.hp`; this is a STATIC
  concern, not an observed failure.

## 5. External dependency requirements

| Dependency | Requirement | Provenance |
| --- | --- | --- |
| SDL3 | linked with `pkg-config sdl3`; header `SDL3/SDL.h` | `stdlib/window.hp:5,10`; `docs/MODERN_STDLIB.md:138` (SDL3 3.4.18, zlib, "linked via pkg-config sdl3, never copied") |
| C+ runtime | `cplus/cplus.h`, `stdlib/error.hp` | `stdlib/window.hp:11-12`; error helpers at `stdlib/error.hp:526-530,581-624` |

No Nuklear or FFmpeg dependency in this header. Local environment check (not a test
run): `pkg-config --modversion sdl3` reports 3.4.18 and
`/opt/homebrew/include/SDL3/SDL.h` exists. Dependency availability is not evidence
that any `window` or `sdl` API works.

## 6. Environment blockers

1. **No deep-test harness.** No `deep_*.cp` includes `stdlib/window.hp` (§3). The
   corpus is frozen at 28 `.cp` + 5 `.stdin` files (inventory §6 D8); producing real
   lifetime/window evidence requires adding tests and recomputing.
2. **No build/link target.** Root `CMakeLists.txt` builds `cplus_core`, `c+`, and
   `cplus_runtime` only (`CMakeLists.txt:87-98`, `:226-237`); CTest at `:268-273`
   runs `tests/run_tests.sh` and `demos/run_demos.sh`. No target in this repository
   compiles a `.hp` including SDL3 or links `pkg-config sdl3`.
3. **Headless display.** `SDL_CreateWindow` (`:62`) and `SDL_Init(SDL_INIT_VIDEO)`
   require a windowing/display environment. A headless CI context cannot create a
   window, so the constructor and the size/title paths cannot be exercised here.
4. **Process-global state.** `SDL_Init`/`SDL_Quit` affect the whole process
   (§4); any harness must serialize SDL lifetime tests and avoid concurrent
   windows, which no existing test infrastructure arranges.

## 7. What cannot be guaranteed — no runtime safety claim

- This audit does **not** guarantee that `window.hp` is correct, memory-safe,
  hardened, leak-free, or regression-free. It records only what the source text
  appears to do and what the inventory records.
- The five "exercised" entries (§3) are bare-name or comment-token credits; **no
  runtime behavior of any `sdl` or `window` method was observed**. In particular,
  the lifecycle ordering requirement (§4) is unverified.
- The cross-instance `SDL_Quit` concern and borrowed-handle ordering are STATIC
  observations requiring execution, sanitizers, or formal analysis to confirm or
  refute; they are not findings proven at runtime.
- Per the inventory's scope statement (`docs/stdlib/REGRESSION_INVENTORY.md:232-234`),
  the underlying call-site inventory is static and is not a safety proof; this
  document inherits that limitation and adds no execution evidence.
