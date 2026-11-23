# Native C+ vs Rust safety-outcome matrix (measured 2026-11-23)

Source pair: `safety_matrix.cp` (C+) and `safety_matrix.rs` (safe Rust, no
`unsafe`). Each row is the *same* unsafe situation expressed with the checked
contracts each language actually offers. This document records **observed
outcomes only**: exact exit codes, return codes, and diagnostic strings.

> **Do not read outcome-token equality as equivalent guarantees.** C+ enforces
> these contracts at **run time** through checked runtime APIs (and its fail-fast
> trap). Rust makes several of the same situations **unrepresentable at compile
> time** (no null references, no use-after-move, no second owner) and uses
> checked return values for the rest. The mechanisms and the strength of the
> guarantees are different; only the observed tokens are comparable.

## Environment

- Host: macOS arm64, Darwin 27.2.0, Apple M3 Pro (`arm64`, T8112).
- C+ driver: `C+ 0.1.0`, backend host Clang/LLVM, `--release` (`-O2`), full
  safe mode (UBSan/bounds/null/alignment instrumentation) for the primary run.
- Backend C compiler: Apple clang 16.0.0 (`clang-1600.0.26.6`, `/usr/bin/clang`).
- rustc 1.100.0 (`0ef1bc386 2026-11-09`), `--edition 2021`.
- Date: 2026-11-23. All runs used `timeout 30` (no timeout was reached).

### Build caveat (honest)

This checkout has a transient toolchain mismatch: the installed `build/c+`
driver resolves its runtime source from a sibling `c-plus-core` tree but its
include path from this repo's `runtime/include`, and the two disagree about
`CPLUS_EXIT_ARITHMETIC`. A plain `c+ build --release` therefore fails *before
reaching this source* with:

```
runtime/cplus_runtime.c:130:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

The matrix was built with `-DCPLUS_EXIT_ARITHMETIC=110` passed through to the
runtime recompile (110 is the value in the sibling tree's header). This changes
no scenario: the identical outcome table was reproduced with
`--no-sanitize` (no UBSan). All refusals come from explicit checked runtime
API calls, not from UBSan.

## Exact reproduction commands (from the repo root)

```sh
mkdir -p <OUT>   # any writable directory outside the repo
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  ./build/c+ build --release benchmarks/safety_compare/safety_matrix.cp \
  -o <OUT>/cplus_final \
  -- -DCPLUS_EXIT_ARITHMETIC=110 -I. -Istdlib/safe -Ilibc \
     -Ilibc/install-include \
     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
timeout 30 <OUT>/cplus_final            # matrix (all recoverable scenarios)
timeout 30 <OUT>/cplus_final fatal      # fatal-safety, SUBPROCESS ONLY

rustc --edition 2021 benchmarks/safety_compare/safety_matrix.rs \
  -o <OUT>/rust_safety_matrix
timeout 30 <OUT>/rust_safety_matrix

rustc --edition 2021 --cfg UAF_COMPILE_REJECT \
  benchmarks/safety_compare/safety_matrix.rs -o <OUT>/rust_uaf_reject
# expected to FAIL compilation; no binary is produced
```

C+ build exits 0; its only build output is four benign
`-Wunused-command-line-argument` warnings because the two static archives are
also visible to the per-file compile step. Rust builds cleanly with no warnings
in the normal configuration.

## Matrix: recoverable scenarios (both programs exit 0)

Classification tokens: `OK` (valid operation completed), `REFUSED` (a checked
API/guard rejected an invalid operation with a defined result), `ABORT`
(fail-fast process termination), `COMPILE_ERROR` (no binary produced).

| ID | Scenario | C+ contract (runtime API) | C+ observed | Rust construct (safe) | Rust observed | Class |
|:--|:--|:--|:--|:--|:--|:--|
| 01 | null argument | `cplus_memcpy_s(NULL,8,src,8)` | `rc=400` (ESNULLP), stdout diag, exit 0 | `copy_checked(None,&src)` | `Err(NullArgument)`, exit 0 | REFUSED / REFUSED |
| 02 | zero-length buffer | `malloc_s(0)` | non-NULL, `tracked_size=1`, exit 0 | `Vec::new()` | `len=0`, exit 0 | OK / OK |
| 03a | exact fit | `cplus_memcpy_s(dmax=8,smax=8)` | `rc=0`, `dst[7]=8`, exit 0 | `copy_checked(Some(&mut buf),&src[..8])` | `Ok(8)`, exit 0 | OK / OK |
| 03b | one over | `cplus_memcpy_s(dmax=8,smax=9)` | `rc=406` (ESNOSPC), stdout diag, exit 0 | same with 9-byte src | `Err(TooSmall)`, exit 0 | REFUSED / REFUSED |
| 04 | integer overflow | `calloc_s(SIZE_MAX,2)` | NULL, `errno=84` (EOVERFLOW), exit 0 | `usize::MAX.checked_mul(2)` | `None`, exit 0 | REFUSED / REFUSED |
| 05 | allocation failure atomicity | `cplus_safe_alloc_fail_after(0)` then `realloc_s(p,64)` | NULL; original `live=1`, `size=32`, `bytes_intact=1`, exit 0 | capacity budget 0 then `try_reserve_exact` | `Err`; original `len=32`, bytes intact, exit 0 | REFUSED / REFUSED |
| 06 | use-after-drop | `cplus_safe_alloc_get_meta` → `free_s` → `cplus_safe_ptr_was_freed`, `-check_generation`, then `cplus_memcpy_s` | `meta=0`, `was_freed=1`, `stale_gen=1`, `memcpy_rc=400`, UAF diag on stderr, exit 0 | runtime model: `Option::take()` twice | second = `None`, exit 0 | REFUSED / REFUSED (runtime) |
| 06c | use-after-drop, compile-only | n/a (C+ has no row: it uses runtime metadata) | n/a | `drop(value); println!("{}", *value)` | `error[E0382]`, exit 1, no binary | n/a / **COMPILE_ERROR** |
| 07 | double drop | `free_checked_s(p)` twice | `rc=0` then `rc=22` (EINVAL), exit 0 | `Option::take()` twice | first `Some`, second `None`, exit 0 | REFUSED / REFUSED |

Both programs' final stdout line is `MATRIX_DONE 8`.

### Exact C+ stdout (matrix mode)

```
IGNORE CONSTRAINT HANDLER: (400) memcpy_s: dest is null
SCENARIO 01_null_argument REFUSED cplus_memcpy_s(dest=NULL) rc=400 (ESNULLP=400)
SCENARIO 02_zero_length OK malloc_s(0) non-NULL tracked_size=1
SCENARIO 03a_exact_fit OK cplus_memcpy_s(dmax=8,smax=8) rc=0 dst[7]=8
IGNORE CONSTRAINT HANDLER: (406) memcpy_s: slen exceeds max
SCENARIO 03b_one_over REFUSED cplus_memcpy_s(dmax=8,smax=9) rc=406 (ESNOSPC=406)
SCENARIO 04_integer_overflow REFUSED calloc_s(SIZE_MAX,2)=NULL errno=84 (EOVERFLOW=84 here)
SCENARIO 05_alloc_atomicity REFUSED realloc_s=NULL original_live=1 size32=1 bytes_intact=1
SCENARIO 06_use_after_drop REFUSED meta=0 was_freed=1 stale_gen=1 memcpy_rc=400
SCENARIO 07_double_drop REFUSED free_checked_s rc=0 then rc=22 (EINVAL=22)
MATRIX_DONE 8
```

Exact C+ stderr (matrix mode), with the non-deterministic address shown as
observed in this run:

```
[CPLUS-SAFE] memcpy_s: use-after-free detected (pointer 0x103c29780 was already freed)
```

The `IGNORE CONSTRAINT HANDLER:` lines are emitted by the vendored safeclib
handler (constraint mode is explicitly set to IGNORE so refusals stay
recoverable). `rc=400` is `ESNULLP`, `rc=406` is `ESNOSPC`, `rc=22` is
`CPLUS_SAFE_EINVAL`. `errno=84` is `EOVERFLOW` **on macOS arm64**; Linux uses
75 — the value is printed from the host macro, not hard-coded.

### Exact Rust stdout (normal build)

```
SCENARIO 01_null_argument REFUSED copy_checked(dest=None) -> Err(NullArgument); null &T is unrepresentable
SCENARIO 02_zero_length OK Vec::new() len=0 (defined; C+ malloc_s(0) reports tracked_size=1)
SCENARIO 03a_exact_fit OK copy_checked(dmax=8,smax=8) -> Ok(8)
SCENARIO 03b_one_over REFUSED copy_checked(dmax=8,smax=9) -> Err(TooSmall); raw buf[8] would panic
SCENARIO 04_integer_overflow REFUSED usize::MAX.checked_mul(2) -> None
SCENARIO 05_alloc_atomicity REFUSED budget=0 reserve -> Err; original len=32 bytes_intact (atomic)
SCENARIO 06_use_after_drop_runtime REFUSED Option::take() twice -> second=None (no second owner in safe Rust)
SCENARIO 07_double_drop REFUSED Option::take() first=Some second=None (second drop refused)
MATRIX_DONE 8
```

Rust stderr is empty; exit 0.

## Compile-only rejection: Rust use-after-drop (E0382)

Command: `rustc --edition 2021 --cfg UAF_COMPILE_REJECT ...`. **Exit code 1**,
no output binary. Exact stderr:

```
error[E0382]: borrow of moved value: `value`
   --> benchmarks/safety_compare/safety_matrix.rs:180:20
    |
178 |     let value = Box::new(7u8);
    |         ----- move occurs because `value` has type `Box<u8>`, which does not implement the `Copy` trait
179 |     drop(value); // moves `value` into drop
    |          ----- value moved here
180 |     println!("{}", *value); // E0382: borrow of moved value `value`
    |                    ^^^^^^ value borrowed here after move
    |
help: consider cloning the value if the performance cost is acceptable
    |
179 |     drop(value.clone()); // moves `value` into drop
    |               ++++++++

error: aborting due to 1 previous error

For more information about this error, try `rustc --explain E0382`.
```

This is the decisive difference for scenario 06: C+ **runs** a freed-pointer
operation and refuses it using a live-allocation/generation metadata table;
Rust **never produces a program** for the equivalent use-after-move. The Rust
runtime row (06) is only an `Option`-based model of "no second release can be
produced"; it is not evidence that Rust executed an invalid release.

## Fatal-safety (SUBPROCESS ONLY): C+ fail-fast trap

The default `cplus_runtime_set_violation_trap` state is ON (fail-fast). `fatal`
mode leaves it on and performs a checked use-after-free; the runtime prints a
diagnostic and calls `_exit(70)`. Run it only in a subprocess; never in the
matrix process.

- Command: `timeout 30 <OUT>/cplus_final fatal` → **exit 70**.
- stdout: `SCENARIO fatal_uaf ABORT about to call cplus_memcpy_s on freed block (trap ON)`
- stderr: `[CPLUS-SAFE] memcpy_s: use-after-free detected (pointer 0x101a95780 was already freed)`

Classification: **ABORT / exit 70**. The pointer address varies per run. Safe
Rust has no equivalent "runtime invalid-free abort": the equivalent source is
rejected by the borrow checker (above). A Rust `panic!` would be a *defined*
unwind/abort of a valid program, which is a different mechanism and was not
used here. No Rust ABORT row is claimed.

## Honest interpretation (where the guarantees differ)

- **Runtime vs compile time.** C+ scenarios 01, 03b, 04, 05, 06, 07 and the
  fatal path are all decided by *called runtime APIs* (`cplus_memcpy_s`,
  `calloc_s`, `realloc_s`, `free_checked_s`, the allocation metadata table, and
  the violation trap). Rust rejects a null `&T` and a use-after-move at compile
  time; the other rows use checked returns (`Result`/`Option`/`checked_mul`).
- **Zero length is defined but not identical.** `malloc_s(0)` yields a tracked
  1-byte block; `Vec::new()` has length 0. Both are `OK`, but the observable
  sizes differ.
- **Bounds refusal vs panic.** C+ refuses a one-over copy with `ESNOSPC`. The
  Rust row refuses via a checked length test; a direct `buf[8]` would *panic*
  (a defined program termination), not corrupt memory. These are different
  failure modes with the same `REFUSED` token in this matrix.
- **Failure injection vs budget.** C+ has a real allocator-failure hook
  (`cplus_safe_alloc_fail_after`). Rust has no equivalent, so row 05 uses an
  explicit capacity budget plus `try_reserve_exact`. It is a capacity check,
  not a simulated OS OOM. Do not treat the two as the same mechanism.
- **UAF metadata vs borrow checker.** C+ detects a stale pointer at run time
  using a bounded live-allocation table and generation numbers; that is a
  runtime diagnosis, not a proof of lifetime safety. Rust's borrow checker
  rejects the same pattern statically, at the cost of rejecting programs the
  C+ runtime would accept and check.
- **Double release.** C+ actually executes a second release and has it refused
  by a bounded free table (`CPLUS_SAFE_EINVAL`); Rust cannot express a second
  owner at all, so the row models the refusal with `Option::take()`.
- **Scope.** These are seven targeted scenarios on one host. They are not a
  general memory-safety claim about either language, and the shared
  `REFUSED`/`OK` tokens must not be reported as "equivalent safety".
