# PHASE 5-7 — memory / string / allocation defense review (Agent D)

Branch `agent/modern-stdlib`. **Files owned / changed:**

| File | Change |
|---|---|
| `stdlib/safe/safe_memory.c` | ONE minimal ABI-neutral fix: `cplus_memcmp_s_auto` both-NULL fail-open (see §3) |
| `stdlib/safe/safe_memory.h` | not modified |
| `tests/defense/memory2/memory2_harness.c` | new fork-based adversarial harness (16 named attacks + 1200 fixed-seed cases) |
| `tests/defense/memory2/regression_memcmp_null.c` | new minimal regression (FAILS before, PASSES after) |
| `tests/defense/memory2/run.sh` | new build + `timeout 60` runner |
| `tests/defense/memory2/run_before_after.sh` | new HEAD-vs-working-tree evidence runner |
| `tests/defense/memory2/HARDENING2.md` | this report |

No compiler, runtime, build script, libc, third_party, other header/source or
existing test was edited. No `git add` / `git commit` was performed.

---

## 1. Method

`tests/defense/memory2/memory2_harness.c` runs every attack in a **forked
child**. A `MAP_SHARED` word carries the child's classification; the parent
maps `WIFSIGNALED` to **CRASH** (deterministic guard-page faults via
`mmap`+`mprotect(PROT_NONE)`), otherwise uses the child's own verdict. Each
child surrounds heap buffers with 24-byte canary patterns and reports
**CORRUPT** when those bytes change without a fault. A documented error signal
with intact canaries is **REFUSED**; a success where a refusal was promised is
**UNEXPECTED_OK**; a valid-input success is **OK**. `cplus_runtime_set_
violation_trap(0)` is set in children so a library refusal returns normally
and is not confused with a crash. Every named attack uses a fixed seed
(`0x1234`); the fuzz loop runs **1200** additional fixed-seed cases
(seeds `i + 0x5eed`, `i = 0..1199`, deterministic odd/even family split).
The whole harness runs under `timeout 60`.

The harness covers: direct `_s` adapters (dmax > proven capacity, smax > dmax,
valid copy), `_auto` wrappers (tracked source over-read, interior-pointer BOS,
`SIZE_MAX` lengths), `memcmp` (tracked small operand, both/one NULL), UAF
(exact and interior), double free, guard-page destinations/sources, realloc
failure atomicity, allocation overflow, and the deterministic fuzz families.

---

## 2. Areas reviewed and verdicts

| Area | Verdict |
|---|---|
| Direct `_s` adapter clears `dmax` past real capacity | **Already fixed by blue-team 6** — re-verified REFUSED (406/ERANGE), canaries intact |
| `memcpy_s_auto`/`memmove_s_auto` tracked source over-read | **Already fixed by blue-team 6** — re-verified REFUSED |
| errno domain on constraint failures, capacity-mode data race | Already fixed — unchanged |
| `dmax` clamp via `CPLUS_SAFE_OBJ_SIZE` / tracked metadata (incl. interior pointers) | Correct — `interior_bos` REFUSED, canaries intact |
| `smax > dmax` error clear | Bounded by proven capacity — REFUSED, no out-of-object write |
| `calloc_s` count×size overflow / `realloc_s` injected-failure atomicity | Correct — REFUSED, owner/size/payload preserved |
| Double free / exact-pointer UAF | Correct — REFUSED (EFAULT / EINVAL) |
| **`cplus_memcmp_s_auto(NULL, NULL, n>0)`** | **GENUINE GAP — fixed (§3)** |
| Interior-pointer UAF | Gap exists but **not fixable in owned files** — escalated (§5) |
| Recent-free-cache exhaustion / very old freed address | Bounded heuristic — escalated (§5) |
| Untracked stack destination with lying `dmax` (direct API) | Honest documented limit (§5) |
| Append / self-append / null-termination / truncation (string layer) | Covered by blue-team 7; not re-opened (outside owned files) |

The 1200-case fuzz run produced **0 CRASH, 0 CORRUPT, 0 UNEXPECTED_OK** both
before and after the fix — no further memory-safety anomaly surfaced.

---

## 3. GENUINE FINDING and minimal fix

**Defect.** In `cplus_memcmp_s_auto` the NULL-operand branch was:

```c
return (s1 == s2) ? 0 : (s1 ? 1 : -1);
```

With `s1 == s2 == NULL` and `n > 0` it returned **0** ("equal") while setting
`errno = EINVAL`. The auto API's error sentinel is `-1` (a genuine "less-than"
collision); returning 0 is a **fail-open**: a caller testing only the return
value treats two invalid buffers as equal. This contradicts the checked API's
own contract (a NULL operand is a constraint violation) and the behaviour of
the direct `cplus_memcmp_s`, which returns ESNULLP. The one-NULL cases were
correct (nonzero return + EINVAL) and are exercised by
`tests/crt/unit/test_auto_bounds_s.c`.

**Fix (one line, `stdlib/safe/src/safe_memory.c`).**

```c
-        return (s1 == s2) ? 0 : (s1 ? 1 : -1);
+        /* A NULL operand is a constraint violation, never an "equal" result.
+         * Return -1 (the error sentinel) for the both-NULL case too; the
+         * one-NULL ordering sentinels are kept for compatibility. */
+        return (s1 == s2) ? -1 : (s1 ? 1 : -1);
```

Only the both-NULL result changes (0 → -1); `s1=NULL,s2=valid` still returns
`-1` and `s1=valid,s2=NULL` still returns `1`, so all existing tests keep their
exact expected values. No signature, symbol, struct layout, macro or exported
value changed. No valid-input result changes.

**Minimal reproducer:** `tests/defense/memory2/regression_memcmp_null.c`.

---

## 4. Exact before/after commands and outputs

Working directory: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.
Compiler `/usr/bin/clang` (Apple clang 16).
Only the `w_safe_memory.o` archive member differs between the two builds;
every safeclib / allocator / string object is byte-identical.

```sh
CPLUS_CLANG=/usr/bin/clang sh tests/defense/memory2/run_before_after.sh
```

```
===== BEFORE (HEAD, pre-fix) =====
FAIL: memcmp_s_auto(NULL,NULL,8) = 0 errno=22 (expected -1 / EINVAL)
regression_exit=1
CASE memcmp_both_null                   UNEXPECTED_OK
ATTACK_TOTALS REFUSED=10 CRASH=0 CORRUPT=0 UNEXPECTED_OK=1 OK=5
FUZZ_TOTALS cases=1200 REFUSED=394 CRASH=0 CORRUPT=0 UNEXPECTED_OK=0 OK=806
===== AFTER (fixed) =====
PASS regression_memcmp_null
regression_exit=0
CASE memcmp_both_null                   REFUSED
ATTACK_TOTALS REFUSED=11 CRASH=0 CORRUPT=0 UNEXPECTED_OK=0 OK=5
FUZZ_TOTALS cases=1200 REFUSED=394 CRASH=0 CORRUPT=0 UNEXPECTED_OK=0 OK=806
```

Standalone hardened run:

```sh
CPLUS_CLANG=/usr/bin/clang sh tests/defense/memory2/run.sh
```

```
CASE dmax_over_tracked                  REFUSED
CASE direct_valid                       OK
CASE smax_gt_dmax                       REFUSED
CASE src_overread                       REFUSED
CASE interior_bos                       REFUSED
CASE memcmp_src_small                   REFUSED
CASE memcmp_both_null                   REFUSED
CASE memcmp_one_null                    OK
CASE uaf_exact                          REFUSED
CASE uaf_interior                       OK
CASE double_free                        REFUSED
CASE guard_direct                       OK
CASE guard_src                          OK
CASE realloc_atomic                     REFUSED
CASE alloc_overflow                     REFUSED
CASE sizemax_auto                       REFUSED
ATTACK_TOTALS REFUSED=11 CRASH=0 CORRUPT=0 UNEXPECTED_OK=0 OK=5
FUZZ_TOTALS cases=1200 REFUSED=394 CRASH=0 CORRUPT=0 UNEXPECTED_OK=0 OK=806
== regression (timeout 30) ==
PASS regression_memcmp_null
harness_exit=0 regression_exit=0
```

---

## 5. Honest limitations / escalated (NOT claimed fixed)

These are real but cannot be closed by editing only `safe_memory.{h,c}` with
static helpers — closing them needs additions to `safe_alloc.{h,c}` or the
third-party layer, which is outside this agent's ownership:

1. **Interior-pointer UAF.** `cplus_safe_ptr_was_freed` uses a cache of exact
   freed *base* addresses; `cplus_safe_reject_uaf(freed_base + offset)` does
   not detect it. The harness's `uaf_interior` case is therefore `OK`
   (silent write to freed memory) both before and after. Fixing it requires a
   "recently-freed ranges" query in `safe_alloc.c`, which this layer cannot
   synthesize from the existing public API. (An exact freed pointer *is*
   refused — `uaf_exact` = REFUSED.)
2. **Recent-free-cache exhaustion (1024 entries).** A very old freed address
   evicted from the cache is reported as `invalid_free` rather than
   `double_free` (both refuse — safety holds, classification accuracy does
   not). Already documented in `safe_alloc.h` / `tests/defense/alloc/HARDENING.md`.
3. **Untracked stack destination with a lying `dmax` (direct `_s` API).**
   `__builtin_object_size` of a pointer parameter inside a non-inlined callee
   is unknown, and a stack buffer has no allocator metadata, so the direct
   adapter cannot clamp a false `dmax`; safeclib's bounded clear can then
   stay inside the lie. The `_auto` macros close this for callers who use
   them (compiler-visible BOS is passed at the call site). Already documented
   in `tests/defense/memory/HARDENING.md`.
4. `cplus_memcmp_s_auto(NULL,NULL,0) == 0` is intentionally retained: with
   `n == 0` no bytes are compared, and `tests/crt/unit/test_auto_bounds_s.c`
   codifies this exact result.

This remains runtime constraint checking, not pointer-provenance / lifetime
proof. No Rust-grade memory-safety claim is made.

---

## 6. Regression and ABI verification (all after the fix)

| Check | Result |
|---|---|
| `sh tests/defense/memory2/run.sh` (timeout 60) | harness_exit=0, regression_exit=0; 0 CRASH / 0 CORRUPT / 0 UNEXPECTED_OK |
| `sh tests/methods/run_methods.sh` | 14 passed, 0 failed (`methods_memory` 18/18, `methods_string` 318/318) |
| `sh tests/methods/run_hundred.sh` | `hundred_memory` 100/100 in all 4 variants; `hundred_string` 100/100 ×4 |
| `sh tests/libc/run_libc_tests.sh` | 109 passed, 0 failed |
| `sh tests/crt/unit/run_unit_tests.sh` | 85 passed, 0 failed (`test_auto_bounds_s` one-NULL sentinels unchanged) |
| `sh tests/defense/memory/run.sh` | PASS (exit 0) |
| `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` | **ABI fingerprint MATCHES baseline (1238 records), diff empty** |

Note on `run_hundred.sh`: it reports 4 unrelated pre-existing failures —
`hundred_alloc (plain)` cases 77/81/82 (`cplus_aligned_alloc` leak accounting,
plain-libc layer, which does not link the changed object) and `hundred_format`
case 63 (`snprintf_s/positional`, a different SafeCRT source file). Neither
touches `cplus_memcmp_s_auto`; all memory/string suites are green.

ABI evidence (exported symbol set of `w_safe_memory.o`, before vs after):

```
T _cplus_memcmp_s        T _cplus_memcmp_s_auto   T _cplus_memcpy_s
T _cplus_memcpy_s_auto   T _cplus_memmove_s       T _cplus_memmove_s_auto
T _cplus_memset_s        T _cplus_memset_s_auto   T _cplus_memzero_s
T _cplus_safe_set_unknown_capacity_mode
T _cplus_safe_unknown_capacity_strict
SYMBOL SET IDENTICAL
```

All changes are in already-exported functions; no new symbol was added. The
ABI fingerprint comparison exited 0 with an empty diff, so there is no new
symbol to report.
