# Memory-model audit: safer + modern C+ libraries (2026-11-26)

Goal reviewed: the inspiration language-inspired speed (VM data stacks, bump arenas, stable
pools, no per-object heap in hot paths) with additive checked safety.
**Verdict: fast and safer within documented contracts, NOT 100% safe, NOT
Rust-equivalent.**

| Library | Memory model | Speed evidence | Safety boundary |
|---|---|---|---|
| `stdlib/datastack.hp` | VM-reserved bump stack; `drop()` = one store; realloc commits pages | `BENCH_SCOPED` 30 vs 328 ns/op heap; `SCOPED_APPS` 23 vs 206 ns/op | Raw pointers must not escape a scope (#53 fixed page commit) |
| `stdlib/arena.hp` | Dynamic blocks, reset reclaims all | used by scratch containers | Raw arena pointers stale after rewind (documented, #44) |
| `stdlib/mempool.hp` | Slab slots; free-list links in separate metadata array | edge1000 pool suites green | Raw released-slot writes no longer corrupt allocator (#43 fixed) |
| `stdlib/scoped_mem.hp` | Unified facade over stack/arena/pool | `SCOPED_MEM_TEST`, `SCOPED_DEMO` green | Same scope-escape limits as raw stack/arena |
| `stdlib/no_heap.hp` | Compile-time macro refusal for raw + C+ allocators | `NO_HEAP_TEST 2 2 0` | Policy only; must include after headers; does not add borrow checking (#52 fixed) |
| `stdlib/vector.hp`, `string.hp` | Allocator-aware `*_in`; optional heap backing | modern libs benchmark 146 ns/op full workload | Views/slices are non-owning; do not outlive owner (#42/#50 documented) |
| `stdlib/web.hp`, `sqldb.hp`, `gameeng.hp` | Stack server handle; fixed slots; fixed world arrays | `APPS 3 3 0`, `GAME_SUITE 4 4 0` | No per-object heap on hot paths; not a safety proof |
| `stdlib/ctl_span.hp`, `nuklear_adapt.hp`, `nuklear_cpp.hp` | Non-owning views / scratch allocator callbacks / class+TRY-CATCH+logs | `CTL_SPAN_TEST 1 1 0`, `NK_ADAPT_TEST 1 1 0`, `NK_CPP_TEST 1 1 0` | Views require owner lifetime; stale raw refs refused only via pool handles |
| `stdlib/gfx_sdl3.hp` | Real SDL3 renderer, widget rendering | `GFX_APP_TEST` real framebuffer screenshot | GUI backend uses system SDL3; no C+ memory-safety claim about SDL3 |
| `stdlib/safe/*`, `libc/src/safecrt_vendor` | Checked wrappers, side-table liveness/bounds queries | `MEMSAFE 5 5 0`, libc 109/109 | No pointer-capability ABI; Fil-C technique adapted only as queries |

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
