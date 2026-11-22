# Safety audit — `stdlib/gui.hp`

> **STATIC — SOURCE INSPECTION ONLY. NOT EXECUTED.** Nothing in this document was
> compiled, linked, injected, or run. No test runner was invoked. No runtime,
> live-game, or "safe in practice" claim is made anywhere in this audit.

## 1. Provenance and scope

| Item | Value |
| --- | --- |
| Branch | `agent/modern-stdlib` |
| Header inspected | `stdlib/gui.hp` (197 lines, 19 method definitions by source inspection) |
| Count authority | `docs/stdlib/REGRESSION_INVENTORY.md` (snapshot generated 2026-11-23 04:24 IST) |
| Enumeration method | `tests/stdlib/coverage.py::entries()` imported verbatim by the inventory (inventory §1, items 1–2) |
| Corpus | only `tests/stdlib/deep_*.cp` (28 files) + 5 sibling `deep_*.stdin` (inventory §1 item 3, §6 D8) |

This is a **per-file, source-inspection-only** companion to the inventory. All counts
and exercised/unexercised names below are copied from the inventory, not recomputed.
The safety observations in §4 are STATIC reading of `stdlib/gui.hp` and its
dependencies; they are hypotheses about the source text, not verified behavior.

## 2. Public API surface — exact counts from the inventory

Inventory §3: `gui.hp` = **18 public callable entries**.
Inventory §4: **2 exercised**. Inventory §5: **16 not exercised**.

Every entry, with the source line and the inventory's classification:

| # | Entry | Source | Status (inventory) |
| --- | --- | --- | --- |
| 1 | `gui.gui` (constructor) | `stdlib/gui.hp:37-58` | NOT exercised |
| 2 | `gui.valid` | `stdlib/gui.hp:60-62` | EXERCISED `[L]` |
| 3 | `gui.native` | `stdlib/gui.hp:65-67` | EXERCISED `[L]` |
| 4 | `gui.handle_event` | `stdlib/gui.hp:70-74` | NOT exercised |
| 5 | `gui.input_begin` | `stdlib/gui.hp:76-79` | NOT exercised |
| 6 | `gui.input_motion` | `stdlib/gui.hp:81-87` | NOT exercised |
| 7 | `gui.input_button` | `stdlib/gui.hp:91-95` | NOT exercised |
| 8 | `gui.input_end` | `stdlib/gui.hp:97-100` | NOT exercised |
| 9 | `gui.window_end` | `stdlib/gui.hp:113-116` | NOT exercised |
| 10 | `gui.layout_row_dynamic` | `stdlib/gui.hp:120-123` | NOT exercised |
| 11 | `gui.layout_row_static` | `stdlib/gui.hp:125-128` | NOT exercised |
| 12 | `gui.button` | `stdlib/gui.hp:130-134` | NOT exercised |
| 13 | `gui.label` | `stdlib/gui.hp:136-139` | NOT exercised |
| 14 | `gui.slider` | `stdlib/gui.hp:141-145` | NOT exercised |
| 15 | `gui.widget_bounds` | `stdlib/gui.hp:149-171` | NOT exercised |
| 16 | `gui.render` | `stdlib/gui.hp:175-179` | NOT exercised |
| 17 | `gui.style_text_color` | `stdlib/gui.hp:181-186` | NOT exercised |
| 18 | `gui.drop` | `stdlib/gui.hp:188-194` | NOT exercised |

**Counting-fidelity observation (STATIC, not a recount).** Source inspection also finds
`gui::window_begin` at `stdlib/gui.hp:103-111`. Its signature spans two lines, so it
does not match the single-line `SIG` regex at `tests/stdlib/coverage.py:24-25` and is
absent from both inventory lists. The official count of 18 is used verbatim as
required; this note records the discrepancy only.

## 3. Which APIs lack any deep test

- **16 of 18 entries are listed as unexercised** (inventory §5, `gui.hp` row, reproduced
  in §2 above).
- The **2 credited as exercised** (`gui.valid`, `gui.native`) are **bare-name matches**,
  not uses of `gui`. No deep-test file includes `stdlib/gui.hp` — a search of the
  28-file corpus for `stdlib/gui.hp` and for the token `gui` returns no matches.
  `valid(` and `native(` occur in deep tests on *other* classes (e.g.
  `tests/stdlib/deep_log_task5.cp:200`), and `coverage.py:87-88` matches unqualified
  names. This is the upward-bias caveat documented at inventory §6 D2, D7.
- Therefore, by source inspection, **all 18 entries lack a genuine deep test that
  constructs or drives a `gui` object**. The inventory's "2 exercised" is a lexical
  artifact and must not be read as evidence that any `gui` path was exercised.

## 4. STATIC safety analysis — not executed

All statements below are reading of source text only. "Guarded" means a source-level
check exists; it does not mean the check was observed to work at runtime.

**Positive source-level guards observed**

- Constructor `stdlib/gui.hp:37-58` rejects NULL window/renderer (`:43-45`) and NULL
  `nk_sdl_init` result (`:48-50`) before use; every other method is gated by
  `valid()` (`:60-62`), which requires both `initialized` and `context`.
- `handle_event` NULL-checks the event pointer (`:70-74`). `window_begin`, `button`,
  `label` NULL-check their C-string arguments (`:105`, `:131`, `:137`).
- `layout_row_dynamic`/`layout_row_static` reject `columns <= 0` (`:121`, `:126`).
- `slider` rejects a NULL value pointer and an inverted range (`:141-145`).
- `widget_bounds` (`:149-171`) is the most hardened routine: it rejects a NULL
  `context->current`, then rejects non-finite and out-of-`int`-range float bounds
  (`:155-160`) before the casts at `:162-169`. The upper bound `2147483520.0f` is the
  largest float below `INT_MAX`, so the casts avoid overflow by construction — a
  static observation only.
- `drop` (`:188-194`) is idempotent: it checks both flags, then clears `context` and
  `initialized`.
- Error plumbing tolerates a NULL `error *`: `error_reset`/`error_fail_cat` return
  early on NULL (`stdlib/error.hp:581-589`, `:607-624`) and `error_fail_cat` copies the
  message into a bounded fixed buffer (`:617-623`), so stack-local messages used by
  the constructors (e.g. `stdlib/gui.hp:44`) are not left dangling.

**Conditions the wrapper does not check — relied upon from Nuklear/SDL**

- `native()` (`:65-67`) returns the raw `nk_context *` unconditionally, including a
  NULL context on a failed constructor. Callers must apply `valid()` first.
- Frame protocol is enforced by comment only: "Call once per frame, after the last
  `window_end()`" (`:173-174`). Source inspection finds no internal state tracking
  that `window_end` (`:113-116`) is paired with a successful `window_begin`, or that
  `render` (`:175-179`) is called exactly once per frame. Misordered calls are passed
  straight to Nuklear.
- `input_begin`/`input_motion`/`input_button`/`input_end` (`:76-100`) are thin gated
  forwards. `input_button` (`:91-95`) uses the last motion coordinates stored at
  `:84-85`; with no prior motion the click is at `(0,0)`. No coordinate clamping is
  done here.
- `layout_row_dynamic`/`layout_row_static` (`:120-128`) do not validate `height` or
  `item_width` (negative or non-finite values are forwarded).
- `style_text_color` (`:181-186`) mutates `context->style` directly.
- Nuklear is a single-threaded immediate-mode library and the wrapper adds no
  locking. Concurrent calls to `handle_event`, `input_*`, `window_*`, or `render`
  would race on `nk_context`; this is a STATIC inference from the absence of
  synchronization in `stdlib/gui.hp`, not an observed race.

## 5. External dependency requirements

| Dependency | Requirement | Provenance |
| --- | --- | --- |
| SDL3 | linked with `pkg-config sdl3`; header `SDL3/SDL.h` | `stdlib/gui.hp:9,24`; `docs/MODERN_STDLIB.md:138` (SDL3 3.4.18, zlib, "linked via pkg-config sdl3, never copied") |
| Nuklear | vendored `third_party/nuklear/nuklear.h`, `nuklear_sdl3_renderer.h`, `LICENSE`; compile `nuklear_impl.c` once | `stdlib/gui.hp:1-2,9,27-28`; `docs/MODERN_STDLIB.md:137` (revision `bab18497…`, MIT or public domain). All four files are present in `third_party/nuklear/`. |
| C+ runtime | `cplus/cplus.h`, `stdlib/error.hp` | `stdlib/gui.hp:25-26`; `error_reset`/`error_fail` from `stdlib/error.hp:526-530,581-624` |

Local environment check (not a test run): `pkg-config --modversion sdl3` reports
3.4.18 and `/opt/homebrew/include/SDL3/SDL.h` exists. Dependency *availability* is
not evidence that any `gui` API works.

## 6. Environment blockers

1. **No deep-test harness.** No `deep_*.cp` includes `stdlib/gui.hp` (§3). The
   inventory corpus is frozen at 28 `.cp` + 5 `.stdin` files (inventory §6 D8);
   producing real `gui` evidence requires adding such tests and recomputing.
2. **No build/link target.** Root `CMakeLists.txt` builds `cplus_core`, `c+`, and
   `cplus_runtime` only (`CMakeLists.txt:87-98`, `:226-237`); the CTest entries at
   `:268-273` run `tests/run_tests.sh` and `demos/run_demos.sh`. No target in this
   repository compiles a `.hp` that includes SDL3 or Nuklear or links
   `pkg-config sdl3`/`nuklear_impl.c`.
3. **Headless display.** `gui` requires a live SDL renderer/window
   (`stdlib/gui.hp:37-58`, `:175-179`). A CI/headless environment cannot create the
   native window/renderer that the constructor requires, so the initialization and
   render paths cannot be exercised here.
4. **Threading/events.** Event and widget calls must originate on SDL's event thread;
   there is no event-loop harness in `tests/stdlib` to drive `handle_event`/`input_*`.

## 7. What cannot be guaranteed — no runtime safety claim

- This audit does **not** guarantee that `gui.hp` is correct, memory-safe, hardened,
  leak-free, or regression-free. It records only what the source text appears to do
  and what the inventory records.
- The two "exercised" entries (§3) are name-only credits; **no runtime behavior of
  any `gui` method was observed**. The 16 unexercised entries have no deep-test
  evidence at all.
- Claims that depend on Nuklear or SDL3 internals (frame ordering enforcement,
  widget bounds, renderer lifetime, error propagation from `nk_sdl_*`) are **not
  verified** by this audit.
- Per the inventory's own scope statement (`docs/stdlib/REGRESSION_INVENTORY.md:232-234`),
  the underlying call-site inventory is static and is not a safety proof; this
  document inherits that limitation and adds no execution evidence.
