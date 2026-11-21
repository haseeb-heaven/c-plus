# Adversarial bug catalogue -> mitigation matrix

Maps the 104 raw-C bug patterns (`tests/crt/adversarial/bug_catalogue.c`) to how
the C+ hardened layer handles them. This is an **honest** matrix: it separates
runtime-checked APIs (libc+/safe CRT can catch) from compile-time/static issues
(Agent 1 compiler) and explicit unsafe boundaries (no libc can catch).

Legend:
- **SAFE-CATCH** — a checked C+ API returns a defined error / avoids the UB when the caller uses it (proved by `tests/crt/adversarial/test_safe_mitigations.c`).
- **COMPILER** — requires static analysis / compile-time enforcement (ownership, lifetime, provenance, bounds on raw arrays). Agent 1 territory; not a libc guarantee.
- **RUNTIME (opt)** — caught only with runtime instrumentation (sanitizer) or checked API; raw form is UB.
- **BOUNDARY** — explicit unsafe boundary: cannot be detected by any library without allocator/compiler metadata; must be documented and avoided.

| # | Bug pattern | Class | Mitigation / evidence |
|---|---|---|---|
| 1 | basic leak (missing free) | COMPILER | RAII/ownership (Agent 1); `free_s(&p)` nulls handle |
| 2 | null deref unchecked malloc | SAFE-CATCH | `malloc_s` returns status; caller must check |
| 3 | null struct member | COMPILER | nullability analysis (Agent 1) |
| 4 | null to memcpy | SAFE-CATCH | `cplus_memcpy_s` -> ESNULLP |
| 5 | UAF read | BOUNDARY | needs allocation/compiler metadata |
| 6 | UAF write | BOUNDARY | needs allocation/compiler metadata |
| 7 | return local addr | COMPILER | lifetime analysis (Agent 1) |
| 8 | block-scope dangling | COMPILER | lifetime analysis (Agent 1) |
| 9 | compound-literal dangling | COMPILER | lifetime analysis (Agent 1) |
| 10 | realloc moved old ptr | COMPILER | `realloc_s` preserves on failure; alias invalidation is compiler-side |
| 11 | aliased dangling | BOUNDARY | alias/provenance tracking |
| 12 | double free | BOUNDARY | allocator metadata; `free_s` nulls single handle only |
| 13 | early-return leak | COMPILER | RAII (Agent 1) |
| 14 | partial-init leak | COMPILER | RAII (Agent 1) |
| 15 | file leak on early return | COMPILER | RAII; `cplus_fclose_s` used with cleanup discipline |
| 16 | mutex deadlock | COMPILER | RAII locks (Agent 1) |
| 17 | longjmp skips cleanup | COMPILER | known baseline `classic_bug34_longjmp` |
| 18 | nested struct leak | COMPILER | ownership (Agent 1) |
| 19 | shallow copy double free | BOUNDARY | `free_s` nulls handle to reduce risk |
| 20 | wild pointer | COMPILER | init analysis (Agent 1) |
| 21 | uninit struct ptr | COMPILER | init analysis (Agent 1) |
| 22 | stack overflow | COMPILER/RUNTIME | static bounds; `cplus_memcpy_s` bounded |
| 23 | heap overflow | SAFE-CATCH | bounded `memcpy_s`/`memset_s` reject |
| 24 | stack underflow | COMPILER | static bounds (Agent 1) |
| 25 | heap underflow | BOUNDARY | allocator metadata |
| 26 | off-by-one | COMPILER | static bounds; bounded API rejects |
| 27 | free stack mem | BOUNDARY | allocator/compiler metadata |
| 28 | free literal | BOUNDARY | allocator metadata |
| 29 | modify literal | COMPILER | const/rodata (Agent 1) |
| 30 | free offset ptr | BOUNDARY | allocator metadata |
| 31 | pointer overwrite leak | COMPILER | ownership (Agent 1) |
| 32 | realloc null leak | SAFE-CATCH | `realloc_s` preserves old on failure |
| 33 | uninit heap read | SAFE-CATCH (opt) | `calloc_s`/`memzero_s` deterministic |
| 34 | uninit stack read | COMPILER | init analysis (Agent 1) |
| 35 | sizeof(pointer) | COMPILER | static analysis (Agent 1) |
| 36 | missing sizeof | SAFE-CATCH | bounded `memcpy_s`/`memset_s` reject on overflow |
| 37 | missing NUL terminator | SAFE-CATCH | `strcpy_s`/`strncpy_s` reject overflow |
| 38 | non-terminated strlen | SAFE-CATCH | `strnlen_s`/`memcmp_s` bounded |
| 39 | unbounded strcpy | SAFE-CATCH | `strcpy_s` -> ESNOSPC |
| 40 | overlap memcpy | SAFE-CATCH | `memcpy_s` -> ESOVRLP; use `memmove_s` |
| 41 | int overflow malloc | SAFE-CATCH | `calloc_s` -> EOVERFLOW |
| 42 | signed->unsigned bypass | SAFE-CATCH | checked sizes reject huge `rsize_t` |
| 43 | ptr arith past end | COMPILER | provenance (Agent 1) |
| 44 | deref end ptr | COMPILER | bounds (Agent 1) |
| 45 | strict aliasing | COMPILER | UB model (Agent 1) |
| 46 | unaligned access | COMPILER | alignment (Agent 1) |
| 47 | fclose(NULL) | SAFE-CATCH | `cplus_fclose_s(NULL)` -> EINVAL |
| 48 | double fclose | SAFE-CATCH | `cplus_fclose_s` 2nd -> EINVAL |
| 49 | 2D free order | BOUNDARY | ownership (Agent 1) |
| 50 | FAM missing payload | SAFE-CATCH (opt) | bounded APIs reject; alloc size is caller's |
| 51 | malloc(0) deref | SAFE-CATCH | `malloc_s(&p,0)` defined non-deref contract |
| 52 | setbuf dangling | BOUNDARY | stream buffer lifetime (Agent 1) |
| 53 | list traversal UAF | COMPILER | ownership (Agent 1) |
| 54 | dlist dangling | COMPILER | ownership (Agent 1) |
| 55 | realloc interior ptr | BOUNDARY | provenance (Agent 1) |
| 56 | thread detach stack ptr | COMPILER | lifetime/threads (Agent 1) |
| 57 | pthread_cancel leak | COMPILER | cleanup handlers (Agent 1) |
| 58 | double-checked lock | COMPILER | atomics (Agent 1) |
| 59 | TOCTOU race | COMPILER | concurrency (Agent 1) |
| 60 | signal malloc/free | BOUNDARY | async-signal-safety |
| 61 | static buffer alias | BOUNDARY | lifetime (Agent 1) |
| 62 | ptr sub distinct | COMPILER | provenance (Agent 1) |
| 63 | ptr cmp distinct | COMPILER | provenance (Agent 1) |
| 64 | provenance violation | BOUNDARY | CHERI/provenance model |
| 65 | struct padding leak | COMPILER | init/`memzero_s` for scrubbing |
| 66 | memcmp padding | SAFE-CATCH (opt) | compare fields, not padding |
| 67 | switch-scope dangling | COMPILER | lifetime (Agent 1) |
| 68 | goto skips init | COMPILER | init analysis (Agent 1) |
| 69 | alloca in loop | COMPILER | stack analysis (Agent 1) |
| 70 | infinite recursion | COMPILER/RUNTIME | stack limits |
| 71 | va_arg mismatch | COMPILER | type checking (Agent 1) |
| 72 | missing sentinel | COMPILER | API contract (Agent 1) |
| 73 | arena alignment | COMPILER | alignment (Agent 1) |
| 74 | refcount underflow | COMPILER | ownership (Agent 1) |
| 75 | cyclic ref leak | COMPILER | ownership (Agent 1) |
| 76 | mmap NULL check | COMPILER | API-usage analysis (Agent 1) |
| 77 | munmap partial | BOUNDARY | OS API contract |
| 78 | mmap SIGBUS | BOUNDARY | OS API contract |
| 79 | dlclose dangling fn | BOUNDARY | dynamic linker contract |
| 80 | strtok literal | SAFE-CATCH | `cplus_strtok_s` on writable buffer; literal is const |
| 81 | snprintf truncation OOB | SAFE-CATCH | bounded format; returns needed length |
| 82 | fread missing NUL | SAFE-CATCH | check `fread_s` count, NUL-terminate by hand |
| 83 | qsort wrong indirection | COMPILER | type checking (Agent 1) |
| 84 | out-param overflow | COMPILER | type/ABI (Agent 1) |
| 85 | restrict violation | COMPILER | aliasing (Agent 1) |
| 86 | FAM by value | COMPILER | language semantics (Agent 1) |
| 87 | container_of NULL | COMPILER | pointer math (Agent 1) |
| 88 | double-ptr ownership | COMPILER | ownership (Agent 1) |
| 89 | out-param leak | COMPILER | ownership (Agent 1) |
| 90 | dangling cache | COMPILER | lifetime (Agent 1) |
| 91 | dangling callback | COMPILER | lifetime (Agent 1) |
| 92 | ring buffer off-by-one | COMPILER/RUNTIME | static bounds |
| 93 | tagged ptr deref | BOUNDARY | tagged-pointer model |
| 94 | free tagged ptr | BOUNDARY | allocator metadata |
| 95 | tree recursion overflow | COMPILER/RUNTIME | stack analysis |
| 96 | partial array cleanup | COMPILER | ownership (Agent 1) |
| 97 | pool double return | BOUNDARY | allocator metadata |
| 98 | memmove alias double free | BOUNDARY | ownership (Agent 1) |
| 99 | async IO stack dangling | COMPILER | lifetime (Agent 1) |
| 100 | format string %s mismatch | COMPILER/RUNTIME | typed format (Agent 1); use `snprintf_s` |
| 101 | getenv dangling | BOUNDARY | environment storage contract |
| 102 | putenv stack dangling | BOUNDARY | POSIX contract |
| 103 | cleanup uninit ptr | COMPILER | init analysis (Agent 1) |
| 104 | cleanup transfer double free | COMPILER | ownership (Agent 1) |

## Executed runtime proof

`tests/crt/adversarial/test_safe_mitigations.c` (built against `libcplus_safecrt.a`)
asserts, and passes, the **SAFE-CATCH** rows that are runtime-detectable:

```
ok catch_null_memcpy            (bug02, bug04)
ok catch_dest_overflow          (bug23, bug26, bug36, bug37, bug39)
ok catch_unterminated_read      (bug38)
ok catch_zero_alloc             (bug51)
ok catch_realloc_failure_preserves (bug32)
ok catch_double_free_handle     (bug19/bug98 handle-nulling)
ok catch_stdio_lifecycle        (bug47, bug48)
ok catch_format_truncation      (bug81)
ok catch_overlap                (bug40)
ok catch_size_overflow          (bug41, bug42)
ok catch_no_write_past_bound    (bug23/26 write-bound)
ok catch_deterministic_zero     (bug33)
safe_mitigations: ok
```

## Honest conclusion

- The checked API layer catches the **runtime-detectable** subset (null, bounds,
  overlap, truncation, zero-alloc, realloc-failure, double-close, overflow).
- Most of the 104 patterns are **compile-time/ownership/lifetime/provenance or
  explicit unsafe boundaries** and are **not** caught by a C library alone. They
  require Agent 1 compiler enforcement and/or sanitizers.
- No Rust-grade memory-safety claim is made for raw C.
