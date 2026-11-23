# rust_examples/alloc — safe-Rust examples for every checked alloc/free API

Scope: **only** `benchmarks/rust_examples/alloc/`. This is a safety-outcome
comparison, not a performance benchmark. Each case prints one deterministic
line `<api> <case> <token>`. Both C archives are linked into the C harness;
the Rust program is pure safe Rust and links neither archive.

## Files

- `harness.c` — C99 harness calling **every** checked allocation/free API in
  scope with the full case set.
- `alloc.rs` — safe-Rust examples using `Vec::with_capacity`,
  `Vec::try_reserve_exact`, `Box`, `Option::take`, `checked_mul` and
  `std::alloc::Layout`. **No `unsafe`, no raw pointers, no FFI.**
- `compare.sh` — driver: verifies symbols with `nm`, builds both under
  `timeout 30`, runs both under `timeout 30`, diffs tokens, prints
  `RUST_EXAMPLES alloc <apis> <cases> <matched> <mismatched> <blocked>`.
  Exits nonzero **only** on a real token mismatch. Absent symbols are reported
  as `BLOCKED` with `nm` evidence, never fabricated matches.
- `results/` — generated binaries, raw token streams, `.err`/`.exit`/`nm`
  logs (gitignored by `results/.gitignore`).
- `NOTES.md` — this file.

## Toolchains and exact commands (from repo root, branch `agent/modern-stdlib`)

```
$ /usr/bin/clang --version | head -1
Apple clang version 16.0.0 (clang-1600.0.26.6)
$ rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)
$ python3 --version
Python 3.12.1
$ command -v timeout
/usr/bin/timeout
```

```sh
/usr/bin/clang -std=c99 -O1 -g -I. -Ilibc/install-include -Ithird_party/safeclib/include \
  benchmarks/rust_examples/alloc/harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o benchmarks/rust_examples/alloc/results/harness

rustc -O benchmarks/rust_examples/alloc/alloc.rs \
  -o benchmarks/rust_examples/alloc/results/alloc_rust

timeout 30 benchmarks/rust_examples/alloc/results/harness > .../results/c.out 2> .../results/c.err
timeout 30 benchmarks/rust_examples/alloc/results/alloc_rust > .../results/rust.out 2> .../results/rust.err
```

Run the whole thing with:

```sh
sh benchmarks/rust_examples/alloc/compare.sh
```

`-I.` is required for `#include "stdlib/safe/safe_alloc.h"` (it also pulls in
`safe_error.h`); `-Ilibc/install-include` is required for `cplus_libc.h`.
`-lm -pthread` match the repository's own allocation-fault-matrix link flags.
Safecrt is linked **before** hardened because the safecrt bookkeeping calls
hardened-libc helpers.

## nm evidence (captured before writing any harness)

`compare.sh` re-runs these checks on every invocation and aborts the C build
if a symbol is absent. macOS prefixes C symbols with `_`; the driver greps
` T _?<symbol>$`.

```
nm -g --defined-only build/safe/libcplus_safecrt.a | grep -E '...'
  0000000000000000 T _malloc_s
  00000000000001e4 T _calloc_s
  0000000000000470 T _realloc_s
  0000000000000bf4 T _free_s
  0000000000000a5c T _free_checked_s
  0000000000000324 T _aligned_alloc_s
  0000000000000de8 T _cplus_safe_ptr_is_live
  0000000000000f60 T _cplus_safe_ptr_size
  0000000000000e90 T _cplus_safe_ptr_was_freed
  00000000000014c8 T _cplus_safe_alloc_fail_after
  0000000000001504 T _cplus_safe_alloc_fail_reset

nm -g --defined-only build/libc/libcplus_hardened.a | grep -E '...'
  0000000000000354 T _cplus_malloc
  0000000000000440 T _cplus_calloc
  0000000000000554 T _cplus_realloc
  0000000000000acc T _cplus_free
  00000000000008c0 T _cplus_free_checked
  0000000000000000 T _cplus_aligned_alloc
  00000000000013fc T _cplus_alloc_fail_after
  000000000000143c T _cplus_alloc_fail_reset
  0000000000001384 T _cplus_alloc_double_free_count
  00000000000013c0 T _cplus_alloc_invalid_free_count
```

**Name mapping for the task's shorthand.** The task writes
`alloc_fail_after/reset` and `ptr_was_freed`; there is **no** standalone
`ptr_was_freed` symbol. `nm -g build/safe/libcplus_safecrt.a | grep ptr_was_freed`
returns only the undefined reference and the definition
`0000000000000e90 T _cplus_safe_ptr_was_freed`. Likewise the failure switches
are exported as `cplus_safe_alloc_fail_after`/`cplus_safe_alloc_fail_reset`
(safecrt) and `cplus_alloc_fail_after`/`cplus_alloc_fail_reset` (hardened).
All of them are covered, under their real exported names.

## Case / token design

Tokens: `OK` (valid alloc/free), `REFUSED` (overflow / double / invalid /
injection refused), `DEFINED` (zero-size or NULL no-op), `PRESERVED` (failed
growth left the original block live and untouched), `CANARY_OK` (8-byte
`0xDEADBEEFCAFEBABE` guard at offset 64 survived), `LIVE`/`NOT_LIVE`/`SIZED`/
`ONE_BYTE`/`FREED`/`NOT_FREED`/`DETECTED` (metadata and counter observations),
`MISMATCH` (C+ behavior disagreed with its documented contract), `BLOCKED`
(not expressible in safe Rust). The driver classifies a case as `BLOCKED` when
**either** side says `BLOCKED`; those are never counted as mismatches.

- Overflow asserts the real contract: `EINVAL` for `malloc_s(SIZE_MAX)`,
  `realloc_s(p, SIZE_MAX)`, `aligned_alloc_s(16, SIZE_MAX)` and bad alignment;
  `EOVERFLOW` for `calloc_s(SIZE_MAX, 4)`. The hardened layer has no
  `RSIZE_MAX_MEM` gate, so `cplus_malloc(SIZE_MAX)` and
  `cplus_calloc(SIZE_MAX, 4)` return `NULL` with `ENOMEM`, and
  `cplus_aligned_alloc(16, SIZE_MAX)` fails the `size % alignment == 0`
  constraint with `EINVAL`. The **observable outcome is still refusal**, so
  the token is `REFUSED` for all of them.
- `SIZE_MAX` cannot be reserved by `try_reserve_exact` (exceeds `isize::MAX`)
  and `Layout::array::<u8>(usize::MAX)` is `Err`, so overflow and injection
  cases produce a genuine `REFUSED` on the Rust side, not a hard-coded string.
- Failure injection uses `*_fail_after(0)` / `*_fail_reset()`, so the refusal
  is deterministic rather than a real OOM. For `realloc_s`/`cplus_realloc` the
  harness additionally asserts the original block is still live at its
  original 32-byte size; Rust models this with `try_reserve_exact` returning
  `Err` while the buffer contents are unchanged — the token is `PRESERVED` on
  both sides.
- Double/invalid free refusal is checked with `free_checked_s` return codes,
  `errno`, and the `cplus_alloc_double_free_count` /
  `cplus_alloc_invalid_free_count` deltas. On the Rust side `Option::take`
  makes a second "free" a `None` no-op — `REFUSED` without heap corruption.
- `cplus_aligned_alloc` requires `size % alignment == 0`, so the canary case
  uses a 80-byte request (a multiple of 16) rather than the 72-byte `TOTAL`.
  A runtime probe confirmed `cplus_aligned_alloc(16, 0)` returns a valid
  16-byte-aligned live block on this host (`DEFINED`).
- `cplus_aligned_alloc` delegates straight to host `aligned_alloc()` and
  never consults the hardened fail-injection counter (verified in
  `libc/src/stdlib/stdlib_c17.c`). Its `inject` case is therefore `BLOCKED`
  on **both** sides — an honest marker that the dimension is not observable
  for that API.

## Results (verbatim, repeated runs byte-identical)

```
RUST_EXAMPLES alloc 21 77 53 0 24
DRIVER_EXIT=0
```

21 distinct APIs, 77 cases, 53 matched, 0 mismatched, 24 blocked. Both
harnesses exited 0 under `timeout 30` (no timeout). Full per-case table is at
`results/driver.out`; raw streams at `results/c.out` and `results/rust.out`.

The 24 `BLOCKED` cases:

```
realloc_s invalid, free_s invalid, free_checked_s invalid            (3)
cplus_realloc invalid, cplus_free invalid, cplus_free_checked invalid (3)
aligned_alloc_s valid / zero / bad_align / canary                     (4)
cplus_aligned_alloc valid / zero / bad_align / inject / canary        (5)
cplus_safe_ptr_is_live live / freed / foreign                         (3)
cplus_safe_ptr_size live / zero                                       (2)
cplus_safe_ptr_was_freed live / after_free                            (2)
cplus_alloc_double_free_count double, cplus_alloc_invalid_free_count invalid (2)
```

Driver edge cases were exercised explicitly with temporary patched copies
(removed afterward):

- Replacing the safecrt archive path with a non-existent archive produced
  `RUST_EXAMPLES alloc 21 77 0 0 77`, exit 0, with one
  `BLOCKED absent symbol: <name>` line and the failed `nm ... -> no match`
  line per symbol in `results/nm_evidence.txt`. Absent symbols are `BLOCKED`,
  never fabricated matches.
- Flipping one emitted Rust token to `MISMATCH_SIM` produced
  `RUST_EXAMPLES alloc 21 77 52 1 24`, exit **1** — the driver does exit
  nonzero on a real mismatch.

## Explicit non-equivalence statement

The C and Rust columns are **not** the same kind of safety guarantee.

- **C+ (SafeCRT + hardened libc)** keeps ordinary C signatures
  (`void *malloc_s(size_t)`, `void free_s(void *)`, `void *cplus_malloc(size_t)`,
  `int cplus_free_checked(void **)`, ...). Safety is enforced **at runtime** by
  a live-allocation metadata table, a bounded recent-free cache and explicit
  errno/return-code contracts. Dangerous code still *compiles and runs*; it is
  refused when executed. The caches are finite, so this is diagnosis and
  mitigation (as `safe_alloc.h` itself states), not a proof of lifetime safety.
- **Rust (safe subset)** enforces ownership, initialization and single-drop
  **at compile time**. A double free, a forged/foreign pointer, or a
  use-after-free is unrepresentable in safe Rust — it does not compile — so
  there is no runtime refusal to observe. Those pointer-validity cases are
  therefore `BLOCKED`, not fabricated `REFUSED` tokens. `align_of::<T>()` is a
  compile-time property and there is no safe stable API for a caller-chosen
  runtime alignment, so the alignment-specific `aligned_alloc_s` /
  `cplus_aligned_alloc` cases are `BLOCKED` too. Metadata queries
  (`cplus_safe_ptr_is_live`, `cplus_safe_ptr_size`, `cplus_safe_ptr_was_freed`)
  and the global double/invalid-free counters are C runtime bookkeeping about
  raw addresses; safe Rust has no equivalent observable, so they are `BLOCKED`.
- The global fail-injection switches have no safe-Rust analogue. For those
  cases only the **observable outcome** is compared: a real fallible
  `try_reserve_exact` refusal (`REFUSED`) or a succeeding `Vec::with_capacity`
  (`OK`). This is not a claim that Rust has an equivalent global switch.
- Consequently `MATCH` means **the observable outcome token agrees** for a
  case expressible in both languages (oversized request refused, failed growth
  preserves the original, guard bytes intact, second free is a no-op). It does
  **not** mean the mechanisms are equivalent: C+ pays runtime bookkeeping to
  reject the operation, while Rust prevents the operation from existing.
  `BLOCKED` is the honest marker of that difference and is never a mismatch.

No compiler, runtime, source, git index, or files outside this directory were
modified.
