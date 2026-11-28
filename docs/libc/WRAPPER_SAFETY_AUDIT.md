# Systematic safety audit of implemented libc/stdlib wrappers

Date: 2026-11-23. Scope: Agent 2 owned headers/sources only. No compiler or
runtime changes. Source evidence: `docs/libc/AUDIT_{alloc,memory_string,stdio_format,numeric_wide,lifecycle}.md`.

## 1. Complete inventory of implemented headers and functions

| Header | Distinct callables | Notes |
|---|---:|---|
| `libc/install-include/cplus_libc.h` | 560 | normal C surface + platform/POSIX wrappers |
| `libc/install-include/cplus_libc_safe.h` | 17 | ordinary-name macro layer (plain libc) |
| `libc/install-include/cplus_uchar.h` | 4 | C11/C23 character types + conversions |
| `libc/install-include/cplus_threads.h` | 25 | C11 threads adapter surface |
| `stdlib/safe/safe_c.h` | 17 | ordinary-name macro layer (SafeCRT) |
| `stdlib/safe/safe_alloc.h` | 22 | checked allocator extensions |
| `stdlib/safe/safe_memory.h` | 16 | checked memory adapters |
| `stdlib/safe/safe_string.h` | 16 | checked string adapters |
| `stdlib/safe/safe_io.h` | 4 | checked stream adapters |
| `stdlib/safe/safe_format.h` | 23 | checked format adapters |
| `stdlib/safe/safe_ext_mem.h` | 2 | vendored safeclib timingsafe |
| **Total (deduplicated)** | **672** | across 11 public headers |

Backends: bundled/musl-derived 29, copied upstream 8, host-delegated 476,
toolchain-provided 359; 27 APIs MISSING, 49 UNSUPPORTED (see `iso_c_inventory.json`).

## 2. Safety audit findings (aggregate)

| Audit | Findings | High | Medium | Low/Other |
|---|---:|---:|---:|---:|
| Allocation (`AUDIT_alloc.md`) | 20 | 3 | 9 | 8 |
| Memory/string/ctype (`AUDIT_memory_string.md`) | 50 | 6 | 27 | 17 |
| Stdio/format (`AUDIT_stdio_format.md`) | 15 | 3 | 7 | 5 |
| Math/time/wide (`AUDIT_numeric_wide.md`) | 21 | 1 | 14 | 6 |
| Lifecycle/concurrency/logging (`AUDIT_lifecycle.md`) | 21 | 3 | 11 | 7 |
| **Total** | **127** | **16** | **68** | **43** |

Cross-cutting root causes (not isolated bugs):
1. Unknown pointer capacity is treated as unchecked in default mode by design;
   constrained provenance is only available for tracked allocations and known
   object sizes. This is documented, not claimed safe.
2. Bounded/heuristic caches (recent-free, closed-FILE rings) can produce false
   positives and evictions; most are fixed in one layer but not the other.
3. Thin host delegates inherit host semantics (errno, locale, FP flags, static
   buffers, non-reentrancy). They are not independent implementations.
4. Compiler-side guarantees (bounds metadata, RAII around longjmp, alloc_size,
   once-flag ABI) are Agent 1 work; this branch only documents them.

## 3. Prioritized list of missing or unsafe functionality

1. **HIGH** `cplus_thrd_create` detached-thread context uses caller stack
   (`AUDIT_lifecycle.md` F1). Fix: heap-allocate + free in trampoline.
2. **HIGH** uchar surrogate state uses a 4-byte `unsigned` where host
   `mbstate_t` is 128 bytes (`AUDIT_lifecycle.md` F2). Fix: real `static mbstate_t`
   plus separate surrogate word; minimal documented adaptation of musl code.
3. **HIGH** `cplus_fclose_s` deletion without tombstones breaks probe chains
   and can reach host `fclose()` on freed `FILE*` (`AUDIT_stdio_format.md` #1).
4. **HIGH** plain `cplus_fclose` false rejection/fd leak and cross-registry
   bypass (`AUDIT_stdio_format.md` #2), plus bounded cache wrap (#3).
5. **HIGH** `cplus_isblank` uncast int (FIXED in this pass), huge-`n` in
   `strncpy`/`_bounded`, `memrchr`/`stpncpy` huge `n` and missing NULL guards
   (`AUDIT_memory_string.md` #2/#3).
6. **HIGH** wide-format `%n` pass-through with no honoured static checking
   (`AUDIT_numeric_wide.md` F01).
7. **MED** recovery `longjmp` skips RAII, racy config globals, missing recursion
   latch (`AUDIT_lifecycle.md` F3+F8+F9+F10); signal/fenv NULL guards (F4+F5);
   logger errno clobber and short writes (F6+F7).
8. **MED** checked-format literal gate only in `_checked` macros; direct `cplus_*_s`
   and `v*` bypass it (`AUDIT_stdio_format.md` #4).
9. **MED** 27 missing C23 APIs with no dependency-closed upstream source.
10. **MED** Agent 1 compiler contracts: alloc_size, bounds metadata, ownership
    enforcement (hand-off files exist; no compiler edits here).

## 4. Concrete implementation plan (work order)

1. Fix HIGH items 1–7 in small commits with regression tests (in progress).
2. Unify close tracking with tombstones/generation in both stdio layers; add
   cross-registry handoff tests.
3. Add bounded `known_src` capacity and max-size clamps to memory wrappers;
   never rely on caller-declared `dmax` alone where BOS is available.
4. Make all guarded entry points consistently reject NULL with `EINVAL`; test
   table-driven across signal/fenv/time/wchar.
5. Save/restore `errno` in the logger and retry partial writes; add signal-safe
   status notes; update stale adversarial logging expectations.
6. For the 27 missing APIs, require a newer toolchain or an approved C++ upstream
   closure; do not fabricate constants/algorithms.
7. Coordinate Agent 1 items via `docs/libc/DISPATCH_BOUNDS_HANDOFF.md`,
   `COMPILER_CONTRACT_ALIGNMENT.md`, `KNOWN_COMPILER_GAPS.md`.
8. Re-run `tests/run_all_evidence.sh` after every fix group; report pass/fail/skip.

## 5. Verification policy

- Test DEFAULT and STRICT modes with sanitizers disabled; sanitizers are
  additional evidence, never the sole safety mechanism.
- Fatal cases run in subprocesses with timeouts; exact exit codes recorded.
- Allocation failures use `stdlib_alloc_fail_at/after/reset` — never memory
  exhaustion.
- Contracts and limitations updated in docs with each fix; no 100% claim.
