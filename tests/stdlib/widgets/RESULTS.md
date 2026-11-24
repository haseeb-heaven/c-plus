# Headless widget-toolkit core — results

Branch `agent/modern-stdlib`.  Files delivered (no other repository files
touched, no `git add` / commit):

- `stdlib/widgets.hp` — additive, C-syntax, allocator-aware header.
- `tests/stdlib/widgets/test_widgets.cp` — deterministic C+ test program.
- `tests/stdlib/widgets/run.sh` — bounded build/run/determinism runner.
- `tests/stdlib/widgets/RESULTS.md` — this file.

## API summary

`stdlib_widget { id, kind, label[64], x, y, w, h, flags }`;
`stdlib_window` owns an allocator-aware widget vector plus grid configuration
(`x, y, w, h, columns`).  All helpers are `static inline` (no exported
symbols).  Functions:

- lifecycle: `stdlib_window_init`, `stdlib_window_drop`, `stdlib_window_count`
- mutation: `stdlib_widget_add`, `stdlib_widget_label`,
  `stdlib_widget_set_bounds`, `stdlib_widget_enable`, `stdlib_widget_visible`
- lookup: `stdlib_widget_find_by_id`, `stdlib_widget_index_by_id`
- core: `stdlib_window_layout`, `stdlib_window_hit_test`,
  `stdlib_window_serialize`, `stdlib_window_serialized_size`

Semantics that the tests pin down:

- layout: integer row-major grid, 1-pixel cell inset, fixed point across
  repeated calls; hidden/disabled widgets keep their deterministic cell;
- hit test: only enabled + visible widgets, topmost (highest index) wins on
  overlap; `1` hit / `0` miss / `STDLIB_FAIL` on bad arguments;
- serialization: `WIDGETS <count>\n` followed by
  `W <id> <kind> <x> <y> <w> <h> <flags> <label>\n` lines; exact size is
  computed before writing, so a short buffer leaves the destination untouched;
- refusals (all through `error *`, window/data unchanged): NULL window or
  output pointer, non-positive or overflowing bounds, duplicate ids, labels
  that are NULL/oversized/contain control characters, out-of-range indices,
  invalid columns/geometry, undersized cell, too-small serialization buffer,
  allocation refusal.

## Exact commands

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh -n tests/stdlib/widgets/run.sh                      # rc=0
sh tests/stdlib/widgets/run.sh                         # rc=0
```

The runner builds with the requested mode and archive argument:

```sh
./build/c+ build tests/stdlib/widgets/test_widgets.cp \
    --default --sanitize=none -I. \
    -o tests/stdlib/widgets/results/test_widgets \
    -- build/libc/libc/libcplus_hardened.a
```

The task-specified archive path `build/libc/libc/libcplus_hardened.a` is not
present in this tree; the runner prefers it when present and falls back to the
actual `build/libc/libcplus_hardened.a` (verified present).  `-o` is placed
before `--` because arguments after `--` go straight to the C backend (the
driver otherwise writes `./test_widgets`).  With neither archive present the
driver's own runtime link is sufficient; the runner prints a note in that case.

## Final run (stdout)

```
BUILD ok: ./build/c+ build test_widgets.cp --default --sanitize=none -I. -- build/libc/libcplus_hardened.a
RUN ok: WIDGETS 144 0 (stdout byte-identical across 2 runs, stderr empty)
WIDGETS SUITE PASS
```

Program stdout (both runs, byte-identical):

```
WIDGETS 144 0
```

Both `run1.stderr` and `run2.stderr` are zero bytes; both binaries exited 0.

## What the 144 checks cover

- **Layout**: exact serialized grid for 5 widgets in a 100x50 / 2-column
  window; repeated layout produces identical bytes; layout of an empty window
  succeeds; a too-small cell is refused with the widget bounds unchanged.
- **Hit testing**: cell centres, inset border miss, outside miss, empty-window
  miss, disabled/hidden misses, topmost overlap, NULL output refusal, and
  extreme rects with `x + w == INT_MAX + 1` (no signed overflow).
- **Serialization**: exact expected text before/after flag and label changes,
  exact `serialized_size`, tail canary untouched, exact-size buffer accepted,
  one-byte-short buffer refused with the whole buffer untouched, NULL buffer
  and NULL size output refused, negative coordinates and `INT_MAX` id printed
  verbatim.
- **Refusals**: every public entry point with a NULL window, duplicate ids,
  zero/negative/overflowing bounds (add and set_bounds), NULL/oversized/
  newline labels, out-of-range indices for all four index-based mutators,
  invalid window configuration, null hit-test output.
- **Canaries**: 64-byte guard arrays before and after the `stdlib_window`
  struct are checked after mutations, after drop and after double drop.  A
  guarded serialization buffer verifies no writes past the emitted text.
- **Allocator awareness**: a counting allocator vtable verifies lazy
  allocation, one growth reallocation for 8 widgets, one-shot failure
  injection leaving count and widget bytes unchanged, retry success, reset
  dispatch, exactly one release on drop and none on double drop.

## Build diagnostics (non-fatal, in `--default` mode)

`tests/stdlib/widgets/results/build.log` contains 27 warnings and no errors:
18 `CPLUS-LIFETIME` warnings for the deliberate NULL-window arguments used to
verify refusals, 6 `CPLUS-ARITH` warnings where the analyser cannot prove the
dynamic column count is nonzero despite the explicit guard, 2 `CPLUS-ALIAS`
warnings from existing `stdlib/error.hp` / `stdlib/alloc_iface.hp` realloc
paths, and one clang “linker input unused” note for the explicitly supplied
archive (the C+ driver already links its runtime).  None affects the result:
build and both runs exit 0 with empty run stderr.

Artifacts are written only under `tests/stdlib/widgets/results/` (gitignored).
No formatting tool was applied: C++ formatting does not apply to these C+ C
sources.
