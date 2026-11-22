# C runtime bug-class matrix (honest mitigation status)

This matrix maps well-known C runtime bug classes to what the C+ hardened libc
actually does about them. It is written to **prevent false safety claims**: a
wrapper over a raw C pointer cannot discover that pointer's provenance or
capacity. Where a class cannot be fixed by a library, it says so.

Evidence for each class is produced by the adversarial suites:
`tests/libc/run_attack_tests.sh` (targeted break attempts) and
`tests/libc/run_corpus_tests.sh` (600–1000+ generated scenarios per class).

Legend:
- **MITIGATED** — a defined interface rejects or neutralises the class; tested.
- **PARTIAL** — mitigated only for a specific subset (e.g. only through a `_s`
  API, or only when object size is known at compile time).
- **NOT FIXABLE BY LIBC** — requires compiler/annotation/provenance support or
  is caller responsibility. Documented, never claimed safe.

| # | Bug class (CWE) | Status | What this library does | Residual limitation |
|---:|---|---|---|---|
| 1 | Stack buffer overflow (CWE-121) | PARTIAL | Checked `_s` APIs validate `dmax`; `__builtin_object_size` auto-bounds wrappers reject known oversized writes; canary tests. | Raw `cplus_memcpy(dst,src,n)` with caller-supplied wrong `n` is still UB. Compiler/object-size data required. |
| 2 | Heap buffer overflow (CWE-122) | PARTIAL | Checked allocation metadata (`cplus_alloc_size`, `cplus_alloc_len`) lets callers pass a verified capacity; `_s` APIs check it. | Raw pointers carry no capacity; a wrong `n` passed to a raw API cannot be detected at runtime. |
| 3 | Off-by-one / off-by-N (CWE-193) | PARTIAL | Boundary suites test `n == size`, `n == size+1`, zero and max; checked APIs return defined errors. | Raw APIs preserve ISO C semantics, including UB for `n > capacity`. |
| 4 | NULL pointer dereference (CWE-476) | PARTIAL | Guarded entry points set `errno = EINVAL` and return a defined failure; null corpus enumerates every guarded API. | Unguarded raw ISO APIs (e.g. `strlen(NULL)`) intentionally keep standard UB. A library cannot redefine standard semantics. |
| 5 | Use-after-free (CWE-416) | PARTIAL | Live-allocation table + generation counters let `cplus_alloc_is_live` / `cplus_alloc_check_generation` detect stale pointers; `cplus_free_checked` is fail-stop. | Once memory is returned to the host allocator, dereferencing a stale raw pointer is UB and cannot be trapped portably. |
| 6 | Double free (CWE-415) | MITIGATED | `cplus_free_checked` and double-free counters detect repeated release through the checked API. | Raw `cplus_free(p)` twice is UB; detection requires using the checked API. |
| 7 | Invalid/unaligned free (CWE-590, CWE-763) | PARTIAL | `invalid_free_count` and provenance queries reject foreign/interior pointers through checked APIs. | Cross-allocator frees are UB; the three allocators (cplus, host, SafeCRT) are deliberately isolated. |
| 8 | Memory leak (CWE-401) | PARTIAL | Live-count/live-bytes telemetry exposes leaks; resource corpus verifies counts return to baseline. | Detection requires the caller to query; no automatic reclamation (no GC, no RAII in raw C). |
| 9 | Allocation integer overflow (CWE-190) | MITIGATED | `calloc`/`realloc` reject wrapping `n*size`; `aligned_alloc` validates power-of-two alignment and size multiple; thousands of generated shapes. | — |
| 10 | Integer underflow / wrap in conversions | MITIGATED | Conversion wrappers delegate to host `strto*`/`wcsto*` with `ERANGE` saturation; corpus covers base 1/37/INT_MAX and 40-digit overflow. | Host-specific errno details are delegated, not reimplemented. |
| 11 | Signed overflow (CWE-191) | NOT FIXABLE BY LIBC | — | Language-level UB. Agent 1's compiler contracts are the correct layer; report via `docs/libc/KNOWN_COMPILER_GAPS.md`. |
| 12 | Format string (CWE-134, `%n`) | PARTIAL | Checked format APIs validate/reject hostile formats; compile-time `format(printf/scanf, m, n)` attributes on raw wrappers; corpus probes `%n`, huge width/precision. | Raw `printf`-family functions with untrusted formats remain unsafe. Apple Clang has no `format(wprintf,...)` support, so wide formats have no static checking. |
| 13 | Variadic argument mismatch (CWE-685) | PARTIAL | Compiler format attributes fire at compile time; compile-probe evidence collected. | At runtime, varargs carry no type metadata; a mismatched raw call is UB. |
| 14 | Uninitialized / indeterminate state (CWE-457) | PARTIAL | Corpus uses explicitly hostile-but-initialized `mbstate_t`, `struct tm`, `fenv_t` and metadata objects; wrappers define behaviour for them. | Reading truly uninitialized memory is UB in the caller; no library can detect it. |
| 15 | Dangling pointer / pointer escape | PARTIAL | Generation checks and ownership-transfer APIs detect stale handles when used. | Escaped raw pointers cannot be revoked. |
| 16 | TOCTOU / insecure temp file (CWE-367) | PARTIAL | `tmpfile()`/`mkstemp()` are provided; `tmpnam`/`tmpnam_s` are deliberately UNSUPPORTED. | Caller must choose the safe API; the library cannot stop use of the insecure one. |
| 17 | Data race on shared state (CWE-362) | PARTIAL | Allocation table, logging flag and telemetry use atomics/mutexes; corpus uses coordinated threads. | Shared `FILE*`, `errno` and `mbstate_t` remain callers' synchronisation responsibility. |
| 18 | Deadlock / recursive logging | PARTIAL | Logger is atomic-flag + direct `fprintf`, no allocation, no internal locking; reentrancy corpus probes signal-handler use. | async-signal-safety of `fprintf` is not guaranteed by C; documented, not claimed. |
| 19 | Divide by zero (CWE-369) | NOT FIXABLE BY LIBC | — | Language-level UB for integers; host FP semantics for floats. Compiler layer (Agent 1). |
| 20 | Invalid shift / shift overflow (CWE-129) | NOT FIXABLE BY LIBC | — | Language-level UB. Compiler layer. |
| 21 | Array out-of-bounds (CWE-129) | PARTIAL | Bounded-copy APIs and canary tests; `countof`-style contracts live in Agent 1's compiler. | Raw pointer arithmetic cannot be bounds-checked by a library. |
| 22 | Strict aliasing / type confusion (CWE-843) | NOT FIXABLE BY LIBC | — | Language rule; compiler must enforce. |
| 23 | Unterminated string (CWE-170) | PARTIAL | Bounded functions (`strnlen`, `strncpy`, `strnlen_s`) accept explicit length; canary tests assert no over-read. | Passing an unterminated buffer to `strlen`/`strcpy` is UB. |
| 24 | Overlapping copy (CWE-124) | PARTIAL | `memmove` is correct for overlap (900-case corpus); checked `memcpy_s` rejects overlap. | Raw `memcpy` with overlap is UB by ISO C. |
| 25 | FP domain/range errors (CWE-369/681) | MITIGATED | All math wrappers preserve host classification, errno and FP flags; 1000-case differential corpus. | Host libm is the source of truth; no independent numeric algorithm is claimed. |
| 26 | Resource/fd leak (CWE-772) | PARTIAL | File corpus opens/closes 200 `tmpfile()` handles and checks the fd count returns to baseline. | Caller must close; no automatic cleanup in raw C. |
| 27 | Double close / stale `FILE*` (CWE-1341) | PARTIAL | `cplus_fclose_s` keeps a live/closed stream cache and rejects the second close. | Raw `fclose` twice is UB. |
| 28 | Realloc-failure misuse (CWE-703) | MITIGATED | Failure injection proves the original pointer and contents are preserved on failure; generation increments only on success. | Caller must still check the return value. |
| 29 | Alignment violation (CWE-124) | MITIGATED | `aligned_alloc` validates alignment/size constraints; corpus covers all powers of two and invalid values. | Windows `_aligned_malloc` requires `_aligned_free`; documented. |
| 30 | Buffer over-read via unbounded length | PARTIAL | `_s` APIs enforce `dmax`/`smax`; corpus uses `SIZE_MAX` on small buffers. | Raw APIs trust the caller's `n`. |
| 31 | Signal-handler reentrancy (CWE-479) | PARTIAL | Logger avoids allocation and locking; probes installed a signal handler. | `fprintf` is not async-signal-safe; documented as a limitation. |
| 32 | `longjmp` bypassing cleanup (CWE-672) | NOT FIXABLE BY LIBC | Recoverable runtime hooks exist in `stdlib/safe/src/safe_runtime.c`. | RAII/lifetime enforcement belongs to Agent 1's compiler; UBSan nested-report caveat documented in `RUNTIME_RECOVERY.md`. |
| 33 | Macro double evaluation (CWE-1023) | PARTIAL | Macros in `safe_memory.h` use guarded helper names; known gap fixtures in `tests/known_gaps/`. | C preprocessor cannot guarantee single evaluation without compiler help. |
| 34 | Flexible-array / struct copy | NOT FIXABLE BY LIBC | Known-gap fixture in `tests/known_gaps/flexible_array_copy.c`. | Compiler/type system responsibility. |
| 35 | Ownership ambiguity (CWE-666) | PARTIAL | `cplus_transfer_ownership`, metadata and generation APIs make ownership explicit when used. | Raw C has no ownership type; a second borrow checker is explicitly out of scope (issue #22). |

## Bottom line

The hardened libc mitigates a large, tested subset of C runtime bug classes,
mostly through checked APIs, metadata and compiler-readable contracts. It does
**not** and cannot make arbitrary raw C memory-safe. Classes marked
**NOT FIXABLE BY LIBC** require Agent 1's compiler contracts, and classes marked
**PARTIAL** require the caller to use the checked interface and supply correct
capacities. Passing the adversarial corpora is evidence of behaviour on the
tested host — not proof of safety.
