# more_games_a — safe-Rust models vs new games_2 / games_3 examples

Scope: `benchmarks/example_rust_compare/more_games_a/` only. No source files
under `examples/expanded/` were edited; no git add/commit was performed.

## What was compared

For each C+ example that appeared, a **safe Rust** equivalent (`.rs`, no
`unsafe`, no FFI) was written to model the deterministic observable behavior
(stdout bytes + process exit status) for a **fixed set of argv inputs**. Both
sides were built and run under `timeout 30` with identical arguments and empty
stdin (`</dev/null`), then stdout and exit status were compared byte-for-byte.

| name | C+ source | Rust model | fixed cases | result |
|------|-----------|------------|-------------|--------|
| pong | examples/expanded/games_2/pong.cp | pong.rs | 4 | MATCH |
| breakout | examples/expanded/games_2/breakout.cp | breakout.rs | 4 | MATCH |
| invaders | examples/expanded/games_2/invaders.cp | invaders.rs | 4 | MATCH |
| sudoku | examples/expanded/games_3/sudoku.cp | sudoku.rs | 5 | MATCH |
| minesweeper | examples/expanded/games_3/minesweeper.cp | minesweeper.rs | 5 | MATCH |
| nim | examples/expanded/games_3/nim.cp | nim.rs | 5 | MATCH |

Totals: **6 examples, 27 fixed cases, 6 MATCH, 0 DIFFERENT, 0 BLOCKED,
0 PENDING.** Script exit status: 0.

Stdout SHA-256 per example (C+ and Rust identical in every case):

| name | sha256 of compared stdout |
|------|---------------------------|
| pong | ad957b54494f1e3eca3d5cf83a7bf9a8c94c4ed1eed6fa98484212fd562ecdca |
| breakout | 63ef40755d8bfa3f20eae24c375cce5abce23ee23e47bae2aada3eea7eeb3513 |
| invaders | b9bb9930b2eb60a5f0110b95362527ea9568c8309001041c6c00f7031ec99f00 |
| sudoku | ede71a87efd92b4e1537326a72348ace3303e19a6c60f497760e8bf31f6092d7 |
| minesweeper | 1025a2e410db80eb60af46acd401a7e9c90a01e9fac2ff4101843f84482eae5c |
| nim | 2dd9a886e29165ea3ba4520490d07f6b77ab842b3df3d1aaf2b3959cfc7f12de |

## Exact commands / toolchains

Repository root: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`
Branch: `agent/modern-stdlib`

- C+ compiler: `build/c+` — `C+ 0.1.0`, host Clang/LLVM backend.
- Rust compiler: `rustc 1.100.0 (0ef1bc386 2026-11-09)`,
  host `aarch64-apple-darwin`, LLVM 23.1.5.
- Run wrapper: `timeout 30` (BSD/GNU `timeout` on this host).

Per example (run from repository root):

```sh
./build/c+ build --strict --sanitize=none -I. \
    -o benchmarks/example_rust_compare/more_games_a/.build/c_<name> \
    examples/expanded/games_2/<name>.cp        # or games_3/<name>.cp

rustc -O --edition 2021 \
    benchmarks/example_rust_compare/more_games_a/<name>.rs \
    -o benchmarks/example_rust_compare/more_games_a/.build/r_<name>

timeout 30 <bin> <fixed args> </dev/null
```

One-command reproduction (builds both sides, runs all 27 cases, prints the
`MORE_GAMES_COMPARE ...` lines):

```sh
sh benchmarks/example_rust_compare/more_games_a/compare.sh
```

Optional env overrides: `MG_CPLUS`, `MG_RUSTC`, `MG_TIMEOUT`, `MG_OUT`.

## Output contract

The script prints exactly one line per example on stdout:

```
MORE_GAMES_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>
```

- `MATCH` — stdout and exit status identical for that example's fixed cases.
- `DIFFERENT` — stdout or exit mismatch (diff in `.build/<name>.diff`);
  the only status that makes the script exit nonzero.
- `BLOCKED` — a build failed (inconclusive; does not fail the script).
- `PENDING` — a scoped C+ source or Rust model did not exist yet. The line
  carries the exact refresh command, e.g.:

  ```
  MORE_GAMES_COMPARE sudoku PENDING source-not-yet-created <path>; refresh: sh <HERE>/compare.sh
  ```

  A `MATCH` is never fabricated for a missing source or model.

Availability note: `games_2` sources were present when this work began;
`games_3` sources appeared during the run (sudoku, minesweeper, then nim). All
six sources and all six Rust models existed at the final comparison, so no
PENDING remained. Re-running the refresh command above reproduces the table.

## Fixed argv inputs (both sides identical)

- pong: `RLSRR`; `R L S R R`; default (no args); `X` (refusal).
- breakout: `RLSRRLSSLLRRRLSRS`; `RLSR`; `R L S R`; `X`.
- invaders: `LFRFLFSRFLFLSFRFL`; `LFRSF`; `L F R F S F`; `X`.
- sudoku: default; `P 0 2 4 V`; `P 0 0 5 V` (given); `P 9 9 5` (bounds); `X`.
- minesweeper: default; `R 4 4 F 0 0 V`; `R 0 0 V` (loss); `R 9 9`; `F 4 4 F 4 4 V`.
- nim: default; `O T 0 1 O T 1 1 S`; `S`; `T 9 1` (bounds); `X`.

## Explicit non-equivalence statement

**A `MATCH` here means only that the safe-Rust model produces byte-identical
stdout and the same exit status as the C+ example on the finite fixed cases
listed above. It is NOT a claim that the Rust program is equivalent to the C+
program.** The two implementations are deliberately not equivalent:

- The C+ sources use the C+ runtime (`vector`, `string`, `error`,
  `cplus_exit_code`) and a proof/strict-mode compiler pipeline; the Rust models
  use `Vec`, arrays, slices, iterators and plain `String`. No C+ runtime type,
  category code, or compile-time proof behavior is reproduced.
- The Rust models re-express only the deterministic core logic (fixed RNG,
  board state machines, flood fill, nim strategy) and the printf formatting.
  They intentionally reproduce the C+ observable quirks (e.g. pong's
  constructor-before-reseed ordering, the `%+d` sign, trailing spaces in
  sudoku render, move-number differences between nim's builtin and argv modes)
  rather than "fixing" them.
- Behavior on other inputs, undefined-behavior space, performance, memory
  layout, allocators, diagnostics on stderr, and any C+ compile-time safety
  guarantees are **out of scope and not compared**. Only stdout + exit were
  captured; stderr was discarded for both sides.
- Consequently these models are useful as behavioral regression oracles for
  the fixed cases, but they are not drop-in replacements, not a port with
  identical semantics, and not evidence of safety equivalence between the two
  languages or toolchains.
