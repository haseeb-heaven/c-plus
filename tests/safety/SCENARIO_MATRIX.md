# Memory Safety Scenario Matrix & 204-Bug Traceability

This matrix documents the 45 safety scenarios verified across C (`test_scenarios_c.c`), C+ (`test_scenarios_cp.cp`), concurrency (`test_scenarios_threads.c`), and SafeCRT/Hardened Libc unit tests, plus explicit mapping of all 204 catalogue bugs (`test_bugs_catalogue_204.c`).

## 1. 45 Safety Scenarios

| ID | Category | Scenario | Verification | Guarantee / Honest Limit |
|----|----------|----------|--------------|--------------------------|
| S01 | Alloc | Zero-size allocation (`malloc_s(0)`, `calloc_s(0,0)`) | `test_scenarios_c.c`, `test_alloc50_size0_defined.c` | Returns defined 1-byte live tracked block |
| S02 | Alloc | Multiplication overflow (`calloc_s(count, size)`) | `test_scenarios_c.c`, `test_calloc_s.c` | Rejected pre-alloc (`NULL`, `errno=EOVERFLOW`) |
| S03 | Alloc | Huge size above `RSIZE_MAX_MEM` | `test_scenarios_c.c`, `test_alloc50_huge_enomem.c` | Rejected (`NULL`, `errno=EINVAL`) |
| S04 | Alloc | Alignment validation (`aligned_alloc_s`) | `test_scenarios_c.c`, `test_alloc_metadata_s.c` | Power-of-two >= `sizeof(void*)` required |
| S05 | Alloc | Injected / real `realloc_s` OOM preservation | `test_scenarios_c.c`, `test_alloc_metadata_s.c` | Original pointer + contents remain live and intact |
| S06 | Free | Double-free detection (`free_checked_s`, `free_s`) | `test_scenarios_c.c`, `test_alloc_metadata_s.c` | Refused without calling `free()`; stats incremented |
| S07 | Free | Invalid free of stack/static/literal address | `test_scenarios_c.c`, `test_bugs_catalogue_204.c` | Refused; `CPLUS_SAFE_EINVAL`, `errno=EINVAL` |
| S08 | Free | Invalid free of interior pointer (`p + 1`) | `test_scenarios_c.c`, `test_bugs_catalogue_204.c` | Refused; not in live table |
| S09 | Free | Mismatched allocator (raw `malloc` passed to `free_checked_s`) | `test_scenarios_c.c`, `test_alloc_metadata_s.c` | Refused (`CPLUS_SAFE_EINVAL`); no cross-allocator heap corruption |
| S10 | Ownership | Explicit ownership transfer & ABA generation bump | `test_scenarios_c.c`, `test_alloc_metadata_s.c` | Source nulled; old generation fails `check_generation` |
| S11 | Libc Meta | `cplus_malloc`/`cplus_calloc`/`cplus_alloc_get_meta` | `test_scenarios_c.c`, `test_alloc_metadata.c` | Tracks `byte_size`, `elem_size`, `elem_count`, `generation` |
| S12 | Libc Meta | `cplus_alloc_query_length` & `cplus_alloc_len` | `test_scenarios_c.c`, `test_alloc_metadata.c` | Safe element count query; rejects interior/freed ptr |
| S13 | Libc Meta | `cplus_realloc` move & OOM preservation | `test_alloc_metadata.c` | Old address evicted; OOM preserves live block |
| S14 | Libc Free | `cplus_free_checked` double/invalid free counters | `test_alloc_metadata.c` | Refuses double/invalid free; counters exact |
| S15 | Libc Track | Optional `CPLUS_MALLOC_TRACKING` compatibility | `libc/install-include/cplus_libc.h` | Opt-in macro redirection to `cplus_*` tracked allocators |
| S16 | Memory | `cplus_memcpy_s` destination overflow (`ESNOSPC`) | `test_scenarios_c.c`, `test_bugclass_heap_overflow.c` | Rejected with zeroed bound constraint |
| S17 | Memory | `cplus_memcpy_s` overlap rejection (`ESOVRLP`) | `test_scenarios_c.c`, `test_bugclass_heap_overflow.c` | Overlap rejected; caller directed to `cplus_memmove_s` |
| S18 | Memory | `cplus_memzero_s` secret clearing | `test_bugclass_uninit.c` | Explicit zeroing + NULL/zero guard |
| S19 | String | `cplus_strcpy_s` / `cplus_strncat_s` NUL & bounds | `test_scenarios_c.c`, `test_all_functions_10_string_alloc.c` | Rejected cleanly when destination insufficient |
| S20 | Format | `cplus_snprintf_s` / `cplus_sprintf_s` bounded NUL | `test_scenarios_c.c`, `test_safe_format.c` | Never writes past `dmax`; always NUL-terminated |
| S21 | String | `cplus_strtok_s` delimiter & end-of-tokens state | `test_scenarios_c.c`, `test_strtok_s.c` | Returns `NULL` without advancing past `dmax` |
| S22 | File I/O | `cplus_fopen_s` / `cplus_fclose_s` & `bmax` clamping | `test_scenarios_c.c`, `test_io50_open_contract.c` | `fclose_s` rejects `NULL` and double-close via stream cache |
| S23 | Runtime | Cleanup stack `mark` / `unwind_to` manual scope | `test_scenarios_c.c`, `test_runtime_cleanup.c` | LIFO destructor execution verified |
| S24 | Runtime | Guarded `longjmp` recovery with LIFO cleanup stack | `test_scenarios_c.c`, `test_runtime_cleanup.c` | Honest limit: single UBSan recovery per thread on Apple runtime |
| S25 | Runtime | Unguarded fail-stop (`exit 70`, no SIGABRT) | `tests/runtime/demo_unguarded.c` | Terminates cleanly instead of continuing through UB |
| S26 | C+ RAII | Normal & nested scope automatic destruction | `test_scenarios_cp.cp`, `tests/stdlib/adversarial.cp` | Destructor runs on scope exit |
| S27 | C+ RAII | Early `return` cleanup | `test_scenarios_cp.cp` | Vector destroyed on early return |
| S28 | C+ RAII | Loop `break` / `continue` cleanup | `test_scenarios_cp.cp` | Per-iteration RAII handles released |
| S29 | C+ Slice | Slice staleness on vector mutation/growth/clear | `test_scenarios_cp.cp`, `containers_safety_test.cp` | `stale() == 1`, `valid() == 0`, reads rejected |
| S30 | C+ String | UTF-8 validation (overlong, surrogate, >U+10FFFF, trunc) | `test_scenarios_cp.cp`, `containers_safety_test.cp` | Invalid sequences return `0` |
| S31 | C+ String | Overlapping self-append with reallocation | `tests/stdlib/safety_regressions.cp` | Source copied before old buffer freed |
| S32 | C+ OOM | `intmap::grow` & `strmap::grow` 3-array rollback | `test_scenarios_cp.cp`, `containers_safety_test.cp` | Failed growth frees partials and preserves map |
| S33 | C+ OOM | `vector::reserve` & `string::reserve` failure atomicity | `containers_safety_test.cp` | Prior buffer, size, and capacity untouched |
| S34 | C+ Containers | `intset`, `deque`, `list`, `stack`, `queue` bounds & drop | `test_scenarios_cp.cp`, `containers_safety_test.cp` | Double-drop idempotent; empty pop returns error |
| S35 | C+ File | `file::close` double-close refusal & `file_temp` cleanup | `test_scenarios_cp.cp`, `containers_safety_test.cp` | Closed handle cannot be closed twice |
| S36 | C+ FS | `fs_mkdtemp` NUL & template validation | `test_scenarios_cp.cp`, `containers_safety_test.cp` | Rejects missing `XXXXXX` or small buffer |
| S37 | C+ Error | Unified error categories + opt-in atomic telemetry | `containers_safety_test.cp` | Telemetry OFF by default; callbacks non-recursive |
| S38 | C+ Sync | `guard::unlock()` + destructor double-unlock prevention | `test_scenarios_cp.cp`, `containers_safety_test.cp` | `owner = NULL` prevents unlock twice |
| S39 | C+ Sync | `condvar` wait/signal/broadcast & `thread` auto-join | `containers_safety_test.cp` | Joinable thread joined in `drop()` |
| S40 | CTL | `ctl/vector.h` OOM preserve + empty `pop_back` guard | `tests/ctl/run_ctl_tests.sh` | No null/underflow on empty pop or failed grow |
| S41 | Threads | Concurrent `malloc_s`/`free_checked_s` table integrity | `test_scenarios_threads.c` | Mutex-protected live/recent tables, 0 leaks |
| S42 | Threads | Concurrent `cplus_malloc`/`cplus_calloc`/`cplus_free` | `test_scenarios_threads.c` | Mutex-protected metadata table, 0 leaks |
| S43 | Threads | Race-free `safe_stats` atomic counters & registration lock | `test_scenarios_threads.c` | Exact sum `ok + fail == calls` across threads |
| S44 | Threads | `pthread_mutex`/`pthread_cond` producer-consumer lifecycle | `test_scenarios_threads.c` | Clean init, signal, wait, destroy |
| S45 | Threads | Cross-thread explicit ownership transfer | `test_scenarios_threads.c` | Producer gen invalidated; consumer owns + frees |

## 2. 204-Bug Catalogue Traceability (`scratch/bugs1.cp`, `scratch/bugs2.cp`)

Every bug from `bug01` to `bug204` is mapped to an explicit `test_bug001()` .. `test_bug204()` case in `tests/crt/unit/test_bugs_catalogue_204.c` plus corresponding SafeCRT/Stdlib adversarial tests:

- **Bugs 001–042 (Memory, UAF, Double Free, Bounds, Overflow)**: Tested via `malloc_s`, `calloc_s`, `realloc_s`, `free_checked_s`, `cplus_safe_transfer_ownership`, `cplus_memcpy_s`, `cplus_strcpy_s`, `cplus_strncpy_s`, `cplus_strnlen_s`, `cplus_memzero_s`, and `cplus_runtime_push_cleanup`/`unwind`. Static stack-escape/alias patterns (007–009, 043–045) are marked `[COMPILER-GAP]` and exercised via heap-safe equivalents.
- **Bugs 043–104 (File I/O, Strings, Threads, Signals, Varargs, Allocator Contracts)**: Tested via `cplus_fopen_s`, `cplus_fclose_s`, `cplus_strtok_s`, `cplus_snprintf_s`, `aligned_alloc_s`, `cplus_safe_ptr_check_generation`, and cleanup unwinders; compiler AST/bounds patterns (060, 062–064, 067–068) and OS/mmap/dlopen boundaries (077–079, 102) are annotated honestly.
- **Bugs 105–204 (Second Catalogue: Overflow, Posix/System, Bitfields, Threads, Stdio, VLAs, Setjmp, Realloc/Free Edges)**: Tested via `test_bug105`..`test_bug204` covering `calloc_s` overflow, `aligned_alloc_s`, `cplus_sprintf_s`, sub-object bounds, `cplus_memmove_s`, `cplus_strncat_s` loop bounds, zero-length `memcpy_s`, `realloc_s(p, 0)`, mismatched allocator rejection (`free_checked_s` on raw `malloc` and stack pointers), `cplus_safe_len`, and `errno` preservation around cleanup (`bug192`).
