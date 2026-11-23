# Per-method memory-safety comparison: C+ checked alloc APIs vs Rust

Scope: `benchmarks/safety_compare/per_method/alloc/` only. This is a
**safety-outcome** comparison, not a performance benchmark. Each case prints
one deterministic line `<method> <case> <token>`.

## Files

- `alloc_harness.c` — deterministic C99 harness driving the C+ SafeCRT checked
  allocation APIs (`malloc_s`, `calloc_s`, `realloc_s`, `free_s`,
  `free_checked_s`, `aligned_alloc_s`).
- `alloc.rs` — safe-Rust equivalents (`Vec::with_capacity`, `try_reserve_exact`,
  `Box`, `Option::take`, `checked_mul`). **No `unsafe`.**
- `compare.sh` — driver: verifies symbols, builds both, runs each under a 30 s
  timeout, diffs tokens, prints `METHOD_ALLOC ...`, exits nonzero only on a
  real mismatch.
- `results/` — generated binaries, raw `.out` streams, `.err` and `.exit`
  files (gitignored by `results/.gitignore`).
- `NOTES.md` — this file.

## Symbols verified with `nm` (before writing any harness)

```
nm -g --defined-only build/safe/libcplus_safecrt.a | grep -iE 'malloc|calloc|realloc|free|aligned'
  0000000000000000 T _malloc_s
  00000000000001e4 T _calloc_s
  0000000000000470 T _realloc_s
  0000000000000a5c T _free_checked_s
  0000000000000bf4 T _free_s
  0000000000000324 T _aligned_alloc_s
  0000000000000e90 T _cplus_safe_ptr_was_freed
  ... also present: _cplus_safe_ptr_is_live, _cplus_safe_ptr_size,
      _cplus_safe_alloc_fail_after, _cplus_safe_alloc_fail_reset,
      _cplus_safe_alloc_seq

nm -g --defined-only build/libc/libcplus_hardened.a | grep -iE 'malloc|calloc|realloc|free|aligned'
  0000000000000354 T _cplus_malloc
  0000000000000440 T _cplus_calloc
  0000000000000554 T _cplus_realloc
  0000000000000acc T _cplus_free
  00000000000008c0 T _cplus_free_checked
  0000000000000000 T _cplus_aligned_alloc
  0000000000001384 T _cplus_alloc_double_free_count
  00000000000013c0 T _cplus_alloc_invalid_free_count
```

All methods requested by the task are present, including `aligned_alloc_s`
("if present": it is present). `compare.sh` re-checks every symbol at runtime
and aborts with exit 2 if any is missing. Note: the checked `*_s` APIs live in
`libcplus_safecrt.a`; the `cplus_*` hardened-libc entry points live in
`libcplus_hardened.a`. Both archives are linked (safecrt first) because the
safecrt bookkeeping calls hardened-libc helpers; this matches the repository's
`tests/libc/run_alloc_fault_matrix.sh` link order.

## Exact commands and toolchains (from the repository root)

```
$ /usr/bin/clang --version | head -1
Apple clang version 16.0.0 (clang-1600.0.26.6)
$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)
$ python3 --version
Python 3.12.x
$ command -v timeout
/usr/bin/timeout
```

Build commands issued by `compare.sh` (archive sizes are volatile because
other agents rebuild in parallel; the `nm` symbol check is the real gate):

```sh
/usr/bin/clang -std=c99 -O1 -g -I. \
  -Ilibc/install-include -Ithird_party/safeclib/include \
  benchmarks/safety_compare/per_method/alloc/alloc_harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread \
  -o benchmarks/safety_compare/per_method/alloc/results/alloc_harness

rustc -O benchmarks/safety_compare/per_method/alloc/alloc.rs \
  -o benchmarks/safety_compare/per_method/alloc/results/alloc_rust

/usr/bin/timeout 30 .../results/alloc_harness > .../results/c.out 2> .../results/c.err
/usr/bin/timeout 30 .../results/alloc_rust    > .../results/rust.out 2> .../results/rust.err
```

`-I.` is required in addition to the two include paths named in the task so
that `#include "stdlib/safe/safe_alloc.h"` (the real checked-API header, which
also pulls in `safe_error.h`) resolves from the repository root. `RSIZE_MAX_MEM`
is `SIZE_MAX >> 1` in that header. `-lm -pthread` are the link flags used by
the repository's own allocation-fault-matrix runner.

Run everything with:

```sh
sh benchmarks/safety_compare/per_method/alloc/compare.sh
```

## Case / token design

Tokens: `OK` (valid alloc/free), `REFUSED` (dangerous op refused), `DEFINED`
(zero-size / null no-op), `PRESERVED` (injected failure left the original live
and untouched — realloc atomicity), `CANARY_OK` (8-byte guard at offset 64
survived), `MISMATCH` (C+ behavior disagreed with its documented contract),
`BLOCKED` (not expressible in safe Rust).

- **Failure injection** uses `cplus_safe_alloc_fail_after(0)` /
  `cplus_safe_alloc_fail_reset()`, so the refusal is deterministic (not a real
  OOM). For `realloc_s` the harness additionally asserts the original pointer
  is still live with its original 32-byte size after the failed growth.
- **Overflow** asserts `errno`: `EINVAL` for `malloc_s(SIZE_MAX)`,
  `realloc_s(p, SIZE_MAX)`, `aligned_alloc_s(16, SIZE_MAX)` and bad alignment;
  `EOVERFLOW` for `calloc_s(SIZE_MAX, 4)`.
- **Double/invalid free** asserts `free_checked_s` returns `CPLUS_SAFE_EINVAL`
  (22) with `errno == EINVAL`; a stale alias is used for the double free and a
  stack object for the invalid free.
- **Canary** writes 64 bytes of payload plus an 8-byte `0xDEADBEEFCAFEBABE`
  guard, exercises the API, then re-reads the guard; `CANARY_OK` means no
  out-of-bounds write occurred.

Rust side: `try_reserve_exact(usize::MAX)` returns `Err(CapacityOverflow)` and,
by its documented contract, leaves the existing buffer/contents untouched —
that is how the atomicity case (`realloc_s inject -> PRESERVED`) is matched
genuinely rather than hard-coded. Double free is modeled with `Option::take`:
the second `take()` yields `None`, so the second "free" is a no-op refusal,
not a heap corruption.

## Actual output (verbatim, repeated runs byte-identical)

```
malloc_s valid OK
malloc_s overflow REFUSED
malloc_s zero DEFINED
malloc_s inject REFUSED
malloc_s canary CANARY_OK
calloc_s valid OK
calloc_s overflow REFUSED
calloc_s zero DEFINED
calloc_s inject REFUSED
calloc_s canary CANARY_OK
realloc_s valid OK
realloc_s overflow REFUSED
realloc_s zero DEFINED
realloc_s inject PRESERVED
realloc_s invalid REFUSED
realloc_s canary CANARY_OK
free_checked_s valid OK
free_checked_s double REFUSED
free_checked_s invalid REFUSED
free_checked_s null DEFINED
free_checked_s canary CANARY_OK
aligned_alloc_s valid OK
aligned_alloc_s overflow REFUSED
aligned_alloc_s zero DEFINED
aligned_alloc_s bad_align REFUSED
aligned_alloc_s inject REFUSED
aligned_alloc_s canary CANARY_OK
```

Rust stream (same except the six `BLOCKED` lines):

```
realloc_s invalid BLOCKED
free_checked_s invalid BLOCKED
aligned_alloc_s valid BLOCKED
aligned_alloc_s zero BLOCKED
aligned_alloc_s bad_align BLOCKED
aligned_alloc_s canary BLOCKED
```

Driver summary line and exit status:

```
METHOD_ALLOC 27 21 0 6
DRIVER_EXIT=0
```

Both harnesses exited 0 under `/usr/bin/timeout 30` (no timeout). C and Rust
token streams were byte-identical across three consecutive runs.

## Explicit non-equivalence statement

These two columns are **not** the same kind of safety guarantee.

- **C+ (SafeCRT)** keeps the ordinary C signatures (`void *malloc_s(size_t)`,
  `void free_s(void *)`, ...). Safety is enforced **at runtime**: a live
  allocation metadata table plus a bounded recent-free cache detect double
  free, invalid/foreign pointers, stale aliases and overflows, and the APIs
  return `NULL` / `CPLUS_SAFE_EINVAL` with `errno` set. Dangerous code still
  *compiles and runs*; it is refused when it executes. The metadata table is
  finite, so this is diagnosis/mitigation, not a proof of lifetime safety
  (as the header itself states).
- **Rust (safe subset)** enforces ownership, initialization, and single-drop
  **at compile time**. An invalid or double free, or a forged/foreign pointer,
  is unrepresentable in safe Rust — it does not compile, so there is no
  runtime refusal to observe. That is why the two pointer-validity cases and
  the four caller-specified-alignment cases are reported as `BLOCKED`: the
  scenario cannot be constructed with safe APIs, not because Rust failed the
  check. `Vec`/`Box` also cannot express a runtime-chosen alignment, so
  `aligned_alloc_s` is only comparable on its size/availability dimension
  (`overflow`, `inject`); its alignment-specific behavior is `BLOCKED`.
- Consequently a `MATCH` means **the observable outcome token agrees** for a
  case that is expressible in both languages (e.g. "oversized request is
  refused", "failed growth preserves the original", "guard bytes intact").
  It does **not** mean the two mechanisms are equivalent: C+ pays runtime
  bookkeeping to reject the operation, while Rust prevents the operation from
  existing. `BLOCKED` is the honest marker of that difference and is never
  counted as a mismatch.

No live-game, injected-DLL, compiler, or runtime sources were modified; only
the five files above were added under this directory.
