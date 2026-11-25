# Rust examples: checked runtime-diagnostic and remaining wrapper APIs

All files in this directory were produced under a strict ownership rule: only
`benchmarks/rust_examples/runtime_diag/` was created/modified. No build system,
compiler, runtime, or other repository files were touched. No git add/commit
was performed. Every token in the results below came from an executed binary
under `timeout 30`; nothing is fabricated.

## 1. Scope and file layout

| File | Role |
|---|---|
| `runtime_diag.rs` | Safe-Rust state model. No `unsafe`, no FFI. Uses `Cell`, `RefCell`, `Option`, `Result`, `Vec`, `std::sync::atomic::AtomicI32`. Prints `<api> <case> <token>`. |
| `harness.c` | C99 harness calling the real C+ symbols. Links **both** archives. Prints the same `<api> <case> <token>` lines. Trap cases are isolated in fresh subprocesses. |
| `compare.sh` | Builds both under `timeout 30`, runs the trap cases as subprocesses (recording exit codes), diffs sorted token streams, prints the summary line, exits nonzero on a real mismatch. |
| `NOTES.md` | This file. |

APIs covered (10 families, 43 cases):

1. `runtime_violation_trap` — `cplus_runtime_{set,get}_violation_trap`, `cplus_runtime_violation`
2. `runtime_failstop` — `cplus_runtime_{set,get}_failstop`
3. `runtime_cleanup` — `push_cleanup`, `cleanup_mark`, `cleanup_depth`, `cleanup_unwind_to`, `unwound_cleanups`
4. `runtime_guard` — `cplus_runtime_guard` (OK, ERROR, longjmp-style unwind, NULL body)
5. `runtime_report` — `cplus_runtime_set_handler`, `cplus_runtime_report`
6. `safe_ptr_query` — `ptr_is_live`, `ptr_size`, `ptr_elem_size`, `ptr_count`, `query_length`, `remaining_capacity`, `alloc_get_meta`, `ptr_was_freed`
7. `safe_constraint_mode` — `cplus_safe_{set,get}_constraint_mode`
8. `sc_handler_setters` — `cplus_sc_set_{mem,str}_constraint_handler_s`
9. `except_init` — `cplus_except_init` + TRY/CATCH
10. `atomic_int` — deterministic `cplus_atomic_*_int` wrappers

## 2. Symbol verification with nm (done before writing any harness)

Both archives were checked; `nm` output, not source reading, was treated as the
truth for callability.

```
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
nm -g --defined-only build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  | grep -E ' T _(cplus_runtime_|cplus_safe_|cplus_except_|cplus_sc_set_|cplus_atomic_|malloc_s|calloc_s|free_s)'
```

Defined (`T`) symbols used by `harness.c` (address in the archive object):

`build/safe/libcplus_safecrt.a`
```
0x0000 _cplus_runtime_set_handler          0x003c _cplus_runtime_set_failstop
0x0050 _cplus_runtime_get_failstop         0x005c _cplus_runtime_set_violation_trap
0x0070 _cplus_runtime_get_violation_trap   0x007c _cplus_runtime_violation
0x0128 _cplus_runtime_report               0x01b0 _cplus_runtime_push_cleanup
0x022c _cplus_runtime_pop_cleanup          0x0290 _cplus_runtime_cleanup_mark
0x02b4 _cplus_runtime_cleanup_depth        0x02d8 _cplus_runtime_cleanup_unwind_to
0x0388 _cplus_runtime_unwind_cleanups      0x042c _cplus_runtime_leaked_cleanups
0x0450 _cplus_runtime_unwound_cleanups     0x0474 _cplus_runtime_reset_counters
0x04ac _cplus_runtime_guard
0x0000 _cplus_safe_set_constraint_mode     0x0088 _cplus_safe_get_constraint_mode
0x0c2c _cplus_safe_remaining_capacity      0x0cf0 _cplus_safe_alloc_get_meta
0x0de8 _cplus_safe_ptr_is_live             0x0e90 _cplus_safe_ptr_was_freed
0x0f60 _cplus_safe_ptr_size                0x100c _cplus_safe_ptr_elem_size
0x10b8 _cplus_safe_ptr_count               0x1170 _cplus_safe_query_length
0x0000 _cplus_sc_set_mem_constraint_handler_s
0x0000 _cplus_sc_set_str_constraint_handler_s
0x0000 _cplus_sc_ignore_handler_s          0x0000 _cplus_sc_abort_handler_s
0x0000 _cplus_except_init                  0x0000 _cplus_safe_log_set_level
0x0000 _malloc_s                           0x01e4 _calloc_s
0x0bf4 _free_s
```

`build/libc/libcplus_hardened.a`
```
0x0000 _cplus_atomic_init_int              0x0008 _cplus_atomic_is_lock_free_int
0x0024 _cplus_atomic_store_int             0x004c _cplus_atomic_load_int
0x0080 _cplus_atomic_exchange_int          0x00cc _cplus_atomic_compare_exchange_strong_int
0x0294 _cplus_atomic_compare_exchange_weak_int
0x045c _cplus_atomic_fetch_add_int         0x04a8 _cplus_atomic_fetch_sub_int
```

Notably **absent** from both archives: `cplus_runtime_set_violation_handler`
(there is only `cplus_runtime_set_handler`), `cplus_sc_set_abort_handler_s`, and
any `cplus_runtime_*` symbol for directly reading the cleanup stack contents.
Those were not called.

### Explicit BLOCKED symbols (nm present, but excluded)

| Symbol | nm | Reason |
|---|---|---|
| `cplus_atomic_is_lock_free_int` | `T` in hardened archive | The value is host/ABI-specific. Safe Rust has no guaranteed-matching query (no FFI allowed), so a token comparison would be meaningless. |
| `cplus_atomic_compare_exchange_weak_int` | `T` in hardened archive | C11 weak CAS may fail spuriously even when `*expected == *obj` (7.17.7.4). Single-threaded `compare_exchange_weak` is nondeterministic, so it cannot yield a stable token. |

Both are reported as `BLOCKED ...` by `compare.sh` and are not called by either
harness. They do not affect the exit status.

## 3. Toolchains

```
/usr/bin/clang --version  -> Apple clang version 16.0.0 (clang-1600.0.26.6)
rustc --version           -> rustc 1.100.0 (0ef1bc386 2026-11-09)
uname -a                  -> Darwin 27.2.0, arm64
timeout                   -> /usr/bin/timeout
```
Date of final verified run: 2026-11-23.

## 4. Exact commands

Driver (does all builds, isolated runs, diff and counting):

```
bash benchmarks/rust_examples/runtime_diag/compare.sh
```

Equivalent manual steps (`${ROOT}` = repo root):

```
# C harness: BOTH archives linked
/usr/bin/clang -std=c99 -O2 -pedantic \
  -Istdlib/safe -Ithird_party/exceptions4c \
  -Ithird_party/safeclib/include -Ilibc/install-include \
  benchmarks/rust_examples/runtime_diag/harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o harness_c

# Rust model
rustc -O benchmarks/rust_examples/runtime_diag/runtime_diag.rs -o harness_rs

# In-process cases + isolated trap cases
timeout 30 ./harness_c                    > c.out
timeout 30 ./harness_c --trap-on          > trap_on.out     # exit 70 -> TRAP
timeout 30 ./harness_c --trap-off         > trap_off.out    # exit 0  -> RETURN
timeout 30 ./harness_rs                   > r.out

# Compare sorted token streams
sort c.out | diff -u - <(sort r.out)
```

`--trap-on` reaches `cplus_runtime_violation()` with the trap enabled. The
library prints `[CPLUS-SAFE] harness: forced trap-on probe` to stderr and calls
`_exit(70)`. The driver observes that exact exit code from the **child**
process and normalizes it to the token `TRAP`; the parent never dies.
`--trap-off` sets the trap to 0 first, so the call returns and prints
`RETURN` (child exit 0). Each mode runs in its own process so the global trap
state cannot leak between cases.

## 5. Observed results (final verified run)

`compare.sh` stdout:

```
BLOCKED cplus_atomic_is_lock_free_int nm=present(T) reason=host-specific result; safe Rust has no equivalent guaranteed-matching query
BLOCKED cplus_atomic_compare_exchange_weak_int nm=present(T) reason=weak CAS may fail spuriously (C11 7.17.7.4); nondeterministic
RUST_EXAMPLES runtime_diag 10 43 43 0 2
```

Exit status: `0`. `diff` of sorted streams is empty.

Token stream (identical from both sides; the `trap_on` line is synthesized by
the driver from the child exit code 70):

```
runtime_violation_trap default ON
runtime_violation_trap set_off OFF
runtime_violation_trap set_on ON
runtime_violation_trap trap_on TRAP
runtime_violation_trap trap_off RETURN
runtime_failstop default OFF
runtime_failstop set_on ON
runtime_failstop set_off OFF
runtime_cleanup mark_round_trip OK:3/3
runtime_cleanup null_refusal REFUSED
runtime_cleanup unwind_lifo OK:CB
runtime_cleanup unwound_counter OK:2
runtime_cleanup depth_after_unwind OK:1
runtime_guard body_ok OK
runtime_guard body_error ERROR
runtime_guard unwind_on_violation ERROR:CLEANED
runtime_guard null_body ERROR
runtime_report handler_invoked OK:1
runtime_report null_message OK:1
safe_ptr_query valid_live OK:32/4/8
safe_ptr_query null_refusal REFUSED
safe_ptr_query invalid_refusal REFUSED
safe_ptr_query remaining_base OK:32
safe_ptr_query remaining_interior OK:24
safe_ptr_query remaining_end OK:0
safe_ptr_query remaining_null REFUSED
safe_ptr_query after_free FREED
safe_ptr_query zero_size_alloc OK
safe_constraint_mode default OK:0
safe_constraint_mode set_abort OK:1
safe_constraint_mode invalid_fallback OK:0
sc_handler_setters mem_round_trip OK
sc_handler_setters str_round_trip OK
sc_handler_setters null_refusal OK
except_init idempotent OK
except_init try_catch OK
atomic_int init_load OK:7
atomic_int store_load OK:9
atomic_int exchange OK:9/3
atomic_int fetch_add OK:3/8
atomic_int fetch_sub OK:8/5
atomic_int cas_strong_ok OK:1/42
atomic_int cas_strong_fail OK:0/42
```

Notable observed C+ behaviours:

* `cplus_runtime_violation` with trap ON terminates with **exit 70** even with
  no guard installed (verified: child rc=70). With trap OFF it returns normally.
* `cplus_runtime_cleanup_unwind_to(mark)` runs entries newest-first: after
  pushing `A,B,C` and unwinding to mark 1 the drop order was `CB`, `unwound`
  counter was 2, remaining depth 1.
* Inside `cplus_runtime_guard`, a violation with trap ON / failstop OFF
  unwinds to the guard watermark, runs the body's registered cleanup exactly
  once, and the guard returns `CPLUS_RUNTIME_ERROR` (1) — token `ERROR:CLEANED`.
  `guard(NULL, ...)` also returns `CPLUS_RUNTIME_ERROR`.
* `calloc_s(8,4)` metadata: `ptr_size=32`, `ptr_elem_size=4`, `ptr_count=8`.
  `remaining_capacity` on the base/interior/one-past-end gave 32/24/0; NULL
  gave `(size_t)-1`. After `free_s`, `is_live=0`, `was_freed=1`, `size=0`.
* Constraint mode defaults to 0 (IGNORE); invalid value 99 falls back to 0.
* The setter round trips return the *previous* handler pointer as specified —
  ignore→abort→ignore returns ignore, abort, ignore respectively. Setting NULL
  restores a non-NULL library default handler.
* Strong int CAS: success returns 1 with new value 42; wrong expected returns
  0 and writes the observed value 42 into `expected`.

### Failure-path check

Temporarily replacing the Rust token `ERROR:CLEANED` with `ERROR:DIRTY`
produced:

```
DIFF -runtime_guard unwind_on_violation ERROR:DIRTY
DIFF +runtime_guard unwind_on_violation ERROR:CLEANED
RUST_EXAMPLES runtime_diag 10 43 42 2 2
exit=1
```

The source was then restored byte-for-byte and the final re-run reproduced
`RUST_EXAMPLES runtime_diag 10 43 43 0 2`, `exit=0`.

## 6. Rust modelling (safe APIs only, no `unsafe`)

* Trap/failstop/mode/handler state → `Cell<bool>` / `Cell<i32>` / `Cell<Handler>`
  with getters and setters, giving genuine set/get round trips.
* Cleanup registry → `RefCell<Vec<Option<char>>>`; `Vec::pop` gives the same
  LIFO drop order, and `None` models the refused null-callback push (the C API
  returns `-1`). Counters are `Cell<u64>`.
* Guards → `Result`-style outcome; the unwind case pops the registered cleanup
  and checks that it ran exactly once. `Option<fn()>` models the refused NULL
  body.
* Pointer queries → `Option<Block>` where `Block { byte_size, elem_size }`.
  `None` models null, unowned stack addresses, freed blocks and the
  `(size_t)-1` capacity result; `Block::remaining(offset)` is a checked
  subtraction that saturates at 0 for one-past-the-end.
* Exceptions → an idempotent init flag plus `Result<(), ()>` for the
  THROW/CATCH pair.
* Atomics → `std::sync::atomic::AtomicI32` with `Ordering::SeqCst`, which for
  single-threaded `store/load/swap/fetch_add/fetch_sub/compare_exchange`
  produces exactly the same observable values as the C11 seq_cst wrappers.
* The Rust program does **not** call the C library (that would require
  `unsafe` FFI). It models the *decision outcomes* using language-level safe
  primitives.

## 7. Explicit caveat — matching tokens are NOT equivalent guarantees

A matched token means only that the two programs printed the same word for the
same `(api, case)` label. It does not mean the two languages provide the same
safety property:

* The C+ side exercises the **real** runtime functions. They are runtime
  diagnosis and recovery mechanisms: `cplus_runtime_violation` fails fast with
  `_exit(70)` when unguarded; `cplus_runtime_guard` uses `setjmp`/`longjmp` to
  abandon a guarded body and run explicitly registered cleanups; the
  `cplus_safe_ptr_*` queries consult a runtime metadata table. None of these
  can prove pointer provenance, lifetime, aliasing or ownership statically.
  The cleanup registry also does **not** run C++ scope destructors skipped by
  `longjmp` — only entries explicitly pushed with `push_cleanup` are run.
* The Rust side rejects many of the same situations **at compile time** (no
  null references, no two overlapping `&mut` borrows, no use-after-free) or
  via `Option`/`Result`-returning safe runtime APIs. Its guarantees cover
  provenance, aliasing and lifetimes by construction, which the C checked
  runtime does not attempt.
* Several Rust tokens are deliberate *models* of situations safe Rust cannot
  express (null pointers, invalid/unowned addresses, a null callback or body).
  They are not evidence that a Rust program can pass a null pointer or create
  an overlapping mutable slice.
* `longjmp` in the C path does not unwind Rust-style RAII either; the C harness
  records only the cleanup entries it registered explicitly.

Therefore this comparison demonstrates aligned *observable outcomes* for a
fixed set of deterministic cases, including controlled child-process exit
codes for the fatal trap case. It is **not** evidence of behavioural or
safety-guarantee equivalence between C+ and Rust.
