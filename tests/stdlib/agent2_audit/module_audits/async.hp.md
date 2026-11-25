# Safety audit — `stdlib/async.hp` (event loop / timers / file I/O via libuv)

> **LABEL: STATIC SOURCE INSPECTION ONLY — NOT EXECUTED.** No test runner, compiler, or
> game host was invoked to produce this document. All statements below are derived from
> reading `stdlib/async.hp` and from the static inventory in
> `docs/stdlib/REGRESSION_INVENTORY.md`. No behavior is claimed to be verified by
> execution. See §7 for what cannot be guaranteed.

## 1. Provenance and method

- Header inspected: `stdlib/async.hp` (226 lines). Header scope comment and link note at
  `stdlib/async.hp:1-6`; includes at `stdlib/async.hp:10-15`.
- Exact counts and unexercised names are taken verbatim from
  `docs/stdlib/REGRESSION_INVENTORY.md`:
  - §3 per-header total **14 public callable entries** (`docs/stdlib/REGRESSION_INVENTORY.md:46,50`).
  - §4 exercised **5 of 14** (`docs/stdlib/REGRESSION_INVENTORY.md:75,79`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:106`.
  - §5 not exercised **9 of 14** (`docs/stdlib/REGRESSION_INVENTORY.md:150,154`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:177`.
- The inventory's own §1/§7 (`docs/stdlib/REGRESSION_INVENTORY.md:6-15,232-234`) states this
  is a lexical call-site snapshot, not execution coverage and not a safety proof.

## 2. Public API list with counts

The inventory's 14 entries (counted by `tests/stdlib/coverage.py::entries`, which matches
single-line `) {` signatures; see `docs/stdlib/REGRESSION_INVENTORY.md:11`), with header
line numbers from source inspection:

| # | Entry | Header lines |
|---|-------|--------------|
| 1 | `loop.loop` (constructor) | `stdlib/async.hp:21-31` |
| 2 | `loop.valid` | `stdlib/async.hp:33-35` |
| 3 | `loop.native` | `stdlib/async.hp:38-40` |
| 4 | `loop.run` | `stdlib/async.hp:42-51` |
| 5 | `loop.stop` | `stdlib/async.hp:53-56` |
| 6 | `loop.now_ms` | `stdlib/async.hp:58-62` |
| 7 | `loop.drop` | `stdlib/async.hp:67-78` |
| 8 | `timer.timer` (constructor) | `stdlib/async.hp:92-115` |
| 9 | `timer.valid` | `stdlib/async.hp:117-119` |
| 10 | `timer.stop` | `stdlib/async.hp:137-148` |
| 11 | `timer.is_active` | `stdlib/async.hp:150-154` |
| 12 | `timer.drop` | `stdlib/async.hp:159-177` |
| 13 | `sleep_ms` | `stdlib/async.hp:218-220` |
| 14 | `uv_error_text` | `stdlib/async.hp:222-224` |

**Parser omissions found by source inspection (not "invented" API — real public callables):**
the inventory's lexical `entries()` parser misses multi-line signatures. Two additional
public callables exist in the header:
- `timer.start(uv_timer_cb, unsigned long long, unsigned long long, void *, error *)`
  at `stdlib/async.hp:121-135` (signature spans lines 121-123, no `(` on the final line).
- `read_file(loop *, const char *, char *, size_t, error *)` at
  `stdlib/async.hp:180-216` (signature spans lines 181-182).

The internal helper `stdlib_timer_release_handle` (`stdlib/async.hp:82-84`) is excluded by
the inventory's `stdlib_` rule (`docs/stdlib/REGRESSION_INVENTORY.md:11,224`). Source
inspection therefore sees **16 public callables**, while the audited headline count is the
inventory's **14**.

## 3. Public surface safety analysis (STATIC — source inspection only)

- **Loop construction/validity:** `loop::loop` zeroes `initialized` then calls
  `uv_loop_init`, failing with `CPLUS_EXIT_RESOURCE` on error (`stdlib/async.hp:21-31`).
  `valid()` gates all other methods (`stdlib/async.hp:33-35`, used at 44, 54, 59). No
  copy/move control is declared in the header; a shallow struct copy would duplicate the
  embedded `uv_loop_t` and break RAII — this is a static observation, not a measured defect.
- **`run`:** casts `int mode` directly to `uv_run_mode` with no range validation before
  `uv_run` (`stdlib/async.hp:42-51`). An out-of-range `mode` is passed through to libuv;
  the header does not enumerate legal values. Non-zero `uv_run` is only logged at DEBUG
  (line 49), and the method still returns `STDLIB_OK` (line 50).
- **`drop` (destructor path):** bounded drain loop `guard < 1000` with `UV_RUN_NOWAIT`
  (`stdlib/async.hp:64-78`). If the loop is still busy after 1000 iterations,
  `uv_loop_close` fails and only a WARN is logged (lines 75-76); `initialized` is then set
  to 0 regardless, so `valid()` returns false while native resources may remain. This is a
  bounded best-effort cleanup, not a guarantee of resource release.
- **Timer lifetime:** `timer::timer` allocates `uv_timer_t` with `stdlib_alloc`, checks NULL
  and `uv_timer_init` failure, and stores a borrowed `uv_loop_t *`
  (`stdlib/async.hp:92-115`). `timer::drop` stops the timer, clears `handle->data`, calls
  `uv_close` with a release callback that frees the heap handle, then drains the owner loop
  with the same 1000-iteration bound (`stdlib/async.hp:156-177`). The owner loop is a
  **borrowed pointer**; if the owning `loop` is dropped before the timer, the drain in
  `timer::drop` dereferences a stale `owner_loop` (line 169). Ordering is the caller's
  responsibility; the header does not enforce it.
- **`timer.start`:** validates `valid()` and non-NULL callback before storing `user` into
  `handle->data` and calling `uv_timer_start` (`stdlib/async.hp:121-135`). A `repeat_ms` of
  0 is documented as one-shot (line 121). No re-entrancy or concurrent-call protection.
- **`timer.stop` / `is_active`:** validity-checked, delegate to libuv, report `CPLUS_EXIT_IO`
  on failure (`stdlib/async.hp:137-154`).
- **`read_file`:** validates all pointers, `size != 0`, and `size <= UINT_MAX`
  (`stdlib/async.hp:188-198`); bounds category `CPLUS_EXIT_BOUNDS` at 194-198. Performs a
  **single** synchronous `uv_fs_read` at offset -1 (lines 206-207) — it reads at most one
  chunk and does not loop to EOF despite the "reads at most size bytes" wording (line 180).
  Error strings come from `uv_strerror` (lines 201, 209). `uv_fs_close` return value is
  ignored (line 213).
- **`sleep_ms` / `uv_error_text`:** thin wrappers over `uv_sleep` and `uv_strerror`
  (`stdlib/async.hp:218-224`); no local validation required.

Everything in this section is a reading of the source text; none of it has been executed.

## 4. External dependency requirements

- **libuv** is the event engine. `#include <uv.h>` at `stdlib/async.hp:13`; header comment
  "Link with pkg-config libuv" at `stdlib/async.hp:5`. Uses `uv_loop_*`, `uv_run`,
  `uv_timer_*`, `uv_fs_*`, `uv_strerror`, `uv_sleep`, `uv_buf_init`.
- **C+ runtime / `cplus/cplus.h`** at `stdlib/async.hp:14`, plus `stdlib/error.hp` at
  `stdlib/async.hp:15` (error/category definitions, telemetry).
- **Not a dependency of this module:** SDL3, libcurl, stb, ffmpeg (those belong to other
  headers). ffmpeg in particular is used by `stdlib/video.hp`, not here.
- **Host availability (static dependency query only, not a test):**
  `pkg-config --modversion libuv` returned `1.51.0` and `uv.h` is present under
  `/opt/homebrew/include/uv.h`. An existing compile-stage log
  `build/stdlib-tests/async_test.build.log` shows `-luv` and the libuv lib path being
  passed to clang (with "linker input unused" warnings, i.e. compile-only). These facts
  establish that the header's external library is installed; they are **not** evidence of
  any executed behavior.

## 5. APIs lacking any deep test

Per inventory §5 (`docs/stdlib/REGRESSION_INVENTORY.md:154,177`), the **9** unexercised
entries are: `loop.run`, `loop.stop`, `loop.now_ms`, `timer.timer`, `timer.stop`,
`timer.is_active`, `timer.drop`, `sleep_ms`, `uv_error_text`.

Additionally, source inspection shows these public callables are absent from the inventory
altogether and therefore have no deep credit either: `timer.start`
(`stdlib/async.hp:121-135`) and `read_file` (`stdlib/async.hp:180-216`).

**Stronger source-inspection caveat:** no file in the deep corpus (`tests/stdlib/deep_*.cp`)
includes `stdlib/async.hp`; a grep for the distinctive tokens `timer` found no deep file,
and `loop` appears only in unrelated files (`deep_collections_collisions.cp`,
`deep_collections_growth.cp`, `deep_file_lines.cp`, `deep_net.cp`, `deep_net_task5.cp`)
as prose/non-module tokens. Consequently the 5 "exercised" credits
(`docs/stdlib/REGRESSION_INVENTORY.md:106`: `loop.loop`, `loop.valid`, `loop.native`,
`loop.drop` [D], `timer.valid`) are **bare-name lexical collisions** on generic names
`valid(`, `native(`, `loop(` and the `loop` owner token — exactly the upward-bias and
comment-only caveats recorded in the inventory at
`docs/stdlib/REGRESSION_INVENTORY.md:222,225,227`. No deep test exercises this module's
semantics. Shallow tests/demos (`tests/stdlib/async_test.cp`,
`tests/stdlib/examples/async_demo.cp`) exist but are explicitly excluded as evidence
(`docs/stdlib/REGRESSION_INVENTORY.md:12,230`).

## 6. Environment blockers for execution on this host

- **No deep test to execute:** the primary blocker is the absence of any deep-corpus file
  including this header; there is nothing module-specific to run.
- **libuv is installed** (`1.51.0`, see §4), so missing dependency is *not* a blocker here.
  The libuv header/lib discovery is via `/opt/homebrew`; a build would need the same
  pkg-config environment (`stdlib/async.hp:5`).
- **Runtime needs if a test were written:** `read_file` needs a readable filesystem path
  and a writable caller buffer (`stdlib/async.hp:180-216`); timer/loop behavior needs a
  functioning libuv event loop. No display, audio device, or network is required by this
  module.
- **Host-specific paths:** `pkg-config`/Homebrew paths are host-specific; a portable
  invocation should rely on pkg-config rather than hard-coded prefixes (header already
  says "Link with pkg-config libuv", line 5).

I did not run any test, compiler, or host command that executes the module, and make no
claim that any listed behavior was observed.

## 7. What cannot be guaranteed

- Static inspection cannot guarantee that `uv_loop_init`, `uv_timer_init`, `uv_timer_start`,
  `uv_fs_open/read/close`, or `uv_run` succeed on this or any host.
- It cannot guarantee the bounded 1000-iteration drains (`stdlib/async.hp:71-74,169-172`)
  always complete cleanup; a WARN-only failure path exists (`stdlib/async.hp:75-76`).
- It cannot guarantee lifetime safety if a `timer` outlives its borrowed `loop`
  (`stdlib/async.hp:88,169`) or if an RAII object is shallow-copied.
- It cannot guarantee `read_file` returns the whole file (single read, `stdlib/async.hp:207`)
  or that the ignored `uv_fs_close` status (`stdlib/async.hp:213`) is harmless.
- It cannot guarantee correctness of any "exercised" credit: those are lexical collisions,
  not executed module calls (`docs/stdlib/REGRESSION_INVENTORY.md:222,227,234`).
- No execution-based safety, regression, or runtime-success claim is made for this module.
