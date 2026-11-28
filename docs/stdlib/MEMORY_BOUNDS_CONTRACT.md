# MEMORY_BOUNDS_CONTRACT — thin, additive bounds-aware memory wrapper

Status: **implemented and verified** (three compiler modes, 61 checks, 0 failures).

Files owned by this work (no other files were modified):

| File | Role |
| --- | --- |
| `stdlib/memory.hp` (new) | The additive wrapper module. Header-only; no runtime change. |
| `tests/stdlib/deep_memory_bounds.cp` (new) | Self-checking deep-corpus test; auto-discovered by `deep_*.cp`. |
| `docs/stdlib/MEMORY_BOUNDS_CONTRACT.md` (new) | This contract. |

No runners, compilers, runtime sources, or existing public APIs were changed.

---

## 1. Goal and non-goals

Goal: let a caller ask "is this write in range?" for a **tracked** stdlib
allocation, and refuse it with a normal `STDLIB_*` error instead of corrupting
memory — **without** introducing fat pointers, changing any ABI, or touching
the runtime.

Non-goals (explicit):

- No new pointer representation. A pointer stays a `void *` / `T *`. There is
  no `(ptr, len, cap, gen)` descriptor type and no new allocation header.
- No change to `stdlib_alloc`, `stdlib_release`, the error class, or the
  metadata table layout.
- No attempt to protect memory written by code that does not call these
  helpers.

The design is **additive**: every existing public API is unchanged, and
`stdlib/memory.hp` is only active when a translation unit includes it.

---

## 2. Verified foundation (names checked against source before use)

The wrapper reuses the metadata queries that already exist in
`stdlib/error.hp`. Exact names and signatures verified in that header:

```c
size_t stdlib_alloc_size(const void *ptr);                    /* bytes, 0 if untracked */
size_t stdlib_alloc_elem_size(const void *ptr);
size_t stdlib_alloc_length(const void *ptr);                  /* bytes / elem_size */
unsigned long long stdlib_alloc_generation(const void *ptr);
int stdlib_alloc_check_generation(const void *ptr, unsigned long long gen);
int stdlib_alloc_query_length(const void *ptr, size_t elem_size, size_t *out_count); /* 0 ok, -1 fail */
int stdlib_transfer_ownership(void **dst, void **src);        /* bumps generation on success */
void stdlib_release(void *ptr);                               /* NULL is a no-op */

void error_reset(void *target);
void error_fail(void *target, cplus_exit_code code, const char *message);
void error_fail_cat(void *target, int category, cplus_exit_code code, const char *message);
```

Relevant constants: `STDLIB_OK` (0), `STDLIB_FAIL` (-1),
`STDLIB_CATEGORY_INVALID` (1), `STDLIB_CATEGORY_BOUNDS` (5),
`STDLIB_CATEGORY_OWNERSHIP` (6), `STDLIB_CATEGORY_LIFECYCLE` (7),
`CPLUS_EXIT_BOUNDS` (101), `CPLUS_EXIT_NULL_POINTER` (102),
`CPLUS_EXIT_USE_AFTER_FREE` (103), `CPLUS_EXIT_TYPE_CONFUSION` (105),
`CPLUS_EXIT_OWNERSHIP` (106).

Key property relied upon: `stdlib_alloc_size()` returns **0** for NULL *and*
for untracked pointers, and a zero-byte `stdlib_alloc(0)` request is recorded
with a 1-byte minimum. Therefore `cap == 0` is an unambiguous "not a tracked
live block" signal.

---

## 3. Public API added (`stdlib/memory.hp`)

All functions are `static inline`, header-only, and fail-closed. Return value
convention: `STDLIB_OK` on success, `STDLIB_FAIL` on refusal. On refusal the
supplied `error *err` is populated via `error_fail_cat()`; passing `err == NULL`
is allowed (the failure is still reported to the callback / telemetry through
`stdlib_note()`).

### 3.1 Capacity / length queries (pure)

```c
size_t stdlib_mem_capacity(const void *ptr);
int    stdlib_mem_count(const void *ptr, size_t elem_size, size_t *out_count);
```

- `stdlib_mem_capacity` = `stdlib_alloc_size(ptr)`. Returns bytes, or 0 for
  NULL / untracked. Use it to size a buffer before a copy.
- `stdlib_mem_count` = `stdlib_alloc_query_length(...)`. Returns 0 on success,
  -1 on failure; `*out_count` is zeroed on failure. `elem_size == 0` means "use
  the recorded element size".

### 3.2 Generation tokens

```c
unsigned long long stdlib_mem_token(const void *ptr);
int stdlib_mem_check_token(const void *ptr, unsigned long long token, error *err);
int stdlib_mem_check(const void *ptr, unsigned long long token, error *err);
```

- `stdlib_mem_token` = `stdlib_alloc_generation(ptr)`; 0 for NULL/untracked.
- `stdlib_mem_check_token` checks *only* generation. `token == 0` opts out of
  the check. A mismatch fails with `STDLIB_CATEGORY_LIFECYCLE` /
  `CPLUS_EXIT_USE_AFTER_FREE`.
- `stdlib_mem_check` checks tracked-ness **and** generation (the token check is
  only reached once the pointer is known to be tracked).

### 3.3 Guarded writes — capacity is queried before the library call

```c
int stdlib_mem_copy(void *dst, size_t dst_off, const void *src, size_t src_off,
                    size_t n, unsigned long long dst_token, error *err);
int stdlib_mem_move(void *dst, size_t dst_off, const void *src, size_t src_off,
                    size_t n, unsigned long long dst_token, error *err);
int stdlib_mem_set(void *dst, size_t dst_off, int value, size_t n,
                   unsigned long long dst_token, error *err);
```

Validation order (all checks happen **before** any `memcpy`/`memmove`/`memset`):

1. `dst` non-NULL and tracked; `src` non-NULL and tracked (for copy/move).
2. `off <= capacity` and `n <= capacity - off` for **both** ranges. The check
   uses subtraction, so `off + n` can never overflow `size_t`.
3. `dst_token` matches the destination generation when non-zero.
4. `n == 0` is a valid no-op *after* validation; for `n > 0` the corresponding
   CRT call is issued on `base + offset`.

`copy` requires non-overlapping ranges (caller responsibility); `move` uses
`memmove` and permits overlap. Nothing is written on any refusal.

### 3.4 Borrow / own transfer helpers

```c
int stdlib_mem_borrow(const void *ptr, size_t off, size_t n,
                      unsigned long long token, error *err);
int stdlib_mem_transfer(void **dst, void **src, error *err);
void stdlib_mem_release(void **ptr);
```

Semantics (documented so callers cannot misuse them accidentally):

- **Borrow** — *non-owning*. Validates that `ptr` is tracked, `[off, off+n)`
  fits, and the optional token is current. It does not transfer, free, or
  change any generation. The caller retains ownership and must not release the
  block while the borrow is live. Returns `STDLIB_OK` / `STDLIB_FAIL`.
- **Own transfer** — thin, error-reporting wrapper over
  `stdlib_transfer_ownership()`. On success `*dst` receives the block, `*src`
  is set to NULL, and the block's generation is bumped — so tokens captured
  before the transfer become stale. Requires `*dst == NULL` and `*src` to be a
  live stdlib block; otherwise refuses with
  `STDLIB_CATEGORY_OWNERSHIP` / `CPLUS_EXIT_OWNERSHIP` and changes nothing.
- **Release** — calls `stdlib_release(*ptr)` then sets `*ptr = NULL`, making
  repeated cleanup safe (a second call on the same handle is a no-op). Never
  pass a raw `malloc()` pointer.

### 3.5 Refusal error mapping ("STDLIB_* error")

| Condition | Return | `error.category()` | `error.code()` |
| --- | --- | --- | --- |
| NULL destination or source | `STDLIB_FAIL` | `STDLIB_CATEGORY_INVALID` | `CPLUS_EXIT_NULL_POINTER` |
| Pointer is not a tracked stdlib block | `STDLIB_FAIL` | `STDLIB_CATEGORY_INVALID` | `CPLUS_EXIT_TYPE_CONFUSION` |
| Offset/length exceeds capacity | `STDLIB_FAIL` | `STDLIB_CATEGORY_BOUNDS` | `CPLUS_EXIT_BOUNDS` |
| Stale generation token | `STDLIB_FAIL` | `STDLIB_CATEGORY_LIFECYCLE` | `CPLUS_EXIT_USE_AFTER_FREE` |
| Invalid ownership transfer | `STDLIB_FAIL` | `STDLIB_CATEGORY_OWNERSHIP` | `CPLUS_EXIT_OWNERSHIP` |

`error` records are sticky until `error_reset()` / `error.clear()` — a
successful call does not clear a previously recorded failure. Tests reset the
record explicitly before checking a success (this is standard `error.hp`
behaviour, not a wrapper quirk).

---

## 4. Coverage of `tests/stdlib/deep_memory_bounds.cp`

The test is self-checking: each check prints `<tag> <0|1>` and the final line is
`MEMORY_BOUNDS <checks> <failures>`; exit status is 0 iff `failures == 0`. It is
named `deep_*.cp`, so `tests/stdlib/run_deep_tests.sh` auto-discovers it — no
runner edit was made. No `.expected` fixture was added (strict file ownership);
the harness therefore lists it under `NO-EXPECTATION (exit code only)`.

| Requirement | Checks (tags) |
| --- | --- |
| empty / zero size | `z_copy_zero`, `z_set_zero`, `z_zero_error_clean`, `z_alloc_zero_tracked`, `z_alloc_zero_cap1`, `z_alloc_zero_set1`, `z_alloc_zero_set2_refused`, `i_off_eq_cap_zero` |
| exact fit | `x_copy_exact`, `x_set_exact`, `x_move_exact` |
| one-over | `o_copy_over`, `o_copy_over_no_write`, `o_set_off_over`, `o_set_off_over_no_write`, `o_src_over`, `o_off_gt_cap`, `i_over_from_interior`, `i_over_tail_clean` |
| NULL | `q_cap_null`, `q_count_null`, `n_dst_null`, `n_src_null`, `n_set_null`, `n_borrow_null` |
| stale generation | `g_token_changed`, `g_stale_write_refused`, `g_stale_borrow_refused`, `g_new_token_ok` |
| interior pointer (base + explicit offset) | `i_set_interior`, `i_set_neighbors_clean`, `i_copy_interior`, `i_copy_neighbors_clean` |
| repeated cleanup | `c_release_nulls_handle`, `c_release_null_noop`, `c_single_free_counted` |
| logging off/on parity | `p_callback_off_silent`, `p_telemetry_off_quiet`, `p_callback_on_fires`, `p_telemetry_on_counts`, `p_results_identical` |
| additional: queries / limitations / ownership | `q_*`, `u_stack_untracked`, `u_stack_refused`, `g_borrow_*`, `g_transfer_*` |

Refused writes are verified non-destructively: all reads used to confirm
"no write happened" are in-bounds, so the test is UBSan-clean. Logging parity is
checked by running one fixed mixed scenario twice, once with the error callback
and telemetry disabled and once with both enabled: the result vector
`(rc, failed, category, code)` per step is byte-for-byte identical
(`p_results_identical`), while the enabled run demonstrably fires the callback
and increments the bounds/API-failure counters.

---

## 5. Exact verification commands and results

All commands run from the repository root; `timeout 30` was applied to every
build and run. Compiler: `build/c+` (C+). `OUT` below was an approved temp
directory outside the repository.

### 5.1 strict / no sanitizer (run first, as required)

```sh
timeout 30 build/c+ build tests/stdlib/deep_memory_bounds.cp \
  --strict --sanitize=none -o "$OUT/deep_memory_bounds_strict_none"
# BUILD_RC=0

timeout 30 "$OUT/deep_memory_bounds_strict_none" > "$OUT/strict_none.out" 2> "$OUT/strict_none.err"
# RUN_RC=0 ; stderr byte count = 0
```

Result: `MEMORY_BOUNDS 61 0`, exit 0.

### 5.2 default

```sh
timeout 30 build/c+ build tests/stdlib/deep_memory_bounds.cp \
  -o "$OUT/deep_memory_bounds_default"     # BUILD_RC=0
timeout 30 "$OUT/deep_memory_bounds_default" > "$OUT/default.out" 2> "$OUT/default.err"
# RUN_RC=0 ; stderr byte count = 0
```

Result: `MEMORY_BOUNDS 61 0`, exit 0.

### 5.3 UBSan

```sh
timeout 30 build/c+ build tests/stdlib/deep_memory_bounds.cp \
  --strict --sanitize=undefined -o "$OUT/deep_memory_bounds_ubsan"
# BUILD_RC=0

timeout 30 "$OUT/deep_memory_bounds_ubsan" > "$OUT/ubsan.out" 2> "$OUT/ubsan.err"
# RUN_RC=0 ; stderr byte count = 0
```

Result: `MEMORY_BOUNDS 61 0`, exit 0. No UBSan diagnostics on stderr.

### 5.4 Byte-for-byte stdout comparison

```sh
diff "$OUT/strict_none.out" "$OUT/default.out"   # no output
diff "$OUT/strict_none.out" "$OUT/ubsan.out"     # no output
# ALL_STDOUT_IDENTICAL
```

### 5.5 Auto-discovery through the deep harness

```sh
timeout 120 sh tests/stdlib/run_deep_tests.sh deep_memory_bounds
```

Output:

```
deep stdlib harness: build<=60s, run<=30s, kill-after=5s, compiler=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/c+
pattern(s): deep_memory_bounds
ok       deep_memory_bounds (default): MEMORY_BOUNDS 61 0
ok       deep_memory_bounds (strict-none): MEMORY_BOUNDS 61 0
ok       deep_memory_bounds (ubsan): MEMORY_BOUNDS 61 0

MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL  OUT-FAIL
default             1        0        0           0         0         0
strict-none         1        0        0           0         0         0
ubsan               1        0        0           0         0         0
deep stdlib tests: 3 passed, 0 failed, 0 skipped (1 file(s) x 3 modes)
NO-EXPECTATION (exit code only, no byte-exact fixture): deep_memory_bounds
```

Harness exit status: 0.

### 5.6 Exact stdout (identical in all three modes; 62 lines)

```
q_cap_null 1
q_count_null 1
q_count_null_error 1
q_alloc_ok 1
q_cap_tracked 1
q_count_bytes 1
q_count_elems 1
q_count_recorded 1
q_token_nonzero 1
q_token_untracked 1
z_copy_zero 1
z_set_zero 1
z_zero_error_clean 1
z_alloc_zero_tracked 1
z_alloc_zero_cap1 1
z_alloc_zero_set1 1
z_alloc_zero_set2_refused 1
b_alloc_ok 1
x_copy_exact 1
x_set_exact 1
x_move_exact 1
o_copy_over 1
o_copy_over_no_write 1
o_set_off_over 1
o_set_off_over_no_write 1
o_src_over 1
o_off_gt_cap 1
n_dst_null 1
n_src_null 1
n_set_null 1
n_borrow_null 1
u_stack_untracked 1
u_stack_refused 1
i_alloc_ok 1
i_set_interior 1
i_set_neighbors_clean 1
i_copy_interior 1
i_copy_neighbors_clean 1
i_over_from_interior 1
i_over_tail_clean 1
i_off_eq_cap_zero 1
g_alloc_ok 1
g_current_token_ok 1
g_borrow_current_ok 1
g_borrow_over_refused 1
g_transfer_ok 1
g_token_changed 1
g_stale_write_refused 1
g_stale_borrow_refused 1
g_new_token_ok 1
g_transfer_dst_occupied 1
g_transfer_src_null 1
c_alloc_ok 1
c_release_nulls_handle 1
c_release_null_noop 1
c_single_free_counted 1
p_callback_off_silent 1
p_telemetry_off_quiet 1
p_callback_on_fires 1
p_telemetry_on_counts 1
p_results_identical 1
MEMORY_BOUNDS 61 0
```

stderr was empty in every mode.

---

## 6. Limitations — what is NOT protected

These are deliberate scope boundaries, not defects. The wrapper is a
**host-side API discipline**, not a memory-safety proof.

1. **Arbitrary raw `malloc()` / stack / static pointers are not protected.**
   They have no entry in the stdlib metadata table, so `stdlib_alloc_size()`
   returns 0 and every guarded helper refuses them (`u_stack_untracked`,
   `u_stack_refused`). An untracked pointer is never silently trusted.
2. **Uninstrumented precompiled C libraries are not protected.** A library that
   writes through a raw pointer never calls `stdlib_mem_*`, so no check runs.
   Only writes issued through these wrappers are checked.
3. **Interior pointers must be expressed as `(base, offset)`.** Metadata is
   keyed by exact base address; there is no "find containing block" reverse
   lookup. Passing `base + offset` alone is indistinguishable from an
   untracked pointer and is refused. This is why every guarded helper takes an
   explicit offset parameter.
4. **No compile-time guarantee.** The checks are runtime checks on the path
   through the wrapper. Code that bypasses the wrapper is unchecked.
5. **Overlap discipline for `stdlib_mem_copy`.** Use `stdlib_mem_move` for
   overlapping ranges; the wrapper does not detect overlap for `copy`.
6. **TOCTOU / concurrency.** Capacity and generation are read under the
   metadata lock inside each query, but a concurrent release by another thread
   after a successful check is outside this header's control. Do not release a
   block concurrently with guarded access to it.

### Interface Agent 1 would have to provide for the out-of-scope capabilities

Nothing in the delivered API requires a runtime change, so **no interface from
Agent 1 is needed for the implemented capabilities**. If any of the following
were later required, it cannot be done in this header and would need an Agent 1
runtime interface:

- **Interior-pointer provenance from `base + offset` alone:** add a metadata
  lookup such as
  `int stdlib_alloc_containing(const void *p, void **out_base,
                               size_t *out_offset, size_t *out_capacity)`.
  The current table only supports exact-key lookup (`stdlib_meta_find`), so
  this requires a new indexed/range query in the runtime metadata layer.
- **Automatic interception of writes from uninstrumented code:** requires
  runtime-level instrumentation (e.g. page-protection trapping or compiler
  instrumentation of every store). No header-only API can observe those writes.
- **Bounds for raw `malloc()` / stack / static pointers:** requires allocation
  interposition plus header metadata for all allocations at the runtime layer,
  not an additive stdlib header.

None of these are implemented or claimed here.

---

## 7. Return summary

- **Files:** `stdlib/memory.hp`, `tests/stdlib/deep_memory_bounds.cp`,
  `docs/stdlib/MEMORY_BOUNDS_CONTRACT.md`. No other files touched; no
  add/commit; no compiler/runtime edits.
- **APIs added:** `stdlib_mem_capacity`, `stdlib_mem_count`, `stdlib_mem_token`,
  `stdlib_mem_check_token`, `stdlib_mem_check`, `stdlib_mem_check_range`
  (internal), `stdlib_mem_copy`, `stdlib_mem_move`, `stdlib_mem_set`,
  `stdlib_mem_borrow`, `stdlib_mem_transfer`, `stdlib_mem_release`. All additive
  and header-only; existing APIs unchanged.
- **Test results:** strict/no-sanitize, default, and UBSan each build and run
  with exit 0 under `timeout 30`; `MEMORY_BOUNDS 61 0` with identical stdout and
  empty stderr in all three; harness auto-discovery 3/3 passed, exit 0.
- **Limitations:** raw/untracked pointers and uninstrumented precompiled
  libraries are not protected; interior pointers need explicit `(base, offset)`;
  no compile-time guarantee; no concurrent-release protection. Required Agent 1
  interfaces for the out-of-scope capabilities are listed in §6.
