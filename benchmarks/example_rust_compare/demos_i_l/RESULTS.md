# Example-by-example behavior comparison: C+ demos I–L vs safe Rust

Generated 2026-11-23UTC. Branch `agent/modern-stdlib`. All work is confined to
`benchmarks/example_rust_compare/demos_i_l/`; no repository source was modified.

## What "MATCH" means here

`MATCH` = the two executables produced **byte-identical stdout and the same
exit status** for the same deterministic input, under a hard 30 s timeout.
It is **not** a claim of language, type-safety, or memory-safety equivalence.
See the explicit non-equivalence statement at the end.

## Toolchains (exact)

| Tool | Version | Path used |
|---|---|---|
| C+ driver | `C+ 0.1.0` | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+` |
| C backend | Apple clang 16.0.0 (`clang-1600.0.26.6`) | `/usr/bin/clang` (via `CPLUS_CLANG`) |
| Rust | `rustc 1.100.0 (0ef1bc386 2026-11-09)` | `/Users/haseeb-mir/.cargo/bin/rustc` |
| Host | Darwin 27.2.0, arm64 | — |
| Timeout | GNU `timeout` | `/usr/bin/timeout` |

Environment: `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`, `LC_ALL=C`,
`CPLUS_CLANG=/usr/bin/clang`. Toolchain paths are resolved **before** PATH is
pinned (rustc is not in the pinned PATH).

## Build command note (important honesty point)

The task template distinguishes ".c → /usr/bin/clang + archives" from
".cp → build/c+ --strict --sanitize=none". In practice **all four sources are
`.c` files that contain C+ language extensions** (`resource struct`, `class`,
`auto`, `print`, `scan`, `values.len`). Plain `/usr/bin/clang` cannot parse them:

```
$ /usr/bin/clang -O2 demos/I_raii_safe.c -o /tmp/raii_plain
demos/I_raii_safe.c:7:1: error: unknown type name 'resource'
... 6 errors generated.
```

The faithful way to compile the originals is therefore the C+ driver, which
lowers the extensions to generated C and compiles that C with `/usr/bin/clang`
while linking `build/libcplus_runtime.a` and `build/libcplus_core.a`. I used the
prescribed strict flags for all four originals:

```
/usr/bin/timeout 30 build/c+ build demos/<name>.c \
    --strict --sanitize=none -o results/bin/cplus_<name>
```

Rust equivalents (safe Rust: no `unsafe`, no FFI, no raw pointers):

```
rustc -O benchmarks/example_rust_compare/demos_i_l/<name>.rs \
    -o results/bin/rust_<name>
```

Run (identical deterministic stdin for both; J gets `7 2.5 demo\n`, others get
an empty stream):

```
/usr/bin/timeout 30 <binary> < results/runs/<input>.stdin \
    > results/runs/<name>.<lang>.out 2> results/runs/<name>.<lang>.err
```

Driver (reproduces every row below):

```
sh benchmarks/example_rust_compare/demos_i_l/compare.sh
```

It prints one `EXAMPLE_COMPARE <name> MATCH|DIFFERENT|BLOCKED <reason>` line per
example. Raw artifacts (binaries, build/run logs, stdout/stderr/exit captures)
are under `benchmarks/example_rust_compare/demos_i_l/results/`.

## Per-example results

| Example | C+ original build (`--strict --sanitize=none`) | Rust build | Verdict | stdout / exit |
|---|---|---|---|---|
| I_raii_safe | ok | ok (no warnings) | **MATCH** | 5 lines, identical bytes; exit 0 / 0 |
| J_modern_io | ok | ok (no warnings) | **MATCH** | 2 lines, identical bytes; exit 0 / 0 |
| K_state_class | **FAILS** (compile-time strict check) | ok (no warnings) | **BLOCKED** | no strict C+ binary to run |
| L_class_lifecycle | ok | ok (no warnings) | **MATCH** | 2 lines, identical bytes; exit 0 / 0 |

### I_raii_safe — MATCH

Both printed exactly (`sed -n l` view, `$` = newline):

```
drop 1$
drop 2$
drop 3$
drop 2$
early=-1$
```

Mechanism difference: C+ `resource struct Mark` makes the compiler inject
`Mark_drop(&m)` at every scope exit (normal loop end, `continue`, early
`return`). Rust has no `resource` qualifier; `impl Drop for Mark` is the
equivalent hook and the compiler likewise runs it on `continue` and `return`.
The observable drop order is identical, but the guarantees come from different
language mechanisms (C+ extension + injected call vs Rust `Drop` trait).

### J_modern_io — MATCH

Input: `7 2.5 demo\n`. Both printed:

```
enter count ratio label:$
count= 7 ratio= 2.5 label= demo values.len= 3$
```

Mechanism difference: C+ `print` is variadic and dispatches each value by its
C type with no format specifiers, inserting one space between arguments and a
trailing newline; C+ `scan(count, ratio, label)` takes lvalues and takes their
addresses internally; `values.len` is a language-level array-length property.
Rust uses a `println!` format string, explicit `split_whitespace()` + `parse()`
for typed input, and the `.len()` method. Output bytes coincide for this input;
the APIs and their failure modes are not the same.

### K_state_class — BLOCKED (under the prescribed strict build)

The original C+ source is rejected at compile time, so no comparison run is
possible under the required toolchain. Exact evidence from
`results/logs/K_state_class.cplus.build.err` (exit 1):

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/demos/K_state_class.c:17:30: error: [CPLUS-ARITH] division by zero cannot be excluded
          return self->count ? self->total / self->count : 0;
                               ^~~~
```

This is a genuine C+ strict-mode (fail-closed) limitation: the compiler does
not prove the divisor `self->count` non-zero behind the ternary guard. It is
reported **BLOCKED**, not MATCH and not DIFFERENT — there is no strict C+
executable whose output could be compared.

Supplementary (not part of the verdict): with the non-strict, explicitly unsafe
C+ mode `--legacy-c`, the original builds and prints `count= 3 average= 8`, and
the safe-Rust equivalent (`results/bin/rust_K_state_class`) prints the same
bytes (`cmp`: IDENTICAL). This shows the Rust behavioral model is correct; it
does not change the BLOCKED verdict for the prescribed strict flags.

Mechanism difference (if/when comparable): C+ `class State` methods lower to
`State_add(&state, n)` / `State_get(&state)` with an implicit receiver; `auto`
infers the zero-initialized struct type. Rust uses an explicit `struct` with
`&mut self` / `&self` methods and an explicit `State::new()`.

### L_class_lifecycle — MATCH

Both printed:

```
total 10$
drop point 10$
```

Mechanism difference: C+ `Point(int a, int b)` lowers to `Point_ctor()` and the
`drop` method lowers to `Point_drop()`, invoked automatically at scope exit.
Rust has no C-style constructor/destructor: `Point::new` is an ordinary
associated function and `impl Drop for Point` is the RAII hook. The same two
lines appear in the same order, enforced by different language rules.

## Totals

- MATCH: **3** (I_raii_safe, J_modern_io, L_class_lifecycle)
- DIFFERENT: **0**
- BLOCKED: **1** (K_state_class — strict C+ compile-time `[CPLUS-ARITH]`
  rejection; exact error above)
- Examples compared: 4

## Explicit non-equivalence statement

**Matching observable behavior is NOT equivalent safety.** Every `MATCH` above
means only that two programs printed the same bytes and returned the same exit
status for one deterministic run. It does not mean the implementations provide
the same guarantees:

- The C+ demos are compiled in `--strict --sanitize=none` mode, i.e. without
  runtime sanitizers; C+ safety relies on compile-time analysis plus injected
  RAII, and this project explicitly documents that C+ is not a blanket
  memory-safety guarantee.
- Rust's `Drop`, `Vec`/array bounds, typed `parse()` and `&mut self` borrows are
  enforced by the Rust type system and ownership rules; C+ `resource`/`class`
  are compiler lowering extensions over raw C structs with no borrow checker.
- The K case is the clearest counter-example to any "equivalence" claim: the
  C+ strict checker rejects a construct that Rust accepts and runs. Identical
  outputs where they exist were produced by different mechanisms with
  different failure modes; a passing run is evidence about that one execution
  only, not a proof of safety for either language.

## Delivered files (all under `benchmarks/example_rust_compare/demos_i_l/`)

- `I_raii_safe.rs`, `J_modern_io.rs`, `K_state_class.rs`, `L_class_lifecycle.rs`
- `compare.sh`
- `RESULTS.md`
- `results/` — generated binaries, build logs, run captures (artifacts)
