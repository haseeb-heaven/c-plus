# Expanded CLI & game examples — C+ vs safe-Rust stdout/exit comparison

This directory holds one standalone **safe-Rust** model (`.rs`, no `unsafe`, no
FFI, no external crates) for each of the nine NEW expanded examples plus a
single `compare.sh` that builds the original C+ source and the Rust model,
runs both with **identical fixed inputs** under a 30-second timeout, and
compares **stdout bytes and process exit status**.

All numbers and result lines below come from an actual run, not from
synthesis. The captured stdout of the final run is in `.build/stdout.txt`.

## Toolchains (exact, from the captured run)

```
CMD: rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

CMD: ./build/c+ --version 2>&1 | head -1
C+ 0.1.0

CMD: /usr/bin/clang --version | head -2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

CMD: timeout --help 2>&1 | head -1
Usage: timeout [-f | --foreground] [-k time | --kill-after time] [-p | --preserve-status] [-s signal | --signal signal]  [-v | --verbose] <duration> <command> [arg ...]

CMD: uname -m
arm64
```

Repository path for the run:
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`, branch `agent/modern-stdlib`.

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/expanded_cli_games/compare.sh
```

`compare.sh` writes only `EXPANDED_COMPARE ...` lines to stdout; toolchain
banners, build commands, hashes and one final `SUMMARY different=N
inconclusive(BLOCKED+PENDING)=N` line go to stderr. The exact stdout/stderr of
the final run are captured in `.build/stdout.txt` and `.build/stderr.txt`.
Build/run artifacts are kept in
`benchmarks/example_rust_compare/expanded_cli_games/.build/`
(per-example `<name>.c.build.log`, `<name>.rs.build.log`,
`<name>.cases.c.stdout`, `<name>.cases.r.stdout`,
`<name>.cases.c.rc`, `<name>.cases.r.rc`).

## Exact build and run commands used by `compare.sh`

C+ (all nine, cwd = repository root so `stdlib/*.hp` includes resolve):

```sh
timeout 30 ./build/c+ build --strict --sanitize=none -I. \
    -o benchmarks/example_rust_compare/expanded_cli_games/.build/c_<name> \
    examples/expanded/<relpath>/<name>.cp
```

Rust (all nine):

```sh
timeout 30 rustc -O --edition 2021 <name>.rs \
    -o benchmarks/example_rust_compare/expanded_cli_games/.build/r_<name>
```

Run (each fixed case; identical arguments and empty stdin for both sides):

```sh
timeout 30 <binary> <fixed args>   # stdout appended, exit code recorded
```

Comparison: `cmp -s <c.stdout> <r.stdout> && cmp -s <c.rc> <r.rc>`.

## Fixed inputs (identical for both sides)

| Example | Fixed invocations (cases concatenated for the comparison) |
|---|---|
| `todo` | `add 1 alpha -- add 2 beta -- done 1 -- list -- remove 2 -- list` |
| `calculator` | `1+2*3`; `2*(3+4)-5`; `1/0` (divide-by-zero error path) |
| `text_tools` | `words`, `lines`, `rev`, `find bar` over fixture `hello world / foo bar baz / one / two three` |
| `file_tools` | `size` (10-byte file); `cmp` equal; `cmp` different; `copy`; `size` of copy |
| `game2048` | `RDLURD` (compact move sequence; fixed xorshift64* seed) |
| `maze` | `UDDDDRRRRR` (fixed layout, fixed start/goal) |
| `tictactoe` | `01458` (fixed X/O cell sequence -> diagonal win) |
| `snake_core` | no arguments (built-in deterministic self-test) |
| `snake_cli` | `growth R`; `wall RRRRRRRRRRRR`; `self RULDL` |

Fixture files are rewritten with fixed bytes by `compare.sh` on every run, so
no pre-existing file content can influence the result. `stdin` is empty for
both sides.

## Captured result lines (verbatim stdout of `compare.sh`)

```
EXPANDED_COMPARE todo MATCH stdout+exit identical for 1 fixed case(s)
EXPANDED_COMPARE calculator MATCH stdout+exit identical for 3 fixed case(s)
EXPANDED_COMPARE text_tools MATCH stdout+exit identical for 4 fixed case(s)
EXPANDED_COMPARE file_tools MATCH stdout+exit identical for 5 fixed case(s)
EXPANDED_COMPARE game2048 MATCH stdout+exit identical for 1 fixed case(s)
EXPANDED_COMPARE maze MATCH stdout+exit identical for 1 fixed case(s)
EXPANDED_COMPARE tictactoe MATCH stdout+exit identical for 1 fixed case(s)
EXPANDED_COMPARE snake_core MATCH stdout+exit identical for 1 fixed case(s)
EXPANDED_COMPARE snake_cli MATCH stdout+exit identical for 3 fixed case(s)
```

Script exit status: **0**. Final stderr summary line:
`SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0`.

## Exit-status semantics (verified)

`compare.sh` exits nonzero **only on a real `DIFFERENT`**. `BLOCKED` (a C+ or
Rust build failure) and `PENDING` (a scoped source or Rust model not created
yet) are reported as `EXPANDED_COMPARE ...` lines but are deliberately
inconclusive and do **not** fail the script. This was verified with controlled
runs using environment overrides (all isolated in temp dirs, no result files
faked):

| Scenario | Command override | Observed exit | As required |
|---|---|---:|---|
| All MATCH (final run) | none | 0 | yes |
| All `BLOCKED` (`rustc` forced to fail) | `EC_RUSTC=false EC_OUT=/tmp/ec_blocked_test` | 0 | yes — inconclusive must not fail |
| All `DIFFERENT` (stub `rustc` emits wrong stdout) | `EC_RUSTC=/tmp/fake_rustc EC_OUT=/tmp/ec_diff_test` | 9 (one per example) | yes — real DIFFERENT must fail |

The final, real run produced nine `MATCH` lines, exit `0`, and
`SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0`. No `MATCH` was
fabricated: every line came from an actual C+/Rust build and paired execution.

## Per-example table

| Example | C+ source | Rust model | stdout bytes (C / Rust) | Exit codes (C / Rust) | Result |
|---|---|---|---:|---|---|
| `todo` | `examples/expanded/cli/todo.cp` | `todo.rs` | 142 / 142 | 0 | **MATCH** |
| `calculator` | `examples/expanded/cli/calculator.cp` | `calculator.rs` | 82 / 82 | 0,0,1 | **MATCH** |
| `text_tools` | `examples/expanded/cli/text_tools.cp` | `text_tools.rs` | 153 / 153 | 0,0,0,0 | **MATCH** |
| `file_tools` | `examples/expanded/cli/file_tools.cp` | `file_tools.rs` | 86 / 86 | 0,0,0,0,0 | **MATCH** |
| `game2048` | `examples/expanded/games/cores/game2048.cp` | `game2048.rs` | 1126 / 1126 | 0 | **MATCH** |
| `maze` | `examples/expanded/games/cores/maze.cp` | `maze.rs` | 1317 / 1317 | 0 | **MATCH** |
| `tictactoe` | `examples/expanded/games/cores/tictactoe.cp` | `tictactoe.rs` | 616 / 616 | 0 | **MATCH** |
| `snake_core` | `examples/expanded/games/snake/snake_core.cp` | `snake_core.rs` | 24 / 24 | 0 | **MATCH** |
| `snake_cli` | `examples/expanded/games/snake/snake_cli.cp` | `snake_cli.rs` | 8573 / 8573 | 0,0,0 | **MATCH** |

Byte counts are from `wc -c` on the captured combined-stdout files. Combined
SHA-256 of the C and Rust stdout files is identical for every example (e.g.
`todo`: `f48d1a3d1dd0fd822fa23df44e551b560d32a31ebd456b5416e090f818905b51`).

## Totals

| MATCH | DIFFERENT | BLOCKED | PENDING | Total |
|---:|---:|---:|---:|---:|
| 9 | 0 | 0 | 0 | 9 |

No case was untestable, so no `BLOCKED` result was needed. At the time of the
final run all nine scoped C+ sources existed, so no `PENDING` result was
produced.

## `PENDING` behaviour and exact refresh command

`compare.sh` is deliberately safe about sources that are still being created
by sibling agents. If a scoped C+ source (or its Rust model) is absent at run
time, that example is reported as `PENDING` instead of being scored a `MATCH`:

```
EXPANDED_COMPARE <name> PENDING source-not-yet-created <abs path>; refresh: sh <here>/compare.sh
```

Exact command to refresh after a missing source appears:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/expanded_cli_games/compare.sh
```

No `MATCH` is ever fabricated for a missing source: the `PENDING` branch runs
before any build or comparison.

## How each C+ feature was modeled (and what differs)

- **`todo`** — C+ `vector` + `at_checked` + `string::substr`/`field` + `file`
  become `Vec<i32>`/`Vec<String>`, checked indexing and `std::fs`. The chained
  `--` command protocol is reproduced exactly. Difference: C+ bounds/RAII are
  compiler+runtime enforced; Rust uses `Vec` indexing and `String` ownership.
- **`calculator`** — the recursive-descent parser is a direct Rust translation;
  the shift-and-subtract `calc_udiv` (used in C+ to satisfy the strict checker)
  is replaced by Rust `i64 /`, which has identical truncate-toward-zero
  semantics for the bounded magnitudes. Difference: division-by-zero and
  overflow are detected by Rust arithmetic/branching, not a C+ `error *`.
- **`text_tools`** / **`file_tools`** — `file::read_line`/`read` + `error *`
  become `std::fs::read_to_string`/`read` with `Result`; `string::find` becomes
  `str::find`. The "one-over" refusal (file one byte past `FT_CAP`) is modeled
  explicitly. Difference: C+ uses fixed stack buffers with checked copies;
  Rust uses owned `Vec`/`String`.
- **`game2048`** — the xorshift64* generator and `rbelow` multiply-shift use
  `Wrapping<u64>`; the board class becomes a `struct` with a `[i32; 16]`.
  Difference: `printf("%4d")` is reproduced with `print!("{:4}")`, and the C+
  class/`print` machinery has no direct Rust analogue.
- **`maze`** / **`tictactoe`** — hard-coded layouts/plans and traversal are
  translated directly; rendered output is reproduced byte-for-byte. Difference:
  the C+ `class` methods become Rust `impl` methods; no game-engine or UI code
  is involved.
- **`snake_core`** / **`snake_cli`** — the C+ stdlib `vector` body becomes
  `Vec<i32>` (head at index 0); the LCG uses `wrapping_mul`/`wrapping_add`;
  refusals are tracked with a boolean rather than C+ `error`. The embedded
  core is duplicated into both Rust models so each `.rs` stands alone.
  Difference: no C+ `.hp` header lowering, no `error` object, no raw character
  buffer; Rust `String` replaces the caller-provided render buffer.

Every Rust program contains **no `unsafe` blocks, no FFI, no raw pointers and
no external crates**. Each `.rs` compiles standalone with
`rustc -O --edition 2021`.

## Explicit non-equivalence statement

A `MATCH` here means only: **for the fixed, deterministic arguments and empty
stdin used, the original C+ executable and the safe-Rust model produced the
same stdout bytes and the same process exit codes.** It does **not** mean the
two programs are language-, ABI-, implementation- or safety-equivalent. The
Rust files are idiomatic *models of observable behaviour*, not translations of
the C+ lowering pipeline: `.hp` header lowering, checked `vector`/`string`/`file`
wrappers, runtime bounds checks, `error *` categories, C+ RAII and the strict
checker's refusals have no direct Rust syntax analogue and were replaced with
`Vec`/`String`/`Result`, `impl` methods, iterators and Rust's type-system
safety. Memory layout, allocation strategy, overflow diagnostics, error object
semantics and compile-time guarantees were **not** compared. No claim is made
beyond the nine stdout/exit observations above.
