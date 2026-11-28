# Memory-model audit: safer + modern C+ libraries (2026-11-26)

Goal reviewed: the inspiration language-inspired speed (VM data stacks, bump arenas, stable
pools, no per-object heap in hot paths) with additive checked safety.
**Verdict: fast and safer within documented contracts, NOT 100% safe, NOT
Rust-equivalent.**

| Library | Memory model | Speed evidence | Safety boundary |
|---|---|---|---|
| `stdlib/datastack.hp` | VM-reserved bump stack; `drop()` = one store; realloc commits pages | `BENCH_SCOPED` 30 vs 328 ns/op heap; `SCOPED_APPS` 23 vs 206 ns/op | Raw pointers must not escape a scope (#53 fixed page commit) |
| `stdlib/arena.hp` | Dynamic blocks, reset reclaims all; `take_checked` + `rewind_checked` refuse aliasing | used by scratch containers | Raw `stdlib_arena_alloc()` is unchecked by design; checked wrappers use the lifetime registry (#44 fixed) |
| `stdlib/mempool.hp` | Slab slots; free-list links in separate metadata array | edge1000 pool suites green | Raw released-slot writes no longer corrupt allocator (#43 fixed) |
| `stdlib/scoped_mem.hp` | Unified facade over stack/arena/pool | `SCOPED_MEM_TEST`, `SCOPED_DEMO` green | Same scope-escape limits as raw stack/arena |
| `stdlib/no_heap.hp` | Compile-time macro refusal for raw + C+ allocators | `NO_HEAP_TEST 2 2 0` | Opt-in policy; include before use; does not add borrow checking (#52 fixed) |
| `stdlib/vector.hp`, `string.hp` | Allocator-aware `*_in`; zero-heap constructors; optional heap backing | modern libs benchmark 146 ns/op full workload | Views/slices validated by a shared lifetime registry; `data()` refuses stale owners; `raw_data()` is an explicit unsafe hatch (#42/#50 fixed) |
| `stdlib/web.hp`, `sqldb.hp`, `gameeng.hp` | Stack server handle; fixed slots; fixed world arrays | `APPS 3 3 0`, `GAME_SUITE 4 4 0` | No per-object heap on hot paths; not a safety proof |
| `stdlib/ctl_span.hp`, `nuklear_adapt.hp`, `nuklear_cpp.hp` | Non-owning views / scratch allocator callbacks / class+TRY-CATCH+logs | `CTL_SPAN_TEST 1 1 0`, `NK_ADAPT_TEST 1 1 0`, `NK_CPP_TEST 1 1 0` | `ctl_span_init` refuses stack/untracked storage (#45 fixed); NU-specific scratch frames initialized and exception frames popped (#87/#88 fixed) |
| `stdlib/gfx_sdl3.hp` | Real SDL3 renderer, widget rendering | `GFX_APP_TEST` real framebuffer screenshot | GUI backend uses system SDL3; no C+ memory-safety claim about SDL3 |
| `stdlib/safe/*`, `libc/src/safecrt_vendor` | Checked wrappers, side-table liveness/bounds queries | `MEMSAFE 5 5 0`, libc 110/110 | No pointer-capability ABI; Fil-C technique adapted only as queries; checked heap remains the ABI backing allocator |

## Hardening fixes: issues #211–#219 (2026-11-28)

These nine findings were previously open runtime/bounds gaps. Each is now
fixed in tree with a deterministic regression; the memory-model table above and
the honesty boundaries below are unchanged. These are observable checks on the
tested paths, not a safety proof. Regression files were added by the owning
commits and were not re-run by this docs-only pass.

| Issue | Area | Fix now in code | Regression |
| --- | --- | --- | --- |
| #211 | `libc/src/string/bounded.c` `strncat` | Scan one byte past the proven destination room so an oversized or unterminated source is refused before the raw write, then delegate only the accepted byte count instead of the caller's unclamped `n`. | `tests/libc/test_strncat_bounded_overflow.c` (overflow, exact-fit, no-op) |
| #212 | `stdlib/safe/src/safe_format.c` scanf rewrite | Bound the `scanf` format rewrite and consume Annex K `%c` capacity so the synthesized format cannot outgrow its buffer. | `tests/crt/unit/test_issue212_scanfmt_rewrite.c`, `tests/crt/unit/test_issue212_scanf_c_capacity.c` |
| #213 | `stdlib/safe/src/safe_string.c` | Derive source scan ceilings from the source's own proven capacity (tracked remaining bytes / object size) for `strcpy_s_auto`/`strcat_s_auto` and `cplus_strcmp_s`, rather than the destination-derived `dmax`. Read-side bounds, oversized-scan refusal and refused-empty semantics are preserved. | `tests/crt/unit/test_issue213_source_scan_bound.c` |
| #214 | `stdlib/sqldb.hp` | Bound `sqldb_load` name writes to the destination capacity. | `tests/stdlib/issue214/` |
| #215 | `stdlib/http.hp` | Restrict the initial URL as well as redirects to the allowed http/https list (`CURLOPT_PROTOCOLS_STR` plus `CURLOPT_REDIR_PROTOCOLS_STR`, bounded redirect chain); install the new `CURLOPT_HTTPHEADER` list before freeing the old one so a failed setopt cannot leave a dangling pointer on the handle. | `tests/stdlib/issue215/` |
| #216 | `stdlib/widgets.hp` | Count the sign when computing serialized size and bound label writes. | `tests/stdlib/issue216/` |
| #217 | `stdlib/view_lifetime.hp` | Reuse tombstoned slots on the probe chain (never-used slot as fallback); keep probe chains intact; tokens are never reused and registration fails closed when the counter is exhausted, so stale tokens cannot be revived. | `tests/stdlib/issue217_view_tombstone_reuse.cp` |
| #218 | `stdlib/arena.hp`, `stdlib/datastack.hp`, `stdlib/view_lifetime.hp` | `arena` destroy/reset retire every registered view in their backing block ranges before release/rewind; `arena`/`datastack` realloc validate `old_size` against cursor/top minus `ptr`; `datastack` release validates `size` before subtraction. | `tests/stdlib/issue218_lifetime_bounds.cp` |
| #219 | `stdlib/mempool.hp` | Zero the grown generations tail, pad slot sizes to `STDLIB_ALIGN_DEFAULT` so aligned headers and user pointers stay aligned, and keep stride consistent in slab alloc/release. | `tests/stdlib/issue219_pool_alignment_generations.cp` |

Boundaries unchanged by these fixes: no 100% memory-safety guarantee; no Rust
or Fil-C equivalence; raw-pointer escape hatches (`raw_data()`, `_unsafe`,
low-level arena calls) remain the caller's responsibility; the checked heap is
still the ABI backing allocator; and `stdlib/no_heap.hp` remains an opt-in
source policy, not a compiler proof.

## Heap policy boundary (read this before claiming "no heap")

- Hot paths in the modern C+ libraries use the scoped data stack, bump arenas,
  slot pools and fixed slots. `string_in()` / `vector_in()` take a caller-owned
  allocator; `vector()` and `string` zero-init by value allocate nothing.
- `stdlib/no_heap.hp` makes raw and checked heap calls a compile-time error in
  C+ source when `CPLUS_NO_HEAP` is defined. It is a source policy, not a
  compiler proof: precompiled C libraries and legacy C mode keep the C ABI.
- A few boundary allocations remain intentionally on the checked heap:
  HTTP POST body copies, JSON read buffers, image pixel buffers and the web
  server handle. They are ownership boundaries, not per-object hot-path
  allocations, and are freed by the owning object's `drop()`.
- Buffer-lifetime enforcement is runtime/registry based, not Rust-style
  borrow checking. `raw_data()` / `_unsafe` / low-level arena calls are
  explicit escape hatches and remain the caller's responsibility.

## What "safer" means here

Additive checked APIs: `error*` refusal codes, generation-checked handles,
page-commit on realloc, externalized pool links, compile-time no-heap policy,
strict diagnostics, and adversarial/edge suites. These are observable checks,
not a soundness proof.

## What is NOT claimed

- No 100% memory-safety guarantee; several raw-pointer escape paths are
  documented in `docs/stdlib/SCOPE_ESCAPE_LIMITS.md`.
- No Rust equivalence; Rust won 5/5 head-to-head compute rounds in
  `proof/benchmarks/language_rounds.txt`.
- No the inspiration language compiler analysis (static root inference, checkfree, relative links).
- Checked heap remains the backing allocator; "no heap" is opt-in for C+ source.

## GUI / GFX apps (real windowed SDL3, 2026-11-26)

| App | Widgets | C+ memory path | Boundary |
|---|---|---|---|
| `apps/gui_widgets/` | textbox, dropdown, list, buttons | SDL3 renderer; C+ GFX facade | SDL3 owns its own heap; no C+ safety claim about SDL3 |
| `apps/memory_lab_gui/` | textbox, dropdown, selectable list, buttons; live keyboard/mouse input | `stdlib_scoped_scope` + `stdlib_scoped_stack_current()` for each frame's transient line; fixed slot state in `app_state`; one-store release | Real framebuffer capture (`screenshots/memory_lab.png`); external SDL3 heap remains outside the C+ policy |

`sh apps/gui_widgets/run.sh` → `GFX_APP_TEST 3 3 0`; `sh apps/memory_lab_gui/run.sh` →
`MEMORY_LAB_TEST 2 2 0`. These are real windowed renderer outputs, not CLI
transcripts. They demonstrate scoped-memory-style scoped transient allocation inside a
real GUI app, not a 100% memory-safety guarantee for external SDL3 code.
