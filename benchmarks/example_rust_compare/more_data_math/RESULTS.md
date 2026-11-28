# New data_2 / math_2 expanded examples — C+ vs safe-Rust stdout/exit comparison

This directory holds one standalone **safe-Rust** model (`.rs`, no `unsafe`, no
FFI, no external crates) for each of the eight NEW expanded data/math examples
plus a single `compare.sh` that builds the original C+ source and the Rust
model, runs both with **identical fixed inputs** under a 30-second timeout, and
compares **stdout bytes and process exit status**.

The eight examples are:

- `examples/expanded/data_2/`: `sort_tool.cp`, `hash_tool.cp`, `rle.cp`,
  `regex_lite.cp`
- `examples/expanded/math_2/`: `bigint.cp`, `prng.cp`, `checksum.cp`,
  `base64.cp`

All numbers and result lines below come from an actual run, not from
synthesis. The captured stdout of the final run is in `.build/stdout.txt`.

## Toolchains (exact, from the captured run)

```
CMD: rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

CMD: ./build/c+ --version 2>&1 | head -1
C+ 0.1.0

CMD: /usr/bin/clang --version | head -1
Apple clang version 16.0.0 (clang-1600.0.26.6)

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
sh benchmarks/example_rust_compare/more_data_math/compare.sh
```

`compare.sh` writes only `MORE_DATA_COMPARE ...` lines to stdout; toolchain
banners, build commands, hashes and one final `SUMMARY different=N
inconclusive(BLOCKED+PENDING)=N` line go to stderr. The exact stdout/stderr of
the final run are captured in `.build/stdout.txt` and `.build/stderr.txt`.
Build/run artifacts are kept in
`benchmarks/example_rust_compare/more_data_math/.build/`
(per-example `<name>.c.build.log`, `<name>.rs.build.log`,
`<name>.cases.c.stdout`, `<name>.cases.r.stdout`,
`<name>.cases.c.rc`, `<name>.cases.r.rc`, `<name>.diff`).

## Exact build and run commands used by `compare.sh`

C+ (all eight, cwd = repository root so `stdlib/*.hp` includes resolve):

```sh
timeout 30 ./build/c+ build --strict --sanitize=none -I. \
    -o benchmarks/example_rust_compare/more_data_math/.build/c_<name> \
    examples/expanded/<relpath>/<name>.cp
```

Rust (all eight):

```sh
timeout 30 rustc -O --edition 2021 <name>.rs \
    -o benchmarks/example_rust_compare/more_data_math/.build/r_<name>
```

Run (each fixed case; identical arguments and empty stdin for both sides):

```sh
timeout 30 <binary> <fixed args>   # stdout appended, exit code recorded
```

Comparison: `cmp -s <c.stdout> <r.stdout> && cmp -s <c.rc> <r.rc>`.

## Fixed inputs (identical for both sides)

| Example | Fixed invocations (cases concatenated for the comparison) |
|---|---|
| `sort_tool` | `int asc 5 3 9 3 1`; `int desc 5 3 9 3 1`; `str asc banana apple cherry apple`; `str desc banana apple cherry apple`; `int asc 1000001` (integer-overflow refusal) |
| `hash_tool` | `hello world foo`; `hello`; `""` (empty-input refusal) |
| `rle` | `enc aaabccdddd`; `dec RLE1;a03;b01;c02;d04`; `dec BAD` (bad-magic refusal) |
| `regex_lite` | no args (built-in 6-case table); `a?c abc`; `a*b aaac`; `[ abc` (invalid-pattern refusal) |
| `bigint` | no args (5 fixed vectors); `input-overflow`; `result-overflow`; `sub-underflow`; `mul-overflow`; `bad-digit`; `bad-mode` |
| `prng` | no args (seed 305419896, 16 values); `1 8`; `4294967295 5`; `4294967296 5` (seed-overflow); `1 257` (count-too-large); `x 5` (seed-syntax) |
| `checksum` | no args (4 fixed vectors); `text hello`; `oversized` (129-byte refusal) |
| `base64` | no args (7 RFC vectors); `enc foobar`; `dec Zm9vYmFy`; `malformed !!!!`; `badlen abc`; `oversized`; `encoversized` |

`stdin` is empty for both sides. Both sides use the same argv tokens; refusal
paths are included deliberately so the error transcripts are compared too.

## Captured result lines (verbatim stdout of `compare.sh`)

```
MORE_DATA_COMPARE sort_tool MATCH stdout+exit identical for 5 fixed case(s)
MORE_DATA_COMPARE hash_tool MATCH stdout+exit identical for 3 fixed case(s)
MORE_DATA_COMPARE rle MATCH stdout+exit identical for 3 fixed case(s)
MORE_DATA_COMPARE regex_lite MATCH stdout+exit identical for 4 fixed case(s)
MORE_DATA_COMPARE bigint MATCH stdout+exit identical for 7 fixed case(s)
MORE_DATA_COMPARE prng MATCH stdout+exit identical for 6 fixed case(s)
MORE_DATA_COMPARE checksum MATCH stdout+exit identical for 3 fixed case(s)
MORE_DATA_COMPARE base64 MATCH stdout+exit identical for 7 fixed case(s)
```

Script exit status: **0**. Final stderr summary line:
`SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0`.

## Per-example table

| Example | C+ source | Rust model | stdout bytes (C / Rust) | Exit codes (C / Rust) | SHA-256 (combined stdout, both sides) | Result |
|---|---|---|---:|---|---|---|
| `sort_tool` | `examples/expanded/data_2/sort_tool.cp` | `sort_tool.rs` | 1174 / 1174 | 0,0,0,0,2 | `f2ec48337e491a79e3166470fdd894e09495ec62834b00c171c22621601fb116` | **MATCH** |
| `hash_tool` | `examples/expanded/data_2/hash_tool.cp` | `hash_tool.rs` | 449 / 449 | 0,0,2 | `7be3d5a186d5e16695c85c7b9624dffcddce301dacba364b50bf9425970a3256` | **MATCH** |
| `rle` | `examples/expanded/data_2/rle.cp` | `rle.rs` | 635 / 635 | 0,0,2 | `f957bb18a18f364856a90f112ed6b358405f9079036a66a7760ca5eca53c871e` | **MATCH** |
| `regex_lite` | `examples/expanded/data_2/regex_lite.cp` | `regex_lite.rs` | 766 / 766 | 0,0,0,2 | `d78ccf8819e5487f63be93c1781c41cfd33c6705a7ef2c372594061bd56765f0` | **MATCH** |
| `bigint` | `examples/expanded/math_2/bigint.cp` | `bigint.rs` | 486 / 486 | 0,1,1,1,1,1,1,1 | `407fecbb4137ddc7f12f1a160e170bafe8d6d3f1ddb45999b578c26910d187f1` | **MATCH** |
| `prng` | `examples/expanded/math_2/prng.cp` | `prng.rs` | 1174 / 1174 | 0,0,0,1,1,1 | `fb2f40ada923f4e613b8c6cf28a64ce4d17862748bd13e1d48e782d66946a12e` | **MATCH** |
| `checksum` | `examples/expanded/math_2/checksum.cp` | `checksum.rs` | 401 / 401 | 0,0,1 | `abd25520ab27061bf63d0012c89e8ff2fc3f179a1309a521a043b78fdbf7a9f2` | **MATCH** |
| `base64` | `examples/expanded/math_2/base64.cp` | `base64.rs` | 552 / 552 | 0,0,0,1,1,1,1 | `e00ec8dcfda9b87842f8de3219df2dd9c4475d6b859e41ffba20e8bf135d5a5f` | **MATCH** |

Byte counts are from `wc -c` on the captured combined-stdout files; the
SHA-256 values are identical for the C and Rust stdout file of every example.

## Totals

| MATCH | DIFFERENT | BLOCKED | PENDING | Total |
|---:|---:|---:|---:|---:|
| 8 | 0 | 0 | 0 | 8 |

No case was untestable, so no `BLOCKED` result was needed. At the time of the
final run all eight scoped C+ sources existed, so no `PENDING` result was
produced.

## Exit-status semantics (verified)

`compare.sh` exits nonzero **only on a real `DIFFERENT`**. `BLOCKED` (a C+ or
Rust build failure) and `PENDING` (a scoped source or Rust model not created
yet) are reported as `MORE_DATA_COMPARE ...` lines but are deliberately
inconclusive and do **not** fail the script. This was verified with controlled
runs using environment overrides (all isolated in a temp directory, no result
files faked):

| Scenario | Command override | Observed exit | As required |
|---|---|---:|---|
| All MATCH (final run) | none | 0 | yes |
| All `BLOCKED` (`rustc` forced to fail) | `EC_RUSTC=false EC_OUT=<temp>/blocked` | 0 | yes — inconclusive must not fail |
| All `DIFFERENT` (stub `rustc` emits wrong stdout) | `EC_RUSTC=<temp>/fake_rustc EC_OUT=<temp>/different` | 8 (one per example) | yes — real DIFFERENT must fail |

The final, real run produced eight `MATCH` lines, exit `0`, and
`SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0`. No `MATCH` was
fabricated: every line came from an actual C+/Rust build and paired execution.

## `PENDING` behaviour and exact refresh command

`compare.sh` is deliberately safe about sources that are still being created
by sibling agents. If a scoped C+ source (or its Rust model) is absent at run
time, that example is reported as `PENDING` instead of being scored a `MATCH`:

```
MORE_DATA_COMPARE <name> PENDING source-not-yet-created <abs path>; refresh: sh <here>/compare.sh
```

Exact command to refresh after a missing source appears:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/more_data_math/compare.sh
```

No `MATCH` is ever fabricated for a missing source: the `PENDING` branch runs
before any build or comparison.

## How each C+ feature was modeled (and what differs)

- **`sort_tool`** — C+ `vector` + `string` + fixed buffers become `Vec<i32>` /
  `Vec<Vec<u8>>`; the stable insertion sort over a permutation is translated
  directly, including the comparison counter and the stable-order audit.
  Difference: C+ `error` codes (101/108) are integer literals in Rust; C+
  `string::at`/checked indexing has no direct Rust analogue.
- **`hash_tool`** — FNV-1a 32-bit uses `wrapping_mul`; the self-check and
  validate-before-print ordering are reproduced. Difference: C+ `error *`
  categories are replaced by `(reason, code)` tuples.
- **`rle`** — encode/decode use `Vec<u8>`/`String` with explicit capacity
  gates; canonical `RLE1;` streams and every refusal reason are reproduced.
  Difference: C+ fixed `char` buffers and `rl_put` helpers are replaced by
  checked `push` operations; the 512-byte encode buffer cannot overflow here.
- **`regex_lite`** — the greedy-with-backtrack matcher, its exact step/backs
  counters and the 256-step budget are translated statement-for-statement,
  as is the built-in 6-case table. Difference: C+ `string::at` becomes byte
  indexing; no C+ `error` object exists in the Rust model.
- **`bigint`** — the 16-digit little-endian decimal arrays become
  `Vec<i32>`; add/sub/mul carries, normalization and every refusal reason are
  reproduced. Difference: C+ `print()` (space-joined args + newline) is modeled
  with a small `cprint` helper; no `vector`/`error` runtime is involved.
- **`prng`** — xorshift32 uses wrapping shifts on `u32`; strict decimal parsing
  with the 2^32/2^64 limits and the replay guard are reproduced.
  Difference: `error_fail`/`goto`-free control flow becomes `Result`; bounded
  sums use `wrapping_add`.
- **`checksum`** — bitwise reflected CRC32 (poly `0xEDB88320`) and Adler32
  (`mod 65521`) are direct translations; the `print()` space-joined refusal
  transcript is reproduced byte-for-byte (including `code= 101`).
  Difference: C+ fixed `char` buffers and `error` pointers are replaced by
  slices and `bool`/`Result`.
- **`base64`** — RFC 4648 encode/decode with the same padding rules, capacity
  gates and refusal reasons; the `print()` refusal transcript is reproduced.
  Difference: C+ `__builtin_memcpy` buffers are replaced by `Vec<u8>`; no C+
  `error` object or string-lowering machinery exists in the Rust model.

Every Rust program contains **no `unsafe` blocks, no FFI, no raw pointers and
no external crates**. Each `.rs` compiles standalone with
`rustc -O --edition 2021`.

## Explicit non-equivalence statement

A `MATCH` here means only: **for the fixed, deterministic argv sequences and
empty stdin used, the original C+ executable and the safe-Rust model produced
the same stdout bytes and the same process exit codes.** It does **not** mean
the two programs are language-, ABI-, implementation- or safety-equivalent.

The Rust files are idiomatic *models of observable behaviour*, not translations
of the C+ lowering pipeline: `.hp` header lowering, checked `vector`/
`string`/`file` wrappers, runtime bounds checks, `error *` categories, C+
RAII, the strict checker's refusals and the `print()` runtime have no direct
Rust syntax analogue and were replaced with `Vec`/`String`/`Result`, `impl`
methods, iterators and Rust's type-system safety. Memory layout, allocation
strategy, overflow diagnostics, error-object semantics and compile-time
guarantees were **not** compared. No claim is made beyond the eight
stdout/exit observations above.
