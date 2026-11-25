# Game/GUI upstreams → C+ standard libraries: honest adoption status (2026-11-25)

Requested upstreams: raylib, SDL, Allegro 5, bgfx, raygui, Nuklear, GTK, LVGL.
**None of these was rewritten in full.** A line-for-line C+ rewrite of eight
mature C/C++ libraries (hundreds of thousands of lines, platform backends and
drivers) cannot be delivered or validated in one change. What is delivered:
one real scoped-memory-style memory adapter (Nuklear) plus an honest, file-level plan
for the rest. No upstream source was copied except Nuklear, which was already
vendored in this repository (MIT, `third_party/nuklear/`).

| Upstream | License / risk | In this repo | C+ decision | scoped-memory-style memory plan |
|---|---|---|---|---|
| Nuklear | MIT, vendored | `third_party/nuklear`, `build/ext/libnuklear.a` | **ADOPT adapter now**: `stdlib/nuklear_adapt.hp` + `tests/stdlib/nuklear_adapt/` (`NK_ADAPT_TEST 1 1 0`) | `nk_allocator` callbacks route to `stdlib_datastack_alloc`; `free` is a no-op; one `stdlib_scratch_scope` drop reclaims the whole frame. Context must be rebuilt per frame. |
| raygui | zlib, header-only | not vendored | DEFER: single-header vendor + checked wrapper | Bind its memory callbacks to the scratch allocator; wrap draw calls in scope. Requires vendoring/license pin first. |
| LVGL | MIT | not vendored | DEFER: `lv_conf.h` allocator override + C+ RAII wrapper | LVGL has allocator hooks (`lv_malloc`/`lv_free`) → route to arena/slot pool; GUI objects need stable handles, not raw pointers. |
| raylib | zlib | not vendored | DEFER: thin C+ RAII/allocator façade, no rewrite | Its internal allocations are not fully injectable; use a frame-scope for transient C+ data only; full rewrite would require reimplementing OpenGL/Vulkan backends. |
| SDL | zlib | not vendored | DEFER: façade only | Large C API with its own allocators; no safe C+ rewrite claim possible. |
| Allegro 5 | MIT-like (check individual files) | not vendored | DEFER: façade only | Same reasoning as SDL. |
| bgfx | Apache-2.0, C++ API | not vendored | DEFER: RAII wrapper only | Owns GPU resources; cannot route its allocations through a C+ VM data stack without a fork. |
| GTK | LGPL (strong copyleft constraints) | not vendored | **REJECT as source rewrite** | FFI façade only if ever needed; do not copy/relicense GTK source into this repo. |

## What "fast memory like Goose" means here

- `stdlib/datastack.hp`: VM-reserved bump region; `stdlib_scratch_scope::drop()`
  restores the top with one store.
- `stdlib/mempool.hp`: stable `stdlib_slot_ref` handles with generation checks;
  growth never invalidates live refs.
- `stdlib/arena.hp`, `stdlib/alloc_iface.hp`: allocator-aware `vector_in`/
  `string_in` so containers can use the scratch allocator instead of malloc.
- Measured: `BENCH_SCOPED 4 4 0` (bump 86 ns/op vs checked heap 261 ns/op on
  this host) and `SCOPED_APPS_BENCH 3 3 0`.

## Non-claims

- No claim that these eight libraries were "rewritten in C+" or match Goose's
  compiler-proven memory model.
- No claim that wrapping a C API makes it memory-safe; third-party code stays
  in `--legacy-c` mode and is not covered by C+ safety guarantees.
- No ABI change to this repo; all work is additive headers/tests/docs.

## Verify

```sh
sh tests/stdlib/nuklear_adapt/run.sh      # NK_ADAPT_TEST 1 1 0
sh tests/edge1000/run_edge1000.sh
python3 tools/gen_api_html.py --check
```
