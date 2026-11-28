# New example-vs-Rust comparisons — aggregate index

This file catalogues the **new** expanded-example comparisons only: the three
groups created for the `games_2`/`games_3`, `games_4`/`apps_2`/`systems_2` and
`data_2`/`math_2` example batches. It contains **no fabricated verdicts**:
every row below is transcribed from that group's `RESULTS.md`, which was
present and read at the time of writing. The older comparison campaign (demos,
CRT, apps/system/games, and the earlier `expanded_*` groups) is indexed
separately in `ALL_EXAMPLES.md` and is out of scope here.

Paths are relative to the repository root unless stated otherwise; the
`Rust model` column is relative to `benchmarks/example_rust_compare/`.

## Headline counts

| Group | Scoped example dirs | Examples | Fixed cases | MATCH | DIFFERENT | BLOCKED | PENDING | Script exit |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| `more_games_a` | `games_2`, `games_3` | 6 | 27 | 6 | 0 | 0 | 0 | 0 |
| `more_apps_a` | `games_4`, `apps_2`, `systems_2` | 11 | 40 | 11 | 0 | 0 | 0 | 0 |
| `more_data_math` | `data_2`, `math_2` | 8 | 38 | 8 | 0 | 0 | 0 | 0 |
| **TOTAL** | | **25** | **105** | **25** | **0** | **0** | **0** | (all 0) |

All three `RESULTS.md` files were present at the time of the final read, so
**no group is marked PENDING** in this index. See the refresh section below for
the exact command to re-read or regenerate any group.

## Toolchains (identical across the three groups)

```
CMD: rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

CMD: ./build/c+ --version 2>&1 | head -1
C+ 0.1.0

CMD: /usr/bin/clang --version | head -1/2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0   (more_apps_a records this line; uname -m = arm64)

CMD: timeout --help 2>&1 | head -1
Usage: timeout [-f | --foreground] [-k time | --kill-after time] [-p | --preserve-status] [-s signal | --signal signal]  [-v | --verbose] <duration> <command> [arg ...]
```

Repository root for every run:
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`, branch `agent/modern-stdlib`.

All three harnesses build C+ with
`./build/c+ build --strict --sanitize=none -I. -o <bin> <source.cp>` (from the
repository root), build Rust with `rustc -O --edition 2021 <name>.rs -o <bin>`,
run both sides under `timeout 30` with identical argv and empty stdin, and
compare **stdout bytes and process exit status** with `cmp -s`. A `MATCH` means
only that those two observations agreed for the listed fixed cases.

---

## Group: `more_games_a` (`games_2`, `games_3`)

Authoritative detail: `more_games_a/RESULTS.md`. Output protocol:
`MORE_GAMES_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>`.

| # | Example | C+ source | Rust model | Fixed cases | Verdict |
|---:|---|---|---|---:|---|
| 1 | `pong` | `examples/expanded/games_2/pong.cp` | `more_games_a/pong.rs` | 4 | **MATCH** |
| 2 | `breakout` | `examples/expanded/games_2/breakout.cp` | `more_games_a/breakout.rs` | 4 | **MATCH** |
| 3 | `invaders` | `examples/expanded/games_2/invaders.cp` | `more_games_a/invaders.rs` | 4 | **MATCH** |
| 4 | `sudoku` | `examples/expanded/games_3/sudoku.cp` | `more_games_a/sudoku.rs` | 5 | **MATCH** |
| 5 | `minesweeper` | `examples/expanded/games_3/minesweeper.cp` | `more_games_a/minesweeper.rs` | 5 | **MATCH** |
| 6 | `nim` | `examples/expanded/games_3/nim.cp` | `more_games_a/nim.rs` | 5 | **MATCH** |

Totals: **6 MATCH, 0 DIFFERENT, 0 BLOCKED, 0 PENDING** across **27 fixed
cases**; script exit status **0**. Stdout SHA-256 (C+ and Rust identical in
every case, from `RESULTS.md`):

| Example | SHA-256 of compared stdout |
|---|---|
| `pong` | `ad957b54494f1e3eca3d5cf83a7bf9a8c94c4ed1eed6fa98484212fd562ecdca` |
| `breakout` | `63ef40755d8bfa3f20eae24c375cce5abce23ee23e47bae2aada3eea7eeb3513` |
| `invaders` | `b9bb9930b2eb60a5f0110b95362527ea9568c8309001041c6c00f7031ec99f00` |
| `sudoku` | `ede71a87efd92b4e1537326a72348ace3303e19a6c60f497760e8bf31f6092d7` |
| `minesweeper` | `1025a2e410db80eb60af46acd401a7e9c90a01e9fac2ff4101843f84482eae5c` |
| `nim` | `2dd9a886e29165ea3ba4520490d07f6b77ab842b3df3d1aaf2b3959cfc7f12de` |

Fixed inputs (full detail in `RESULTS.md`): `pong` `RLSRR` / `R L S R R` /
default / `X` refusal; `breakout` `RLSRRLSSLLRRRLSRS` / `RLSR` / `R L S R` /
`X`; `invaders` `LFRFLFSRFLFLSFRFL` / `LFRSF` / `L F R F S F` / `X`; `sudoku`
default / `P 0 2 4 V` / `P 0 0 5 V` / `P 9 9 5` / `X`; `minesweeper` default /
`R 4 4 F 0 0 V` / `R 0 0 V` / `R 9 9` / `F 4 4 F 4 4 V`; `nim` default /
`O T 0 1 O T 1 1 S` / `S` / `T 9 1` / `X`.

---

## Group: `more_apps_a` (`games_4`, `apps_2`, `systems_2`)

Authoritative detail: `more_apps_a/RESULTS.md`. Output protocol:
`MORE_APPS_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>`.

| # | Example | C+ source | Rust model | Fixed cases | Verdict |
|---:|---|---|---|---:|---|
| 1 | `connect4` | `examples/expanded/games_4/connect4.cp` | `more_apps_a/connect4.rs` | 5 | **MATCH** |
| 2 | `checkers` | `examples/expanded/games_4/checkers.cp` | `more_apps_a/checkers.rs` | 4 | **MATCH** |
| 3 | `chesslite` | `examples/expanded/games_4/chesslite.cp` | `more_apps_a/chesslite.rs` | 4 | **MATCH** |
| 4 | `vault` | `examples/expanded/apps_2/vault.cp` | `more_apps_a/vault.rs` | 2 | **MATCH** |
| 5 | `json_query` | `examples/expanded/apps_2/json_query.cp` | `more_apps_a/json_query.rs` | 5 | **MATCH** |
| 6 | `csv_tool` | `examples/expanded/apps_2/csv_tool.cp` | `more_apps_a/csv_tool.rs` | 4 | **MATCH** |
| 7 | `matrix` | `examples/expanded/apps_2/matrix.cp` | `more_apps_a/matrix.rs` | 4 | **MATCH** |
| 8 | `lru_cache` | `examples/expanded/systems_2/lru_cache.cp` | `more_apps_a/lru_cache.rs` | 3 | **MATCH** |
| 9 | `scheduler` | `examples/expanded/systems_2/scheduler.cp` | `more_apps_a/scheduler.rs` | 3 | **MATCH** |
| 10 | `event_bus` | `examples/expanded/systems_2/event_bus.cp` | `more_apps_a/event_bus.rs` | 3 | **MATCH** |
| 11 | `ring_buffer` | `examples/expanded/systems_2/ring_buffer.cp` | `more_apps_a/ring_buffer.rs` | 3 | **MATCH** |

Totals: **11 MATCH, 0 DIFFERENT, 0 BLOCKED, 0 PENDING** across **40 fixed
cases**; script exit status **0**. Byte counts and abbreviated stdout SHA-256
per example are in the per-example table of `more_apps_a/RESULTS.md`; the full
hashes are in `more_apps_a/out/final.stderr`. Note that `json_query` is the one
build that adds `-Ithird_party/yyjson -Lbuild/ext -lyyjson` (see its group
`RESULTS.md`). Fixed inputs are enumerated there; the CSV fixture is rewritten
with fixed bytes on every run.

---

## Group: `more_data_math` (`data_2`, `math_2`)

Authoritative detail: `more_data_math/RESULTS.md`. Output protocol:
`MORE_DATA_COMPARE <name> MATCH|DIFFERENT|BLOCKED|PENDING <reason>`.

| # | Example | C+ source | Rust model | Fixed cases | Verdict |
|---:|---|---|---|---:|---|
| 1 | `sort_tool` | `examples/expanded/data_2/sort_tool.cp` | `more_data_math/sort_tool.rs` | 5 | **MATCH** |
| 2 | `hash_tool` | `examples/expanded/data_2/hash_tool.cp` | `more_data_math/hash_tool.rs` | 3 | **MATCH** |
| 3 | `rle` | `examples/expanded/data_2/rle.cp` | `more_data_math/rle.rs` | 3 | **MATCH** |
| 4 | `regex_lite` | `examples/expanded/data_2/regex_lite.cp` | `more_data_math/regex_lite.rs` | 4 | **MATCH** |
| 5 | `bigint` | `examples/expanded/math_2/bigint.cp` | `more_data_math/bigint.rs` | 7 | **MATCH** |
| 6 | `prng` | `examples/expanded/math_2/prng.cp` | `more_data_math/prng.rs` | 6 | **MATCH** |
| 7 | `checksum` | `examples/expanded/math_2/checksum.cp` | `more_data_math/checksum.rs` | 3 | **MATCH** |
| 8 | `base64` | `examples/expanded/math_2/base64.cp` | `more_data_math/base64.rs` | 7 | **MATCH** |

Totals: **8 MATCH, 0 DIFFERENT, 0 BLOCKED, 0 PENDING** across **38 fixed
cases**; script exit status **0**. Stdout SHA-256 (C+ and Rust identical, from
`more_data_math/RESULTS.md`):

| Example | SHA-256 of compared stdout |
|---|---|
| `sort_tool` | `f2ec48337e491a79e3166470fdd894e09495ec62834b00c171c22621601fb116` |
| `hash_tool` | `7be3d5a186d5e16695c85c7b9624dffcddce301dacba364b50bf9425970a3256` |
| `rle` | `f957bb18a18f364856a90f112ed6b358405f9079036a66a7760ca5eca53c871e` |
| `regex_lite` | `d78ccf8819e5487f63be93c1781c41cfd33c6705a7ef2c372594061bd56765f0` |
| `bigint` | `407fecbb4137ddc7f12f1a160e170bafe8d6d3f1ddb45999b578c26910d187f1` |
| `prng` | `fb2f40ada923f4e613b8c6cf28a64ce4d17862748bd13e1d48e782d66946a12e` |
| `checksum` | `abd25520ab27061bf63d0012c89e8ff2fc3f179a1309a521a043b78fdbf7a9f2` |
| `base64` | `e00ec8dcfda9b87842f8de3219df2dd9c4475d6b859e41ffba20e8bf135d5a5f` |

Fixed inputs (full detail in `RESULTS.md`): `sort_tool` exercises int/str,
asc/desc and an integer-overflow refusal; `hash_tool` hashes 3 / 1 tokens plus
an empty-input refusal; `rle` does `enc aaabccdddd`, `dec RLE1;a03;b01;c02;d04`
and a bad-magic refusal; `regex_lite` runs its built-in 6-case table, `a?c
abc`, `a*b aaac` and an invalid-pattern refusal; `bigint` runs its 5-vector
table plus all six refusal modes; `prng` runs the default seed/count, two
explicit seeds and three refusal paths; `checksum` runs its 4-vector table,
`text hello` and `oversized`; `base64` runs its 7 RFC vectors, `enc`/`dec`, two
malformed inputs and two oversized refusals.

---

## Refresh / `PENDING` protocol

If a group's `RESULTS.md` is absent when this index is refreshed, that group
must be reported as `PENDING` (with the exact refresh command below) rather
than scored from memory. The same applies per example inside a group: every
harness emits a `... PENDING source-not-yet-created <path>; refresh: sh
<HERE>/compare.sh` line for a scoped C+ source or Rust model that does not yet
exist, and never fabricates a `MATCH` for it. At the final read all three
`RESULTS.md` files and all 25 scoped sources/models were present.

Exact commands to refresh each group (from the repository root):

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/more_games_a/compare.sh    # rebuilds+reruns; raw evidence in more_games_a/.build/
sh benchmarks/example_rust_compare/more_apps_a/compare.sh     # rebuilds+reruns; raw evidence in more_apps_a/out/
sh benchmarks/example_rust_compare/more_data_math/compare.sh  # rebuilds+reruns; raw evidence in more_data_math/.build/
```

Environment overrides: `MG_*` (`more_games_a`), `MA_*` (`more_apps_a`) and
`EC_*` (`more_data_math`) for compiler/timeout/output paths; consult each
group's `RESULTS.md` for the exact variable names.

All three harnesses exit nonzero **only** on a real `DIFFERENT`; `BLOCKED` and
`PENDING` are reported but inconclusive. The controlled verification runs in
each group's `RESULTS.md` confirm this (forced build failure → exit 0, stub
compiler with wrong stdout → one nonzero per example).

## Explicit non-equivalence statement

**Every `MATCH` above means observable behavior parity for the tested inputs
only — it is NOT a Rust-equivalence claim and NOT a safety claim.**

- For each listed example, at the fixed deterministic argv sequences and empty
  stdin used, the C+ executable and the safe-Rust model produced identical
  stdout bytes and identical process exit codes. Nothing is claimed about
  untested inputs, longer executions, stderr, timing or performance.
- The Rust files are idiomatic *models of observable behaviour*, not ports of
  the C+ lowering pipeline. C+ runtime constructs (`vector`, `string`, `file`,
  `strmap`, `deque`, `error *`, `cplus_exit_code`, `.hp` header lowering, strict
  checker refusals, RAII and runtime bounds checks) have no direct Rust syntax
  analogue and were replaced with `Vec`/`String`/`Result`/`Option`/`HashMap`,
  arrays, slices, iterators and Rust's compile-time safety.
- The games models re-express deterministic cores only (fixed RNGs, board state
  machines, flood fill, nim strategy); the apps/models use safe parsers,
  ordered collections and explicit counters; the data/math models re-express
  bounded sort/hash/RLE/wildcard-match and bigint/PRNG/CRC/Adler/Base64 logic.
  Memory layout, allocator strategy, overflow diagnostics, error-object
  semantics, yyjson internals and compile-time guarantees were **not**
  compared.
- An aggregate total of 25 MATCH / 0 DIFFERENT is a statement about 105
  stdout/exit observations, not about language or toolchain equivalence.
