# C+ vs Rust memory-safety violation comparison

Native (compiler-binary) suite that exercises **equivalent memory-safety
violations** in C+ and Rust and records the exact stage and evidence of
rejection. It owns only new files in this directory; it does not build the
compiler, does not touch other test trees, and makes no commits.

## Run

```sh
cd <repo-root>
python3 tests/compiler/memory_safety_compare/run_suite.py
```

Environment knobs:

| Variable | Default | Purpose |
| --- | --- | --- |
| `CPLUS_BIN` | `<repo>/build/c+` | C+ compiler binary (never rebuilt) |
| `RUSTC` | `rustc` | Rust compiler |
| `MSC_RUSTC_FLAGS` | `--edition 2021 -C overflow-checks=on` | Rust compile flags |
| `MSC_TIMEOUT` | `60` | hard timeout per subprocess, seconds |
| `MSC_WORKERS` | `2` | parallel workers |
| `CPLUS_CLANG` | `/usr/bin/clang` | C backend used by `c+` |

Exit status is `0` only when all 14 declared expectations match, `1` otherwise.

## Sanitizers are disabled

| Language | Compile command | Why this is unsanitized |
| --- | --- | --- |
| C+ | `c+ check --strict --sanitize=none <SRC>` and `c+ build --strict --sanitize=none <SRC> -o <BIN>` | `--sanitize=none` explicitly disables the default UBSan instrumentation |
| Rust | `rustc --edition 2021 -C overflow-checks=on <SRC> -o <BIN>` | no `-Zsanitizer` flag; Rust debug overflow checks are language debug assertions, not a compiler sanitizer |

Any sanitizer banner (`AddressSanitizer`, `UndefinedBehaviorSanitizer`,
`runtime error:`, `MemorySanitizer`, `ThreadSanitizer`, `LeakSanitizer`) in any
captured output is a hard failure.

## Exact assertions — no bare nonzero exit

Each case declares **one** stage and the exact evidence required for it:

* `compile_reject` — compiler rc is exactly **1**, the named diagnostic
  substring(s) are present, and **no executable is produced**. For C+ both
  `check` and `build` are asserted independently. For Rust, `rustc` is the
  compile step.
* `runtime_trap` — compile rc is exactly **0** and a binary is produced; then
  the binary is run and its rc must equal the exact expected code
  (C+ SIGABRT = **-6**, Rust main-thread panic = **101**) and the exact panic
  / diagnostic substring(s) must be present. The process must not time out or
  crash with an unexpected signal.

A case that merely returns nonzero without its declared code *and* diagnostic
fails. Source paths in logs are normalised to `<SRC>`; the dynamic Rust thread
id in panic messages is left untouched, so assertions use stable substrings
(`thread 'main'`, the panic reason) rather than the whole line.

## Coverage: 7 categories, 7 pairs, 14 cases

| Category | C+ case | stage | Rust case | stage |
| --- | --- | --- | --- | --- |
| out-of-bounds index | `volatile` index 7 into `int a[4]` | runtime_trap (-6, `C+ error: index 7 is outside the bounds of 'a'`) | `black_box(7)` into a `Vec` of len 4 | runtime_trap (101, `index out of bounds: the len is 4 but the index is 7`) |
| use-after-free | subscript of released pointer | compile_reject (`[CPLUS-LIFETIME] ... use-after-free`) | drop of a `Box` while `&mut` borrow is live | compile_reject (`E0505`) |
| double-free | second `free(p)` | compile_reject (`[CPLUS-OWNER] double free of 'p'`) | second `drop(v)` | compile_reject (`E0382`, use of moved value) |
| invalid free | `free(p+2)` on an `malloc` owner | compile_reject (`[CPLUS-OWNER] invalid free of interior pointer`) | interior `*mut i32` passed where owned `Box<i32>` is required | compile_reject (`E0308`, mismatched types) |
| null deref | dereference of provably null `p` | compile_reject (`[CPLUS-SAFE-LIFETIME] null pointer dereference`) | `None.unwrap()` | runtime_trap (101, `called Option::unwrap() on a None value`) |
| integer overflow | `2147483647 + 1` | compile_reject (`[CPLUS-ARITH] signed integer overflow in addition`) | `i32::MAX + 1` with `black_box` operands | runtime_trap (101, `attempt to add with overflow`) |
| invalid shift | `1 << 32` | compile_reject (`[CPLUS-FFI] shift count 32 is not less than the width 32`) | `1i32 << black_box(32u32)` | runtime_trap (101, `attempt to shift left with overflow`) |

Note on **invalid free**: Rust has no raw `free`. The safe deallocation path
requires an owned `Box<T>`, so an interior raw pointer cannot be handed to it;
the compiler rejects the program with `E0308`. That is the faithful Rust-side
analogue of C+ rejecting `free(p+2)` — both refuse to deallocate a pointer that
is not the owner — but it is a type rejection rather than a runtime allocator
check, and it is reported as such.

## Results

Written to `results/`:

* `summary.txt` — counts (overall, per language, per stage, per category) and
  every failure with exact case name, language, stage and reason.
* `summary.json` — full records: exact commands, return codes, timeouts,
  diagnostic/panic text, sources, expectations and failure list.
* `logs/<case>.<step>.txt` — complete normalised output and return code for
  every `check` / `compile` / `run` subprocess.

Sources in `cases/` are regenerated from the manifest in `run_suite.py` on
every run. Executables remain in `cases/` only for `runtime_trap` cases, as
evidence that compilation succeeded before the trap.

## Last verified run

`14 / 14 cases passed, 0 failed` (7 categories; C+: 6 compile_reject + 1
runtime_trap; Rust: 3 compile_reject + 4 runtime_trap) against `build/c+`
(C+ 0.1.0) and `rustc 1.100.0 (0ef1bc386 2026-11-09)`, both unsanitized.
