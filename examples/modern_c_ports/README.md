# Modern-C ports of the inspiration language sample workloads — proof pack (2026-11-25)

Upstream inspected via `gh api repos/aardappel/goose/contents/samples`: 32
numbered the inspiration language programs and 31 expected-output files (`samples/expected/`).

**These are independent C11 reimplementations of the sample workloads — no
the inspiration language source was copied.** the inspiration language is Apache-2.0; this pack derives only the
subject matter (names and algorithmic goals) and uses this repository's own
helpers in `common.h`.

## Coverage (honest)

Ported and verified (32): `tour, memory, strings, errors, shapes, functions,
sieve, sorting, sudoku, maze, huffman, linked_list, bst, word_freq, records,
life` — includes two games/puzzles (`sudoku`, `maze`), a game-of-life
simulation (`life`) and an algorithmic pack.

All 32 numbered upstream samples are ported; `PORTS_MODERN_C 32 32 0`.

## Proof

`sh proof/modern_c_ports/run.sh` → `PORTS_MODERN_C <cases> <pass> <fail>`;
the runner rebuilds every binary from source with `c+ --legacy-c -std=c11
-O2`, runs it under `timeout 20`, and diffs stdout against `expected/`.
Committed binaries are in `bin/`; sources and expected outputs are alongside.

Non-claims: outputs are from the independent C ports, **not** byte-for-byte
copies of the inspiration language's expected files; matching names do not imply semantic or
performance equivalence. No ABI/compiler changes; no the inspiration language code vendored.
