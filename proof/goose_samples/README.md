# Modern-C ports of Goose sample workloads — proof pack (2026-11-25)

Upstream inspected via `gh api repos/aardappel/goose/contents/samples`: 32
numbered Goose programs and 31 expected-output files (`samples/expected/`).

**These are independent C11 reimplementations of the sample workloads — no
Goose source was copied.** Goose is Apache-2.0; this pack derives only the
subject matter (names and algorithmic goals) and uses this repository's own
helpers in `common.h`.

## Coverage (honest)

Ported and verified (16): `tour, memory, strings, errors, shapes, functions,
sieve, sorting, sudoku, maze, huffman, linked_list, bst, word_freq, records,
life` — includes two games/puzzles (`sudoku`, `maze`), a game-of-life
simulation (`life`) and an algorithmic pack.

Not ported here (remaining upstream samples): `bignum, calc, json, vm,
file_tree, gfx_cube, physics_boxes, ui_todo, mini_minecraft, mini_doom,
pool_worker_snapshots, raytrace, image_filters, mandelbrot_threads,
serialize, call_c`. Some are graphics/UI/platform-specific or require Goose
VM/compiler features; this pack does not claim they are covered.

## Proof

`sh proof/goose_samples/run.sh` → `GOOSE_PORT <cases> <pass> <fail>`;
the runner rebuilds every binary from source with `/usr/bin/clang -std=c11
-O2`, runs it under `timeout 20`, and diffs stdout against `expected/`.
Committed binaries are in `bin/`; sources and expected outputs are alongside.

Non-claims: outputs are from the independent C ports, **not** byte-for-byte
copies of Goose's expected files; matching names do not imply semantic or
performance equivalence. No ABI/compiler changes; no Goose code vendored.
