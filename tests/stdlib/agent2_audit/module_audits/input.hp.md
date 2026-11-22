# Safety audit — `stdlib/input.hp`

> **STATIC — SOURCE INSPECTION ONLY. NOT EXECUTED.** Nothing in this document was
> compiled, linked, injected, or run. No test runner was invoked. No runtime,
> live-game, or "safe in practice" claim is made anywhere in this audit.

## 1. Provenance and scope

| Item | Value |
| --- | --- |
| Branch | `agent/modern-stdlib` |
| Header inspected | `stdlib/input.hp` (65 lines, 10 free-function definitions) |
| Count authority | `docs/stdlib/REGRESSION_INVENTORY.md` (snapshot generated 2026-11-23 04:24 IST) |
| Enumeration method | `tests/stdlib/coverage.py::entries()` imported verbatim by the inventory (inventory §1, items 1–2) |
| Corpus | only `tests/stdlib/deep_*.cp` (28 files) + 5 sibling `deep_*.stdin` (inventory §1 item 3, §6 D8) |

`input.hp` is the weakest-covered module in the inventory: the **only** header with
zero exercised entries. All counts and exercised/unexercised names below are copied
from the inventory, not recomputed. The safety observations in §4 are STATIC reading
of `stdlib/input.hp` and SDL3's documented contracts as referenced by the header;
they are not verified behavior.

## 2. Public API surface — exact counts from the inventory

Inventory §3: `input.hp` = **10 public callable entries**.
Inventory §4: **0 exercised**. Inventory §5: **10 not exercised** — every entry.

| # | Entry (all free `static inline`) | Source | Status (inventory) |
| --- | --- | --- | --- |
| 1 | `poll_event` | `stdlib/input.hp:15-17` | NOT exercised |
| 2 | `event_is_quit` | `stdlib/input.hp:19-21` | NOT exercised |
| 3 | `event_is_key_down` | `stdlib/input.hp:23-25` | NOT exercised |
| 4 | `event_is_key_up` | `stdlib/input.hp:27-29` | NOT exercised |
| 5 | `event_scancode` | `stdlib/input.hp:31-36` | NOT exercised |
| 6 | `event_text` | `stdlib/input.hp:38-43` | NOT exercised |
| 7 | `keyboard_state` | `stdlib/input.hp:46-48` | NOT exercised |
| 8 | `key_pressed` | `stdlib/input.hp:50-54` | NOT exercised |
| 9 | `mouse_state` | `stdlib/input.hp:57-59` | NOT exercised |
| 10 | `mouse_button_left` | `stdlib/input.hp:61-63` | NOT exercised |

There is no constructor/RAII entry for this module; all ten are stateless free
functions. The inventory's public-callable parser and the header agree on 10.

## 3. Which APIs lack any deep test

- **All 10 of 10 entries lack a deep test** (inventory §4 `input.hp` = 0; §5
  `input.hp` = 10, reproduced in §2 above). There are no partial or name-only
  credits to discount here.
- A search of the 28-file deep corpus for `stdlib/input.hp` and for the module's
  function names returns no matches. No deep test constructs or polls an SDL event,
  calls a keyboard/mouse query, or includes this header.
- Consequently there is **no deep-test evidence for any input behavior**: event type
  classification, scancode extraction, text extraction, keyboard/mouse state, or
  button-mask decoding.

## 4. STATIC safety analysis — not executed

All statements below are reading of source text only.

**Positive source-level guards observed**

- `event_is_quit`, `event_is_key_down`, `event_is_key_up` (`:19-29`) NULL-check the
  event before reading `event->type`.
- `event_scancode` (`:31-36`) NULL-checks and restricts extraction to
  `SDL_EVENT_KEY_DOWN`/`SDL_EVENT_KEY_UP`; other events return
  `SDL_SCANCODE_UNKNOWN`.
- `event_text` (`:38-43`) checks the event pointer, the `SDL_EVENT_TEXT_INPUT` type,
  and `event->text.text` before indexing `[0]`.
- `key_pressed` (`:50-54`) checks the state pointer and bounds-checks the scancode
  against `SDL_SCANCODE_COUNT`, covering both negative and out-of-range values
  (`:51`).
- `drop`-style lifecycle hazards do not apply: these are free functions with no
  owned resources.

**Conditions the wrapper relies upon — not checked here**

- `poll_event` (`:15-17`) forwards its argument directly to `SDL_PollEvent`. The
  header documents that a NULL event "only pumps" (`:13-15`). Safety of NULL
  therefore depends on SDL3's documented behavior, not on a check in this file.
- `keyboard_state` (`:46-48`) returns the borrowed pointer from
  `SDL_GetKeyboardState(NULL)`. The header states it is "valid until the next pump"
  and "never free it" (`:45`). Holding the pointer across a pump or passing it to
  `free` is a caller-contract violation that the type system does not prevent.
- `mouse_state` (`:57-59`) passes `x`/`y` straight through to `SDL_GetMouseState`;
  NULL out-parameters are allowed by SDL but are not validated here.
- `event_text` returns only `event->text.text[0]` (`:42`). SDL text input is UTF-8,
  so a multi-byte code point is truncated to its first byte. This is a correctness
  limitation visible in source; no memory-safety conclusion follows from it.
- No API in this module accepts an `error *` or returns an error object. Failures
  must be inferred by the caller from SDL state (e.g. `sdl_backend_error` in
  `stdlib/window.hp:48-50`). There is no error-reporting path to audit.
- SDL3 event polling, keyboard state, and mouse state are main/event-thread APIs.
  No synchronization or thread-affinity enforcement exists in `stdlib/input.hp`;
  using these helpers from another thread is a STATIC concern only, not an
  observed failure.

## 5. External dependency requirements

| Dependency | Requirement | Provenance |
| --- | --- | --- |
| SDL3 | linked with `pkg-config sdl3`; header `SDL3/SDL.h` | `stdlib/input.hp:5,10`; `docs/MODERN_STDLIB.md:138` (SDL3 3.4.18, zlib, "linked via pkg-config sdl3, never copied") |
| C+ runtime | `cplus/cplus.h` | `stdlib/input.hp:11` |

No Nuklear or FFmpeg dependency. Local environment check (not a test run):
`pkg-config --modversion sdl3` reports 3.4.18 and `/opt/homebrew/include/SDL3/SDL.h`
exists. Dependency availability is not evidence that any `input` function works.

## 6. Environment blockers

1. **Zero deep-test coverage.** The inventory records 0 of 10 exercised. No deep
   test includes `stdlib/input.hp` (§3). Generating real evidence requires adding
   an SDL event-pump harness and recomputing the inventory (corpus snapshot §6 D8).
2. **No build/link target.** Root `CMakeLists.txt` builds `cplus_core`, `c+`, and
   `cplus_runtime` only (`CMakeLists.txt:87-98`, `:226-237`); CTest at `:268-273`
   runs `tests/run_tests.sh` and `demos/run_demos.sh`. No target in this repository
   compiles a `.hp` that includes SDL3 or links `pkg-config sdl3`.
3. **No event loop or display.** These helpers only do something meaningful when
   SDL events are pumped and a video subsystem exists. A headless CI context has no
   window/display to generate key, text, or mouse events, so even a newly added
   harness would need synthetic event injection to exercise them.
4. **Thread affinity.** SDL input APIs require the event thread; no harness in
   `tests/stdlib` drives an event loop from a controlled thread.

## 7. What cannot be guaranteed — no runtime safety claim

- This audit does **not** guarantee that `input.hp` is correct, memory-safe,
  hardened, or regression-free. It records only what the source text appears to do
  and what the inventory records.
- With **0 of 10 entries exercised**, no runtime behavior of any `input` function was
  observed. The source-level checks in §4 are reading, not verification.
- Claims that depend on SDL3 contracts (NULL-event polling, borrowed keyboard-state
  lifetime, NULL mouse out-parameters, UTF-8 text semantics) are **not verified** by
  this audit.
- Per the inventory's scope statement (`docs/stdlib/REGRESSION_INVENTORY.md:232-234`),
  the underlying call-site inventory is static and is not a safety proof; this
  document inherits that limitation and adds no execution evidence.
