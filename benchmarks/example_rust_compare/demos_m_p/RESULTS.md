# Examples M–P — original C+ vs safe-Rust behaviour comparison

This directory contains one standalone safe-Rust model per demo plus a single
`compare.sh` that builds and runs the original and the Rust model for each
example, then compares **stdout bytes and process exit status** under a
30-second timeout with identical (empty) stdin.

All numbers and outputs below are from an actual run, not synthesized.

## Toolchains (exact, from the captured run)

```
CMD: rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

CMD: /usr/bin/clang --version | head -2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

CMD: ./build/c+ --version 2>&1 | head -2
C+ 0.1.0
Language: C+ (C with memory safety, deterministic RAII, lightweight classes)

CMD: timeout --help | head -1
Usage: timeout [-f | --foreground] [-k time | --kill-after time] [-p | --preserve-status] [-s signal | --signal signal]  [-v | --verbose] <duration> <command> [arg ...]

CMD: uname -m
arm64
```

Repository path for the run:
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`, branch `agent/modern-stdlib`.

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
bash benchmarks/example_rust_compare/demos_m_p/compare.sh
```

The script writes only `EXAMPLE_COMPARE ...` lines to stdout; toolchain banners
and any diff context go to stderr. Build/run artifacts are kept in
`benchmarks/example_rust_compare/demos_m_p/.build/` (per-example
`<name>.build.log`, `orig_<name>.stdout`, `rust_<name>.stdout`, stderr files).

## Exact build commands used by `compare.sh`

Rust (all four examples):

```sh
timeout 30 rustc -O --edition 2021 <name>.rs -o .build/rust_<name>
```

Originals:

```sh
# M_default_args.c, N_range_for.c — C+ syntax must be lowered first.
# Evidence that plain clang cannot compile these sources directly:
/usr/bin/clang -std=gnu17 -I runtime/include -I demos demos/M_default_args.c -o /dev/null
# demos/M_default_args.c:6:27: error: C does not support default arguments
# demos/M_default_args.c:13:22: error: C does not support default arguments
# demos/M_default_args.c:13:33: error: C does not support default arguments
# 3 errors generated.
/usr/bin/clang -std=gnu17 -I runtime/include -I demos demos/N_range_for.c -o /dev/null
# demos/N_range_for.c:7:1: error: unknown type name 'class'
# ... (also `auto`, range-for and managed-pointer extensions)

# Therefore the .c pipeline is: c+ lowering, then clang + the runtime archive.
# Every c+ invocation runs with cwd = repository root (c+ resolves stdlib/*.hp
# relative to cwd); --emit-c is given -o so no default-named executable lands
# in the caller's directory.
timeout 30 build/c+ --strict --sanitize=none --emit-c \
    .build/lowered_<name>.c demos/<name>.c -o .build/emitted_<name>
timeout 30 /usr/bin/clang -std=gnu17 -I runtime/include -I demos \
    .build/lowered_<name>.c "$RT" -o .build/orig_<name>

# O_extensions.cp, P_std_library.cp — canonical .cp/.hp path (includes .hp lowering):
timeout 30 build/c+ --strict --sanitize=none build demos/<name>.cp -o .build/orig_<name>
```

A robustness check was also run with the caller's cwd set to `/tmp`; all four
result lines were byte-identical to those above.

`RT` is **discovered at run time**, not hard-coded, from the archive path in
`build/c+ --strict --sanitize=none --verbose build demos/M_default_args.c`
(`.build/probe_rt.log`). In the captured run it resolved to
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/build-verify-1123/libcplus_runtime.a`,
which is the same archive the c+ driver links into a normal `c+ build`
(verified with `c+ --verbose`: the link line ends in `.../libcplus_runtime.a`).
The repo-local `build/libcplus_runtime.a` lacks `__cplus_bounds_check`, so it
cannot link the lowered N demo; using the driver's own archive avoids that.

A cross-check was also performed: canonical `c+ --strict --sanitize=none`
builds of M and N produced stdout byte-identical to the clang+archive builds
reported below.

Run commands (same empty stdin for both):

```sh
timeout 30 .build/orig_<name>   < .build/stdin > .build/orig_<name>.stdout   2> .build/orig_<name>.stderr
timeout 30 .build/rust_<name>   < .build/stdin > .build/rust_<name>.stdout   2> .build/rust_<name>.stderr
cmp -s .build/orig_<name>.stdout .build/rust_<name>.stdout
```

## Captured result lines (verbatim stdout)

```
EXAMPLE_COMPARE M_default_args MATCH stdout bytes and exit 0 identical with empty stdin
EXAMPLE_COMPARE N_range_for MATCH stdout bytes and exit 0 identical with empty stdin
EXAMPLE_COMPARE O_extensions MATCH stdout bytes and exit 0 identical with empty stdin
EXAMPLE_COMPARE P_std_library MATCH stdout bytes and exit 0 identical with empty stdin
```

Script exit status: **0**.

## Per-example table

| Example | Original source | Rust model | Original stdout (bytes) | Rust stdout (bytes) | Exit (orig/rust) | Result |
|---|---|---|---|---|---|---|
| `M_default_args` | `demos/M_default_args.c` | `M_default_args.rs` | `first 10 default 6 partial 9\n` (29) | identical (29) | 0 / 0 | **MATCH** |
| `N_range_for` | `demos/N_range_for.c` | `N_range_for.rs` | `total 60\nbuffer.len 3\n` (22) | identical (22) | 0 / 0 | **MATCH** |
| `O_extensions` | `demos/O_extensions.cp` (+ `O_extensions.hp`) | `O_extensions.rs` | `counter 15\n` (11) | identical (11) | 0 / 0 | **MATCH** |
| `P_std_library` | `demos/P_std_library.cp` | `P_std_library.rs` | `language C+ scores 2 20\n\n` (25) | identical (25) | 0 / 0 | **MATCH** |

Byte counts are from `od -c`/`wc -c` on the captured stdout files; each original
and Rust stderr file was empty.

## Totals

| MATCH | DIFFERENT | BLOCKED | Total |
|---:|---:|---:|---:|
| 4 | 0 | 0 | 4 |

No case was genuinely untestable, so no `BLOCKED` result was needed.

## How each C+ feature was modeled (and what differs)

- **Default arguments (M).** Rust has no default parameters. The three call
  sites were lowered explicitly: `get_data()` -> `get_data(None)`, `sum(1)` ->
  `sum(1, None, None)`, `sum(1, 5)` -> `sum(1, Some(5), None)`, with helper
  functions `get_data_default`, `sum_one`, `sum_two`. The C+ front end instead
  rewrites the AST to `get_data(0)`, `sum(1, 2, 3)`, `sum(1, 5, 3)`. Difference:
  the Rust `Option` layer is an API-modeling device; it is not a language-level
  default argument and cannot be called with C+ syntax.
- **Range-for (N).** `for (auto v : arr)` -> Rust iterator `for` over `&[i32; 3]`
  and `&Vec<i32>`; the managed `cplus_new_array(int, 3)` -> `vec![0i32; 3]`
  with `buffer.len()` in place of `buffer.len`. Difference: the C+ lowering
  emits a counted loop with `__cplus_bounds_check` on element reads; Rust uses
  iterator/`Vec` bounds safety with no runtime bounds-check call in the model.
- **`.cp`/`.hp` class (O).** The lowered `class Counter` (constructor + `add`
  + `get`) becomes `struct Counter` + `impl` with `new`/`add`/`get`; `auto` -> a
  `let` binding with `mut`. Difference: Rust has no header/translation-unit
  lowering or `self->` member syntax; the `.hp` inclusion mechanism itself has
  no Rust analogue and is not reproduced.
- **C+ stdlib wrappers (P).** `string`/`vector`/`mutex`/`guard` -> `String`,
  `Vec<i32>`, `std::sync::Mutex<()>`, `MutexGuard`; type-generic `print` -> a
  small `cprint!` macro using `Display` + `join(" ")`; `println()` -> `println!()`.
  Difference: C+ stdlib types are `.hp` wrappers over libc/POSIX with runtime
  RAII, while the Rust types are standard-library implementations with their
  own allocation and locking machinery; this single-threaded example cannot
  distinguish their concurrency semantics.

The Rust programs contain **no `unsafe` blocks, no FFI, no raw pointers from
game memory, and no external crates**. Each `.rs` file compiles standalone with
`rustc -O --edition 2021`.

## Explicit non-equivalence statement

A `MATCH` here means only: **for the fixed, deterministic input used (empty
stdin), the original executable and the safe-Rust model produced the same
stdout bytes and the same process exit code.** It does **not** mean the two
programs are language-, ABI-, implementation-, or safety-equivalent. The Rust
files are idiomatic *models of observable behaviour*, not translations of the
C+ lowering pipeline: default arguments, range-for instrumentation, `.hp`
header lowering, managed allocations, runtime bounds checks and C+ RAII wrappers
have no direct Rust syntax analogue and were replaced with `Option`/helpers,
iterators, `struct`+`impl`, `Vec`/`String`/`Mutex`, and Rust's type-system
safety. Memory layout, allocation strategy, runtime diagnostics, diagnostics
on misuse, thread-sync implementation, and compile-time guarantees differ and
were not compared. No claim is made beyond the four stdout/exit observations
above.
