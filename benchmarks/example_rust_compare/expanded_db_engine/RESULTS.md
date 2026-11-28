# Expanded database / engine / safety examples — C+ vs safe-Rust stdout/exit comparison

This directory holds one standalone **safe-Rust** model (`.rs`, no `unsafe`, no
FFI, no raw pointers, no external crates) for each of the ten NEW expanded
database / engine / safety examples, plus a single `compare.sh` that builds the
original C+ (or C, for `sync_demo`) source and the Rust model, runs both with
**identical deterministic argv and empty stdin under a hard 30-second timeout**,
and compares **stdout bytes and process exit status**.

All numbers and result lines below come from an actual run, not from synthesis.
The captured stdout of the final run is in `out/compare.stdout`; the toolchain
banner is in `out/compare.stderr`.

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

The resolved tool paths printed by `compare.sh` were:

```
compare: root=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
compare: cplus=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
compare: clang=/usr/bin/clang
compare: rustc=/Users/haseeb-mir/.cargo/bin/rustc (rustc 1.100.0 (0ef1bc386 2026-11-09))
compare: timeout=timeout (build/run limit 30s)
```

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
bash benchmarks/example_rust_compare/expanded_db_engine/compare.sh
```

`compare.sh` writes only `EXPANDED_COMPARE ...` lines to stdout; toolchain
banners and diff context go to stderr. Build/run artifacts are kept in
`benchmarks/example_rust_compare/expanded_db_engine/out/` (per-example
`<name>.orig.build.*`, `<name>.rust.build.*`, `<name>.orig.stdout`,
`<name>.rust.stdout`, `<name>.orig.exit`, `<name>.rust.exit`).

## Exact build and run commands used by `compare.sh`

C+ (nine examples, cwd = repository root so `stdlib/*.hp` includes resolve):

```sh
timeout 30 ./build/c+ build --strict --sanitize=none -I. <src.cp> \
    -o out/orig_<name>
```

`memcpy_s_demo` additionally links the additive checked archives:

```sh
timeout 30 ./build/c+ build --strict --sanitize=none -I. <src.cp> \
    -o out/orig_memcpy_s_demo -- build/safe/libcplus_safecrt.a \
    build/libc/libcplus_hardened.a
```

C (only `sync_demo`, which is a `.c` file):

```sh
timeout 30 /usr/bin/clang -std=c99 -D_POSIX_C_SOURCE=200809L \
    -I. -Ilibc/install-include examples/expanded/safety/sync_demo.c \
    build/libc/libcplus_hardened.a -pthread -o out/orig_sync_demo
```

Rust (all ten):

```sh
timeout 30 rustc -O <name>.rs -o out/rust_<name>
```

Run (both sides, identical argv from `<name>.args` when present and empty
stdin, each bounded by `timeout -k 2 30`):

```sh
timeout 30 <binary> [argv...] < empty.stdin
```

Comparison: `cmp -s <orig.stdout> <rust.stdout> && [ <orig.exit> -eq <rust.exit> ]`.
A run that returns 124 (timeout) is reported `BLOCKED`, not scored as a match.

## Fixed inputs (identical for both sides)

| Example | argv source | Fixed invocation / stdin |
|---|---|---|
| `kv_store` | `kv_store.args` | `insert alpha one get alpha update alpha uno insert beta two list expect_size 2 get missing delete beta size dummy list` |
| `table_query` | `table_query.args` | `row 1 10 100 row 2 20 200 row 3 10 300 eq grp 10 range amt 100 300 count id 2 sum grp 10 expect_count grp 10 2 expect_sum grp 10 400 check 3` |
| `safety_demo` | — | no arguments |
| `ecs` | — | no arguments (built-in fixed seed) |
| `engine_loop` | `engine_loop.args` | `N N N N N N F R F L F N F R F N` |
| `resource_cache` | — | no arguments (built-in fixed script) |
| `memcpy_s_demo` | — | no arguments (forks an unguarded child internally) |
| `bounds_demo` | — | no arguments |
| `lifetime_demo` | — | no arguments |
| `sync_demo` | — | no arguments |

`stdin` is empty for both sides in every case (`empty.stdin`, rewritten on each
run by `compare.sh`), so no pre-existing file content can influence a result.

## Captured result lines (verbatim stdout of `compare.sh`)

```
EXPANDED_COMPARE kv_store MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE table_query MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE safety_demo MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE ecs MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE engine_loop MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE resource_cache MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE memcpy_s_demo MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE bounds_demo MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE lifetime_demo MATCH stdout byte-identical, exit=0 (both)
EXPANDED_COMPARE sync_demo MATCH stdout byte-identical, exit=0 (both)
```

Script exit status: **0**.

## Per-example table

| Example | Original source | Rust model | stdout bytes (orig / Rust) | Exit codes (orig / Rust) | SHA-256 (both) | Result |
|---|---|---|---:|---|---|---|
| `kv_store` | `examples/expanded/database/kv_store.cp` | `kv_store.rs` | 259 / 259 | 0 / 0 | `3e4065bd1d6e56bb16aa73b594cf09622f6ca74ad26e6416eef87c2975246051` | **MATCH** |
| `table_query` | `examples/expanded/database/table_query.cp` | `table_query.rs` | 273 / 273 | 0 / 0 | `6584eb1e9a737a2e6150fc945f95fddbc4930990b01bf303211d0f3483a6d6c7` | **MATCH** |
| `safety_demo` | `examples/expanded/database/safety_demo.cp` | `safety_demo.rs` | 665 / 665 | 0 / 0 | `327d29153d203cbac02a7444f7fec260d0432b039a2ed6576e60039ab170fe0f` | **MATCH** |
| `ecs` | `examples/expanded/engine/ecs.cp` | `ecs.rs` | 3547 / 3547 | 0 / 0 | `2f8c3380edc62f55bf5ada3b6383f1745c1fee097bbdc4dc7b7fab5f2875da21` | **MATCH** |
| `engine_loop` | `examples/expanded/engine/engine_loop.cp` | `engine_loop.rs` | 1301 / 1301 | 0 / 0 | `bc3f8315037f5b3c1c7f249e72984b5ca855eb588ffa33d5d666d648b9a4727f` | **MATCH** |
| `resource_cache` | `examples/expanded/engine/resource_cache.cp` | `resource_cache.rs` | 876 / 876 | 0 / 0 | `2003b3ba809e01c6577fa462b24d42234c2582941c82d466513fb0e367888645` | **MATCH** |
| `memcpy_s_demo` | `examples/expanded/safety/memcpy_s_demo.cp` | `memcpy_s_demo.rs` | 327 / 327 | 0 / 0 | `85671c56d69bfac26908c111796aa40a6bc5df3e158ae312ff134172f3d192e2` | **MATCH** |
| `bounds_demo` | `examples/expanded/safety/bounds_demo.cp` | `bounds_demo.rs` | 336 / 336 | 0 / 0 | `29059afcf1082edb846c7551f93575e1dc6ba765f05c71ae049464206236037d` | **MATCH** |
| `lifetime_demo` | `examples/expanded/safety/lifetime_demo.cp` | `lifetime_demo.rs` | 277 / 277 | 0 / 0 | `89b887cd856fda3905f100b363f0b2873c6b6af98e4a0cc4cb16324bbf6c13ac` | **MATCH** |
| `sync_demo` | `examples/expanded/safety/sync_demo.c` | `sync_demo.rs` | 261 / 261 | 0 / 0 | `8f703636e4ca41ed52c6f132daad5c46938da56bdf4181fd5284dd68fbc8882d` | **MATCH** |

Byte counts are from `wc -c` on the captured stdout files; SHA-256 is from
`shasum -a 256` on those same files. For every example the C and Rust stdout
files are byte-for-byte identical (identical hash), and both exit 0.

## Totals

| MATCH | DIFFERENT | BLOCKED | PENDING | Total |
|---:|---:|---:|---:|---:|
| 10 | 0 | 0 | 0 | 10 |

No case was untestable or timed out, so no `BLOCKED` result was needed. At the
time of the final run all ten scoped sources and all ten Rust models existed,
so no `PENDING` result was produced.

## Exit-status semantics

`compare.sh` exits **nonzero only on a real `DIFFERENT`** (one count added per
differing example). `BLOCKED` (build failure or 30 s run timeout) and `PENDING`
(missing scoped source or Rust model) are reported on stdout but never make the
script fail — the scoped sources are produced by sibling agents and may not
exist yet at a given run. A missing source/model is reported before any build
or comparison, so a `MATCH` is never fabricated for a file that is absent:

```
EXPANDED_COMPARE <name> PENDING missing original source <path>
EXPANDED_COMPARE <name> PENDING missing Rust model <path>
```

Exact refresh command after a missing source appears:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
bash benchmarks/example_rust_compare/expanded_db_engine/compare.sh
```

## How each C+ / C feature was modeled (and what differs)

- **`kv_store`** — the C+ fixed-width record `vector` with `at_checked`/`erase`/
  `reserve` becomes an owned `Vec<(String, String)>` (insertion order preserved
  so `list` matches); refusals are `Result` values with stable category/code/
  message. Difference: no C+ `error *` object, no lowering of `stdlib/vector`.
- **`table_query`** — rows become `Vec<[i32;3]>` with a `HashSet` id index;
  every query uses slice/iterator access and `SUM` uses `i32::checked_add` in
  place of the C+ overflow guard. Difference: no C+ `error *` object.
- **`safety_demo`** — hostile fixed-width records become owned
  `Vec<(String,String)>` plus a guarded canary array; `i32::checked_add`
  refuses the overflowing SUM before it happens. The canary is a *modelled
  invariant*, not a real guard page — Rust's owned containers cannot be
  overrun by these operations.
- **`ecs`** — the fixed-seed component simulation uses owned `Vec`/`HashMap`;
  C+ `vector_at_checked` / `intmap_get` become `Vec::get` / `HashMap::get`, and
  the recoverable `error *` API becomes a `Result` carrying
  `(category, code, message)`. Difference: no C+ checked container wrappers.
- **`engine_loop`** — the fixed-timestep state machine uses an owned `Vec<i32>`
  for the argv token queue with slice indexing guarded by iteration bounds, in
  place of C+ `stdlib::vector::at_checked`. Difference: no C+ `at_checked`.
- **`resource_cache`** — generation-tagged integer handles over per-slot
  state become owned `Vec<Asset>` plus a `HashMap<id, slot>`; the payload is an
  owned `[u8;8]` rather than a raw pointer, so no `unsafe` is needed. Double
  release / use-after-release refusals carry the same category/code/message.
- **`memcpy_s_demo`** — the original forks an unguarded one-over copy in a
  child (corrupting a canary) and calls the additive checked `cplus_memcpy_s`
  in the parent. Safe Rust cannot fork or write past a buffer, so the child's
  deterministic outcome (the 9th byte lands on the canary) is modelled
  *explicitly* and the checked call is a length check plus `copy_from_slice`.
  The `IGNORE CONSTRAINT HANDLER` line is reproduced verbatim as text. This is
  a transcription of an observed outcome, not a real out-of-bounds write.
- **`bounds_demo`** — the C++ demo never dereferences memory; metadata queries
  (`span_ok`/`contains`/`key_current`/`query`) become an owned
  `Tracked { cap, gen }` value and ordinary checked integer comparisons. No
  raw pointer or span metadata exists in the Rust model.
- **`lifetime_demo`** — the C+ raw-pointer + generation-token pairing becomes
  owned `Vec<u8>` storage plus a token on an `Owner`; a `Borrow` stores only
  the captured token and every access checks it before safe slicing. No raw
  pointer is present.
- **`sync_demo`** — the C99 checked destroy/join wrappers in
  `libcplus_hardened.a` become Rust's type system: an `Option<Mutex/Condvar/
  JoinHandle>` is consumed by the first successful destroy/join, so a second
  attempt is refused with EINVAL before touching the primitive. No FFI or
  `unsafe`.

Every Rust program contains **no `unsafe` blocks, no FFI, no raw pointers and
no external crates**. Each `.rs` compiles standalone with `rustc -O` and was
built by `compare.sh` with the pinned rustc above.

## Explicit non-equivalence statement

A `MATCH` here means only: **for the fixed, deterministic argv and empty stdin
used, the original C+ / C executable and the safe-Rust model produced the same
stdout bytes and the same process exit codes.** It does **not** mean the two
programs are language-, ABI-, implementation- or safety-equivalent. The Rust
files are idiomatic *models of observable behaviour*, not translations of the
C+ lowering pipeline: `.hp` header lowering, checked `vector`/`string`/`file`
wrappers, span/lifetime metadata, runtime bounds checks, `error *` categories,
checked `memcpy_s`/hardened-C wrappers, C+ RAII and the strict checker's
refusals have no direct Rust syntax analogue and were replaced with
`Vec`/`String`/`Result`/`Option`, `impl` methods, iterators and Rust's
type-system safety. Memory layout, allocation strategy, overflow diagnostics,
fork/subprocess behaviour, error object semantics, thread-primitive internals
and compile-time guarantees were **not** compared. In particular, the
`memcpy_s_demo` and `bounds_demo` Rust models reproduce an *observed output*
without performing any out-of-bounds or metadata operation. No claim is made
beyond the ten stdout/exit observations above.
