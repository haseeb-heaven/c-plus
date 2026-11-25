# Agent 2 Current Audit (`agent/modern-stdlib`)

Baseline branch audited: `agent/modern-stdlib` (`064e0e47171f53dcbfce85940ec0e00deac842ba`, 88 commits ahead and 11 commits behind `origin/develop`).

## 1. Comparison of the 11 Commits Missing from `origin/develop`

Inspection via `git log --stat agent/modern-stdlib..origin/develop` established that all 11 commits (`0e5a8b6`, `2127625`, `4a6b5be`, `73e0ab0`, `784490e`, `d891365`, `07ad9ea`, `08d887e`, `faa1bd2`, `d7ffcbb`, `5eec102`) touch only `README.md` and `docs/ROADMAP.md`. They contain no compiler, runtime, SafeCRT, libc, or stdlib code changes, so rebasing or merging `develop` is unnecessary and would add merge churn without functional benefit.

## 2. Defects Identified & Fixed

### A. SafeCRT (`stdlib/safe/`)
1. **`strtok_s` state & end-of-tokens defects (`third_party/safeclib/src/str/strtok_s.c`, `cplus_sc_wrappers.c`, `safe_string.c`)**:
   - Trailing tokens failed to advance `*ptr = dest`, and empty remaining input (`dest == NULL && *dmaxp == 0 && **ptr == '\0'`) returned an unexpected state instead of `NULL`. Fixed and added 5-arg `cplus_sc_strtok_s` wrapper matching `cplus_strtok_s` call arity; tightened `tests/crt/unit/test_strtok_s.c` and `tests/crt/adversarial/test_str50_strtok.c`.
2. **Standard C signatures + allocation metadata (`stdlib/safe/safe_alloc.h`, `src/safe_alloc.c`, `safe_io.h`, `src/safe_io.c`)**:
   - Converted `malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `aligned_alloc_s`, `cplus_fopen_s`, and `cplus_fclose_s` to standard C parameter lists and return types (`void *` / `FILE *` / `int`) while retaining Annex K `dmax`/`bmax` on bounded copy/string/format/I/O operations.
   - Added thread-safe allocation metadata (`byte_size`, `elem_size`, `elem_count`, `generation`), `cplus_safe_len`, `cplus_safe_query_length`, `cplus_safe_ptr_check_generation`, `cplus_safe_transfer_ownership`, `aligned_alloc_s`, `free_checked_s`, and failure injection (`cplus_safe_alloc_fail_after/reset/seq`).
   - Added thread-safe stream live/closed cache in `safe_io.c` to block double-close in `cplus_fclose_s(FILE *)`.
3. **Telemetry registration race (`stdlib/safe/src/safe_stats.c`)**:
   - Added `pthread_mutex_t g_reg_lock` and release-acquire atomics on `g_count` so concurrent slot registration cannot lose entries or race with readers.
4. **Recoverable runtime `longjmp` vs RAII (`stdlib/safe/safe_runtime.h`, `src/safe_runtime.c`)**:
   - Added nested guard frames, `cplus_runtime_set_failstop`, and a thread-local LIFO cleanup registry (`cplus_runtime_push_cleanup`, `pop_cleanup`, `cleanup_mark`, `cleanup_unwind_to`) so registered cleanups execute on unwind.
   - Honest finding verified in `tests/runtime/test_runtime_cleanup.c`: Apple Clang UBSan holds a thread-local `InReport` flag during `__ubsan_on_report()`, so triggering a second UBSan fault after `longjmp` recovery in the same thread aborts with `UndefinedBehaviorSanitizer: nested bug in the same thread`. Documented for Agent 1 and in `docs/libc/RUNTIME_RECOVERY.md`.
5. **Strict C99 `-Wall -Wextra -pedantic` warnings**:
   - Fixed prototype in `third_party/exceptions4c/exceptions4c.h` (`(void)`) and TLS macro in `stdlib/safe/src/safe_except.c`.

### B. Hardened Libc (`libc/`)
1. **Metadata, double/invalid-free, and ownership (`libc/install-include/cplus_libc.h`, `libc/src/stdlib/alloc.c`)**:
   - Added thread-safe live allocation table (`byte_size`, `elem_size`, `generation`), `g_recent` free cache, `cplus_free_checked`, `cplus_alloc_get_meta/is_live/size/elem_size/length/query_length/check_generation/len`, `cplus_transfer_ownership`, `cplus_alloc_fail_after/reset`, `cplus_alloc_double_free_count/invalid_free_count`, and opt-in `CPLUS_MALLOC_TRACKING`.
   - Updated `cplus_strdup`/`cplus_strndup` (`extra2.c`, `strndup.c`) to allocate via `cplus_malloc`.
   - Declared `cplus_strspn`, `cplus_strcspn`, `cplus_strpbrk` in `cplus_libc.h`.
2. **Pointer/UB cleanup**:
   - Fixed `musl_extra.c` pointer subtraction cast `(size_t)(z - h) < l`, `tests/libc/test_libc_full.c`, and unused warning in `test_stdio_varargs.c`.

### C. Modern C+ Stdlib (`stdlib/*.hp`)
1. **`error.hp`**: Added `OWNERSHIP`, `LIFECYCLE`, `UTF8` categories, `stdlib_category_name`, callback hook (`stdlib_set_error_callback`), OFF-by-default atomic telemetry (`stdlib_telemetry_*`), failure-injectable `stdlib_alloc`/`stdlib_calloc`/`stdlib_release`, and weak metadata side-table (`stdlib_alloc_*`, `stdlib_transfer_ownership`). Renamed internal `stdlib_meta_drop` -> `stdlib_meta_forget` and used a static sentinel address for `STDLIB_META_TOMB` to eliminate C+ `[CPLUS-OWNER]` and `[CPLUS-FFI]` false positives.
2. **`vector.hp`**: Preserved prior allocation on `vector::reserve` OOM; added generation-checked `slice::stale()`; added `insert`, `erase`, `copy_from`, `shrink_to_fit`.
3. **`string.hp`**: Checked ctor/reserve allocation & overflow, fixed `append` `uintptr_t` pointer comparisons and NULL checks on `equals`/`compare`/`append`; added `stdlib_utf8_validate`, `is_valid_utf8`, `valid`, `copy_from`.
4. **`collections.hp`**: Fixed 3-array OOM rollback in `intmap::grow` and `strmap::grow`, key copy OOM in `strmap::put`, power-of-2 sizing; added `intset`, `deque`, `list`, `stack`, `queue`.
5. **`file.hp` & `fs.hp`**: Checked `mode == NULL`, fixed `owns`, added `file::close`, `file_temp`, and `fs_mkdtemp`.
6. **`thread.hp`**: Cleared `this->owner = NULL` in `guard::unlock()` to prevent double unlock on destruction; added RAII `condvar` and `thread` classes.
7. **`third_party/ctl/ctl/vector.h`**: Guarded empty `pop_back`, multiplication overflow in `fit`, and `realloc` failure preservation in `fit`/`push_back`/`emplace_back`.

## 3. Host Platform Limits Observed Honestly

- **macOS arm64**: `-fsanitize=address` aborts at runtime (`sanitizer_malloc_mac.inc:189`) and `-fsanitize=fuzzer` cannot link (`libclang_rt.fuzzer_osx.a` missing). ASan, TSan, and libFuzzer require Linux CI (`.github/workflows/libc-crt.yml`).
- **Compiler hand-offs (`docs/patches/`)**: Owned `.len` syntax for SafeCRT/Libc allocators (`agent1-len-managed-allocators.md`) and `&var` out-parameter `[CPLUS-SAFE-LIFETIME]` false positive live in compiler/runtime (`c-plus-core`); SafeCRT/Libc provide `cplus_safe_len()` / `cplus_alloc_len()` runtime helpers today.
