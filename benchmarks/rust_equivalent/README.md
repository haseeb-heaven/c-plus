# Rust equivalents of the C benchmark programs

This directory contains deterministic Rust equivalents of the plain-C benchmark
programs in `benchmarks/`. Each Rust program prints **exactly the same stdout**
as its C counterpart, in both optimized and unoptimized builds. No existing
files are modified.

Toolchain: `rustc 1.100.0 (0ef1bc386 2026-11-09)`, invoked directly.

## Source mapping

| C source (`benchmarks/`) | Rust equivalent (this dir) | Deterministic stdout |
| --- | --- | --- |
| `bounds_sum.c` | `bounds_sum.rs` | `bounds_sum 1350000000` |
| `integer_compute.c` | `integer_compute.rs` | `integer 6299863613973285121` |
| `pointer_chase.c` | `pointer_chase.rs` | `chase 24999975000000` |
| `function_call.c` | `function_call.rs` | `call 2560` |
| `alloc_churn.c` | `alloc_churn.rs` | `alloc_churn 9000021000000` |
| `raii_loop.c` | `raii_loop.rs` | `raii_loop 125000000 0` |
| `method_dispatch.c` | `method_dispatch.rs` | `method_dispatch 75000000` |

`parallel_scale.c` is intentionally not included: it was not part of the
requested set.

## Build and run

Release (`-O`, opt-level 2):

```sh
rustc --edition 2021 -O bounds_sum.rs -o bounds_sum
./bounds_sum
```

Debug (`-O0`): rustc has no `-O0` flag; the direct equivalent is
`-C opt-level=0`, which is also the default and keeps overflow checks on:

```sh
rustc --edition 2021 -C opt-level=0 bounds_sum.rs -o bounds_sum_debug
./bounds_sum_debug
```

Repeat the two commands for each `.rs` file. Output binaries are build
artifacts and are not checked in.

## Notes on equivalence and safety

- `integer_compute.rs` uses `wrapping_mul`/`wrapping_add` so the defined C
  unsigned wrap (modulo 2^64) is identical with debug overflow checks on and
  with optimization on.
- `pointer_chase.rs` is the only file using `unsafe`. That benchmark measures
  pointer chasing, so it keeps a real raw-pointer linked ring rather than an
  index-based (bounds-checked) `Vec` walk, which would measure different work.
  The ring lives in a `Vec<Node>` filled to capacity before any address is
  taken and never resized afterwards; every `next` points to a live element,
  so all dereferences are sound. Safety is documented inline.
- `alloc_churn.rs` uses `Option<Box<Node>>` for the `a -> b` link, so dropping
  `a` frees both objects (the C build frees `b` then `a`). `black_box` plays the
  role of the `volatile escaped` sink, keeping the allocations observable.
- `raii_loop.rs` implements `Drop` for `Acc`, the direct analogue of the C+
  compiler-inserted destructor; a static `AtomicI32` stands in for `destroyed`
  (always 0 because `total` is never `-1`).
- `function_call.rs` uses `#[inline(never)]` to mirror `__attribute__((noinline))`.
- `method_dispatch.rs` uses ordinary `&mut self` methods, matching the C+
  `p.add(x)` lowering to `Counter_add(&p, x)`.

## Verification results

Ground-truth stdout was captured by running the existing prebuilt C binaries in
`benchmarks/out/`. All seven Rust programs were compiled in both modes and
produced byte-identical stdout to their C counterparts in both modes:

```
mode                      stdout (all 7 programs)
-O       (release)        matches every C binary
-C opt-level=0 (debug)    matches every C binary
```

Exact stdout per program is listed in the table above.
