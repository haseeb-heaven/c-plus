# libc contract status (`libc_contracts.json`)

This document records which machine-readable contract values were brought in line
with the code currently on `agent/modern-stdlib`, what evidence supports each
change, and which claims are deliberately **not** made. It is a status record,
not a new guarantee: where the runtime behavior is heuristic, the contract says
so.

Only `libc/contracts/libc_contracts.json` and this file were changed by this
pass. The validator, `c17_contracts.json`, and `symbol_map.json` were not
modified.

## 1. Contracts corrected

| Contract | Stale / vague value | Replacement (summary) |
| --- | --- | --- |
| `realloc` | `"success-branch alias invalidation (planned)"`; generic `"returns NULL; original valid"` | Concrete generation-guarded success branch in `cplus_realloc`; stale/unowned pointer returns `NULL`/`EINVAL` without freeing another block; stale aliases observable via `cplus_alloc_check_generation`, `cplus_alloc_is_live`, `cplus_alloc_get_meta`; injected failure/OOM preserves original. Added `limitations`, `evidence`; refined `failure` / `failureBehavior` and the thread assumption (host `realloc` runs with the metadata lock released). |
| `free` | `"managed double/invalid release"` with no stated limit | Documents `cplus_free_checked` + bounded recent-free table and stale-alias observation; added limitations for heuristic classification, host-`free` bypass, and cross-allocator UB. |
| `malloc_s` | `"checked API -- exact signature from Agent 2 capability layer"`, `"native/CRT status"` | Real signature `(size_t size)`, `RSIZE_MAX_MEM` guard, metadata/generation behavior, `EINVAL`/`ENOMEM` outcomes, and limitations. |
| `calloc_s` | Same placeholder signature/status | Real signature `(size_t count, size_t size)`, overflow + `RSIZE_MAX_MEM` checks, `elem_size` tracking, `EOVERFLOW`/`EINVAL`/`ENOMEM` outcomes, limitations. |
| `realloc_s` | Same placeholder; no alias behavior described | Real signature `(void *ptr, size_t size)`; ABA generation guard, stale/unowned-pointer refusal, stale-alias detection via `cplus_safe_ptr_check_generation` / `cplus_safe_ptr_was_freed` / `free_checked_s` / `cplus_safe_alloc_get_meta`, failure preservation, and explicit heuristic/cross-allocator limitations. |
| `free_s` | Same placeholder; `ownership: borrow` | Real signature `(void *ptr)`, `consumes` ownership, `free_checked_s` double/invalid-free refusal, stale-alias observation, and heuristic/cross-allocator limitations. |
| `sprintf_s`, `snprintf_s` | Literal-format / no-`%n` / bounds flags read as complete validation | Limitation-only addition: the flags describe specific implemented checks, not general format-string validity or format/argument agreement. No new guarantee asserted. |

Schema keys required by `tests/libc/validate_contracts.py` (`params`,
`ownership`, `failure`, `returnState`) are preserved on every entry. New keys
(`limitations`, `evidence`, `status`-style metadata) are additive and optional;
the validator does not require them.

## 2. Evidence

| Contract | Source | Tests |
| --- | --- | --- |
| `realloc`, `free` | `libc/src/stdlib/alloc.c` (`cplus_realloc` ABA guard at the post-`realloc` metadata update; `meta_find_addr` miss path; `recent_add` / `recent_remove`; `cplus_alloc_check_generation`, `cplus_alloc_is_live`, `cplus_free_checked`) | `tests/libc/test_alloc_metadata.c`, `tests/libc/test_alloc_attack.c`, `tests/safety/test_scenarios_c.c` |
| `malloc_s`, `calloc_s` | `stdlib/safe/src/safe_alloc.c` (`malloc_s`, `calloc_s`, `finish_alloc`, `tab_insert`, `cplus_safe_ptr_check_generation`) | `tests/crt/unit/test_malloc_s.c`, `tests/crt/unit/test_calloc_s.c`, `tests/crt/unit/test_calloc_s_overflow_uninit.c`, `tests/crt/unit/test_alloc_metadata_s.c` |
| `realloc_s` | `stdlib/safe/src/safe_alloc.c` (`realloc_s` ABA guard, `tab_find_addr` miss path, `tab_reserve_one`, `cplus_safe_ptr_was_freed`, `cplus_safe_ptr_check_generation`) | `tests/crt/unit/test_realloc_s.c`, `tests/crt/unit/test_realloc_zero_and_failure.c`, `tests/crt/unit/test_alloc_metadata_s.c`, `tests/crt/adversarial/test_alloc50_realloc_failure_preserve.c`, `tests/scenarios/runtime_01_alloc.c`, `tests/scenarios/runtime_13_threads.c` |
| `free_s` | `stdlib/safe/src/safe_alloc.c` (`free_checked_s`, `g_recent`, `cplus_safe_ptr_was_freed`) | `tests/crt/unit/test_alloc_metadata_s.c`, `tests/safety/test_scenarios_c.c`, `tests/scenarios/runtime_01_alloc.c` |
| `sprintf_s`, `snprintf_s` | No new behavioral claim; limitation text only | No new behavioral claim; limitation text only |

The tests show: generation bumps on ownership transfer and realloc success,
old generations failing `check_generation`, unowned/stale pointers refused with
`EINVAL` and the original block preserved on injected failure/OOM, double and
invalid frees refused without calling host `free`, and interior/raw pointers
reporting non-live metadata.

## 3. Remaining limited or unverifiable contracts

- **Bounded recent-free caches.** Both allocators keep a 1024-entry
  recently-freed table. `libc/src/stdlib/alloc.c` uses an open-addressed table
  with linear probing and removes an entry when the address is re-issued;
  `stdlib/safe/src/safe_alloc.c` uses a direct-mapped `g_recent` slot. When the
  libc table fills, first-slot overwrite can misclassify a real double free as
  an invalid free; SafeCRT cache collisions and address reuse can likewise
  cause heuristic misclassification. Double/invalid-free *classification* is
  best-effort, not complete.
- **Host allocator bypass.** A host `free()` of a `cplus_malloc` /
  `malloc_s` pointer bypasses the side table; subsequent `cplus_alloc_is_live`
  or `cplus_safe_ptr_is_live` can report a false "live" result and leak
  accounting until the table changes.
- **Cross-allocator use is undefined behavior.** Passing a host pointer to
  `cplus_realloc` / `realloc_s` / `free(_s)`, or a library pointer to host
  `free()` / `realloc()`, is UB at the host level. The checked wrappers refuse
  it with `EINVAL` when the metadata table detects it; this is not a guarantee
  for every address.
- **Concurrent realloc.** The metadata table is mutex-protected, but the host
  `realloc()` call runs with the lock released. The ABA generation guard keeps
  bookkeeping from stripping a newer block at a recycled address; it does not
  make concurrent use of a stale pointer safe.
- **Abstract `_s` string/CRT entries.** `strcpy_s`, `strncpy_s`, `strcat_s`,
  `memcpy_s`, `memmove_s`, `memset_s`, `strnlen_s` and `fopen_s` still carry the
  placeholder `"checked API -- exact signature from Agent 2 capability layer"`
  and `"native/CRT status"`. They were **not** rewritten in this pass and are
  **not** verified here. Do not treat them as precise declarations.
- **Format-string contracts.** `sprintf_s` / `snprintf_s` declare specific
  checks (literal format required, `%n` forbidden, destination bounded). They
  do not prove general format-string validity or that specifiers match the
  supplied arguments.

## 4. Explicitly NOT proven by this contract update

- **Pointer provenance.** The metadata side table checks address/Slot
  membership and generation only. It does not establish C-level provenance,
  interior-object identity, or that an arbitrary pointer legitimately refers to
  a tracked allocation.
- **Arbitrary alias safety.** Generation checks and the bounded recent-free
  table detect common stale-alias patterns; they do not make every aliased or
  recycled address safe to dereference, free, or realloc. A stale alias whose
  address has been re-issued can defeat detection, and passing it to host
  `realloc()` is undefined behavior.
- **Format-string validity.** No contract here proves that a format string is
  valid beyond the explicitly listed checks, nor that its specifiers agree
  with the variadic arguments.
- **Live-game / runtime success.** These are source-and-test-backed contract
  statements for the bundled host build on this platform, not a claim about
  the retail Windows executable or any injected DLL workflow.

## 5. Verification performed

```
python3 -m json.tool libc/contracts/libc_contracts.json >/dev/null
python3 tests/libc/validate_contracts.py
```

Result: JSON parses cleanly; contract validation reports
`contract validation: ok (5 files validated, 202 C17 APIs)`.
