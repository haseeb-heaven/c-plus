# Final stdlib security audit — severity-ranked findings and audit skeleton

- **Date:** 2026-11-23
- **Branch / baseline:** `agent/modern-stdlib` / audit baseline commit `595531f`
- **Purpose:** Task 6 synthesis — one severity-ranked, evidence-linked finding list for the
  hardened libc / SafeCRT / modern-stdlib wrapper work, with a skeleton for the remaining
  audit and sign-off.
- **Ownership note:** This document is the sole file changed by this agent. No source, test,
  or other doc was edited; nothing was staged or committed.

---

## 1. Purpose and scope

This document **synthesizes previously verified findings**; it does not run new dynamic
analysis and does not invent vulnerabilities. Every entry below traces to a source audit,
a fix commit, an acceptance test, or a recorded sanitizer result. Where a cited sibling
document was unavailable, the source/test is cited directly and the row is marked
**PENDING** (see §2).

Classification rule used throughout:

| Class | Meaning |
|---|---|
| **CONFIRMED** | A concrete, verified defect: status `FIXED` (named fix commit + accepted test) or `OPEN` (actionable runtime finding per the source audit, no fix yet). |
| **UNVERIFIED RISK** | External-dependency findings: `GAP-COMPILER` (needs Agent 1 compiler work) and `BLOCKED` (missing API / no dependency-closed upstream). Not a demonstrated runtime exploit in this branch. |
| **THEORETICAL / DOCUMENTED-CONTRACT** | Inherent or caller-owned limitations, host-delegated semantics, and theoretical edge cases. Explicitly **not** a safety claim. |

---

## 2. Sources consulted and availability

Primary sources (present and used):

- `docs/libc/AUDIT_FINDINGS_TRACKER.md` and its machine-readable source of truth
  `docs/libc/audit_findings.json` — 127 reconciled findings, fix commits, acceptance tests.
- `docs/libc/WRAPPER_SAFETY_AUDIT.md` — aggregate inventory (672 callables) and 16 HIGH findings.
- Family audits: `docs/libc/AUDIT_alloc.md`, `AUDIT_memory_string.md`,
  `AUDIT_stdio_format.md`, `AUDIT_numeric_wide.md`, `AUDIT_lifecycle.md`.
- Verification: `docs/libc/AUDIT_HIGH_REGRESSION.md` (`audit_high` suite, 113/113 checks),
  `docs/stdlib/TASK6_FINAL_REPORT.md`, `docs/libc/AGENT2_CURRENT_AUDIT.md`.
- Security-test NOTES: `tests/scenarios/cve_like/NOTES_1.md` … `NOTES_5.md` (baseline
  ASan/TSan crash evidence and safe-variant results for cases 01–15).

Task-mentioned paths that were **not present** in this branch (handled as substitutes, all
relevant rows marked PENDING where they depend on them):

| Requested path | Status | Substitute used |
|---|---|---|
| `docs/stdlib/agent2_audit/` | **absent** | `docs/libc/AGENT2_CURRENT_AUDIT.md` + the five family `AUDIT_*.md` |
| `docs/stdlib/system-api-safety.md` | **absent** (a sibling `docs/stdlib/system_api/API_INVENTORY.md` appeared during this work, but contains no finding register) | `docs/libc/WRAPPER_SAFETY_AUDIT.md` §2/§3, `AUDIT_lifecycle.md`, `AUDIT_numeric_wide.md` |
| `tests/security/**` | **no NOTES present** (a sibling `tests/security/stdlib/fs_security.cp`, `run.sh`, `hostile_inputs/_probe.cp` appeared during this work, but no finding/NOTES content) | `tests/scenarios/cve_like/NOTES_1..5.md` + `tests/libc/run_attack_tests.sh` evidence |

No vulnerability is asserted solely from an absent document; every claim below stands on
the cited present source or test. These three paths remain **PENDING** as inputs because no
sibling finding register or NOTES existed at synthesis time.

---

## 3. Severity model (P0–P3)

The upstream audits use HIGH / MEDIUM / LOW / DOCUMENTED_LIMIT / INFO. This synthesis maps
them to P0–P3 by combining intrinsic severity with fix state (so the ranking is actionable):

| P-level | Definition | Source mapping | Count (CONFIRMED) |
|---|---|---|---:|
| **P0** | Unmitigated, high-severity memory-corruption / UAF / OOB-write; still OPEN | HIGH + OPEN | 5 |
| **P1** | High-severity, fixed and regression-guarded; remaining risk is regression only | HIGH + FIXED | 10 |
| **P2** | Medium-severity correctness / hardening gaps | MEDIUM + (FIXED or OPEN) | 41 |
| **P3** | Low-severity / informational confirmed findings | LOW + (FIXED or OPEN) | 7 |
| | | **Total CONFIRMED** | **63** |

Note: one HIGH finding (`LIFE-03`, longjmp-skips-RAII) is classified
DOCUMENTED-CONTRACT, so it is counted in §8, not in CONFIRMED — hence 15 confirmed HIGH
(5 P0 + 10 P1) rather than 16.

---

## 4. Severity counts

### 4.1 By class (all 127 upstream findings)

| Class | Count |
|---|---:|
| CONFIRMED — FIXED | 17 |
| CONFIRMED — OPEN | 46 |
| **CONFIRMED total** | **63** |
| UNVERIFIED RISK — GAP-COMPILER | 7 |
| UNVERIFIED RISK — BLOCKED | 4 |
| **UNVERIFIED RISK total** | **11** |
| THEORETICAL / DOCUMENTED-CONTRACT | 53 |
| **Grand total** | **127** |

### 4.2 CONFIRMED by P-level and family

| Family | P0 | P1 | P2 | P3 | Total |
|---|---:|---:|---:|---:|---:|
| ALLOC | 3 | 0 | 8 | 0 | 11 |
| MEMSTR | 2 | 4 | 14 | 0 | 20 |
| STDIO | 0 | 3 | 4 | 1 | 8 |
| NUMWIDE | 0 | 1 | 5 | 1 | 7 |
| LIFE | 0 | 2 | 10 | 5 | 17 |
| **Total** | **5** | **10** | **41** | **7** | **63** |

Cross-check: upstream severity totals HIGH 16 / MEDIUM 68 / LOW 38 / DOCUMENTED_LIMIT 4 /
INFO 1 (=127) reconcile with `WRAPPER_SAFETY_AUDIT.md` §2 (16 / 68 / 43).

---

## 5. Top 5 confirmed issues

All five are **P0 / OPEN HIGH** — unmitigated, with direct UAF or OOB-write impact and a
recorded reproducer. Ordered by exploitability.

### 5.1 ALLOC-01 — generation-blind checked free/transfer (stale alias frees the new live block)

- **Affected function:** `cplus_free`, `cplus_free_checked`, `cplus_transfer_ownership` (`libc/src/stdlib/alloc.c:340,373,524`).
- **Triggering input:** allocate `p`; free/reallocate so the address is reissued to a new live block; pass the stale alias `p` to a checked free or transfer.
- **Reproducer path:** `tests/libc/run_attack_tests.sh` (`test_alloc_attack.c:221-231,311-346,556-613`).
- **Impact:** use-after-free — the library frees or transfers the *new* owner's block; heap corruption / double-free of live memory.
- **Root cause:** `meta_find` keys the live table by address only; no generation comparison; `cplus_free` discards the checked API's refusal.
- **Fix:** **not fixed** (no commit in `5fe387d`/`d45681b`).
- **Regression test:** `sh tests/libc/run_attack_tests.sh`; proposed generation-aware `cplus_free_checked_gen(void **, unsigned long long)`.
- **Verification evidence:** `docs/libc/AUDIT_alloc.md` §1 rows 4/5/14; `tests/adversarial/alloc_attack_NOTES.md:62-98`; `CONTRACT_STATUS.md:48-55,85-89`.
- **Severity:** **P0** (source HIGH, OPEN).

### 5.2 ALLOC-02 — host `free()` of a tracked pointer leaves false-live metadata

- **Affected function:** `cplus_malloc` / `cplus_calloc` metadata and query APIs (`alloc.c:216,242,413-572`).
- **Triggering input:** `void *p = cplus_malloc(n); free(p);` using the host allocator directly.
- **Reproducer path:** `tests/libc/run_attack_tests.sh` (`test_alloc_attack.c:73-75,182-200,624-641`).
- **Impact:** metadata table still reports the block live; permanent `live_count`/`live_bytes` inflation; stale liveness queries.
- **Root cause:** host `free` bypasses the metadata table; no `cplus_alloc_forget(ptr)` API exists.
- **Fix:** **not fixed**.
- **Regression test:** `sh tests/libc/run_attack_tests.sh`; proposed `cplus_alloc_forget()`.
- **Verification evidence:** `docs/libc/AUDIT_alloc.md` §1 rows 1/2/6/15; `CONTRACT_STATUS.md:56-59`.
- **Severity:** **P0** (source HIGH, OPEN).

### 5.3 ALLOC-03 — two aligned allocators with opposite deallocators; untracked `cplus_aligned_alloc`

- **Affected function:** `cplus_aligned_alloc` (`stdlib_c17.c:53`), `aligned_alloc_s`, macro `free` under `CPLUS_MALLOC_TRACKING`.
- **Triggering input:** allocate with `cplus_aligned_alloc`, then release with `free`; on Windows allocate with `_aligned_malloc`/`_aligned_malloc` and release with `free` instead of `_aligned_free`.
- **Reproducer path:** `tests/run_all_evidence.sh` (`asan_sc_11`, `test_alloc_asan.c:320-348`).
- **Impact:** untracked memory (all liveness/metadata queries return 0, `cplus_free` refuses → leak); wrong-deallocator crash; macro-driven deallocator confusion.
- **Root cause:** `cplus_aligned_alloc` deliberately untracked; Windows requires `_aligned_free`; `CPLUS_MALLOC_TRACKING` redefines `free` to `cplus_free`.
- **Fix:** **not fixed**; proposed namespaced `cplus_aligned_free`.
- **Regression test:** `sh tests/run_all_evidence.sh`.
- **Verification evidence:** `docs/libc/AUDIT_alloc.md` §1 row 19; `cplus_libc.h:69-78`.
- **Severity:** **P0** (source HIGH, OPEN).

### 5.4 MEMSTR-07 — `cplus_strcpy` has no capacity and writes no NUL on refusal

- **Affected function:** `cplus_strcpy` (`libc/src/string/string.c:9-13`).
- **Triggering input:** copy a long/unterminated source into a small fixed destination.
- **Reproducer path:** `tests/libc/run_libc_tests.sh` (source audit identifies the unbounded `while((*d++=*src++))` loop).
- **Impact:** linear stack/heap OOB write; destination left unterminated; classic buffer-overflow primitive.
- **Root cause:** raw bundled-C delegate — only a NULL check; no capacity parameter, no source bound, no NUL written on refusal.
- **Fix:** **not fixed** (explicitly *not* changed by `5fe387d` — see tracker reconciliation note 5).
- **Regression test:** `sh tests/libc/run_libc_tests.sh`; hardened alternative is the `_s`/`_bounded` tiers.
- **Verification evidence:** `docs/libc/AUDIT_memory_string.md` §1/§5, `string.c:9-13`; tracker §5.2.
- **Severity:** **P0** (source HIGH, OPEN).

### 5.5 MEMSTR-09 — `cplus_strcat` scans then copies unbounded

- **Affected function:** `cplus_strcat` (`string.c:22-26`).
- **Triggering input:** concatenate onto a destination that is already near its capacity (or unterminated).
- **Reproducer path:** `tests/libc/run_libc_tests.sh`.
- **Impact:** OOB read during the scan and OOB write during the copy; buffer overflow.
- **Root cause:** unbounded scan-to-NUL then unbounded copy; no capacity or source bound; NULL-only guard.
- **Fix:** **not fixed**.
- **Regression test:** `sh tests/libc/run_libc_tests.sh`.
- **Verification evidence:** `docs/libc/AUDIT_memory_string.md` §1/§5, `string.c:22-26`; tracker §5.2.
- **Severity:** **P0** (source HIGH, OPEN).

---

## 6. CONFIRMED findings register (P0–P3)

All 63 confirmed findings, severity-ranked. "Fix" is the verified fix commit or **not fixed**;
"Test" is the acceptance/regression test; "Evidence" is the recorded verification. Source IDs
follow the tracker: ALLOC `F-01…F-20`, STDIO `F1…F15`, NUMWIDE `F01…F21`, LIFE `F1…F21`
verbatim; MEMSTR IDs are derived (source `AUDIT_memory_string.md` numbers only its top 5).

### 6.1 P0 — unmitigated HIGH (OPEN)

| ID | P | Function | Triggering input | Reproducer | Impact | Root cause | Fix | Regression test | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| ALLOC-01 | P0 | `cplus_free`, `cplus_free_checked`, `cplus_transfer_ownership` | stale alias at reissued address | `run_attack_tests.sh` (`test_alloc_attack.c:221-231,311-346`) | UAF: frees/transfers new live block | address-keyed `meta_find`, no generation compare | not fixed | `sh tests/libc/run_attack_tests.sh` | `AUDIT_alloc.md` §1 #4/#5/#14 |
| ALLOC-02 | P0 | `cplus_malloc`/`cplus_calloc` + metadata queries | host `free()` of tracked pointer | `test_alloc_attack.c:182-200,624-641` | false-live metadata, accounting inflation | host free bypasses table; no forget API | not fixed | `sh tests/libc/run_attack_tests.sh` | `AUDIT_alloc.md` §1 #1/#2/#15 |
| ALLOC-03 | P0 | `cplus_aligned_alloc`, `aligned_alloc_s`, macro `free` | aligned alloc freed with `free`/wrong deallocator | `test_alloc_asan.c:320-348` (`asan_sc_11`) | untracked memory, leak/wrong-deallocator crash | untracked allocator; Windows `_aligned_free`; macro redefine | not fixed | `sh tests/run_all_evidence.sh` | `AUDIT_alloc.md` §1 #19 |
| MEMSTR-07 | P0 | `cplus_strcpy` | long/unterminated source → small dest | `tests/libc/run_libc_tests.sh` | OOB write, no NUL on refusal | unbounded copy; no capacity/source bound | not fixed | `sh tests/libc/run_libc_tests.sh` | `AUDIT_memory_string.md` §1 `string.c:9-13` |
| MEMSTR-09 | P0 | `cplus_strcat` | concat onto near-full/unterminated dest | `tests/libc/run_libc_tests.sh` | OOB scan + OOB write | unbounded scan then copy | not fixed | `sh tests/libc/run_libc_tests.sh` | `AUDIT_memory_string.md` §1 `string.c:22-26` |

### 6.2 P1 — HIGH, fixed and regression-guarded

Evidence for every P1 row from `5fe387d` is additionally
`docs/libc/AUDIT_HIGH_REGRESSION.md`: `audit_high: 113/113 checks ok` plain and UBSan
(10/10 and 5/5 repeated runs; 92/92 under `LC_ALL=C`).

| ID | P | Function | Triggering input | Reproducer | Impact (pre-fix) | Root cause | Fix | Regression test | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| MEMSTR-08 (F#2) | P1 | `cplus_strncpy` | `n=SIZE_MAX` / `SIZE_MAX/2` into 8-byte dest | `test_strncpy_huge_n.c` pattern | multi-byte overflow, unterminated dest | n-byte zero-fill with no clamp | `5fe387d` | `run_libc_tests.sh` + `run_unit_tests.sh` (`test_string_extra*.c`, `test_audit_high.c`) | `AUDIT_memory_string.md` §5 #2; high-regression |
| MEMSTR-15 (F#3) | P1 | `cplus_memrchr` | `memrchr(smallbuf,'x',1<<20)`; `memrchr(NULL,'x',4)` | `test_string_extra2.c` + guard-byte test | OOB first read; NULL segfault | downward scan from `p[n-1]`, no NULL guard | `5fe387d` | `run_libc_tests.sh` + `run_unit_tests.sh` | `AUDIT_memory_string.md` §5 #3; high-regression |
| MEMSTR-28 | P1 | `cplus_strncpy_bounded` | huge `n` in default (unknown-capacity) mode | `test_string_extra*.c` | zero-fill overflow | unchecked pass-through to raw loop | `5fe387d` | `run_libc_tests.sh` + `run_unit_tests.sh` | `AUDIT_memory_string.md` §5 #2; high-regression |
| MEMSTR-47 (F#1) | P1 | `cplus_isblank` | `cplus_isblank((char)0xE9)`, `0xFE`, `EOF` | `tests/libc/test_ctype2.c` | signed-char UB | uncast `int` passed to `isblank` | `b7d1686`, `5009b6b` | `sh tests/libc/run_libc_tests.sh` | tracker reconciliation note 4; `test_ctype2.c` |
| STDIO-01 (F1) | P1 | `cplus_fclose_s` | colliding `FILE*` addresses, second close | `test_fclose_s.c` | double-close UAF reaching host `fclose` | probe-chain deletion without tombstones | `5fe387d`, `d45681b` | `sh tests/crt/unit/run_unit_tests.sh` | high-regression map; `AUDIT_stdio_format.md` #1 |
| STDIO-02 (F2) | P1 | `cplus_fclose` | cross-registry close / false rejection | `test_fclose_s.c` | fd leak, cross-registry bypass | registry not reconciled | `5fe387d`, `d45681b` | `sh tests/crt/unit/run_unit_tests.sh` | high-regression map; `AUDIT_stdio_format.md` #2 |
| STDIO-03 (F3) | P1 | bounded/evicted close caches | 256-ring / 512-slot wrap + double close | `test_fclose_s.c` | evicted double-close reaches host | bounded cache wrap | `5fe387d`, `d45681b` | `sh tests/crt/unit/run_unit_tests.sh` | high-regression map; `AUDIT_stdio_format.md` #3 |
| NUMWIDE-01 (F01) | P1 | `cplus_swprintf_checked` | `%n`, `%1$n`, `%lln` | `tests/libc/test_wchar_format.c` | arbitrary write via `%n` | format `%n` pass-through, no honoured static check | `5fe387d` | `run_libc_tests.sh` + high-regression | `WIDE_FORMAT_CONTRACT.md`; high-regression |
| LIFE-01 (F1) | P1 | `cplus_thrd_create` / detach | worker outlives creating frame (poison `0xA5`) | `test_threads_c11.c` | detached-thread context UAF (pre-fix crashed 3/3 SIGBUS) | `{func,arg}` on caller stack | `5fe387d` | `sh tests/crt/unit/run_unit_tests.sh` | high-regression map; `TASK6_FINAL_REPORT.md` §8 |
| LIFE-02 (F2) | P1 | `cplus_mbrtoc16/32`, `cplus_c16rtomb` | split UTF-8 through `ps==NULL` + canary | `test_uchar.c` | 4-byte state used as 128-byte `mbstate_t` → overflow | `static unsigned` cast to `mbstate_t*` | `5fe387d` | `sh tests/crt/unit/run_unit_tests.sh` | high-regression map; `TASK6_FINAL_REPORT.md` §8 |

### 6.3 P2 — MEDIUM confirmed (41)

FIXED MEDIUM: MEMSTR-12, MEMSTR-33, LIFE-04, LIFE-05, LIFE-06, LIFE-07 (all `5fe387d`,
verified by `AUDIT_HIGH_REGRESSION.md` and `run_unit_tests.sh`). All other rows are
OPEN ("not fixed"); their evidence is the source-audit verdict plus the listed test.

| ID | P | Function | Triggering input | Reproducer | Impact | Root cause | Fix | Regression test | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| ALLOC-05 | P2 | `cplus_free_checked`, counters | fresh host malloc at just-freed cplus address | `run_attack_tests.sh` | false double-free refusal | address-only recent cache | not fixed | `run_attack_tests.sh` | `AUDIT_alloc.md` §1 #5 |
| ALLOC-06 | P2 | recent-cache classification | fill 1024-slot table / SafeCRT direct-map collision | `run_attack_tests.sh` | misclassified invalid/free | first-slot overwrite heuristic | not fixed | `run_attack_tests.sh` | `AUDIT_alloc.md` §1 #5/#16 |
| ALLOC-08 | P2 | `cplus_calloc` vs `calloc_s` | `nmemb*size` overflow | `run_all_evidence.sh` | inconsistent `errno` (ENOMEM vs EOVERFLOW) | divergent error mapping | not fixed | `run_all_evidence.sh` | `AUDIT_alloc.md` §1 #2 |
| ALLOC-09 | P2 | `cplus_alloc_fail_after` | `fail_after(-1)` | `run_all_evidence.sh` | disable vs fail-all divergence | layer-specific negative semantics | not fixed | `run_all_evidence.sh` | `AUDIT_alloc.md` §1 #17 |
| ALLOC-10 | P2 | injection state | concurrent injection use | `run_all_evidence.sh` | global state, no getter; `g_seq` counts decisions | process-global design | not fixed | `run_all_evidence.sh` | `AUDIT_alloc.md` §1 #17 |
| ALLOC-11 | P2 | `cplus_free`, `cplus_free_checked` | refused free of live pointer | `run_attack_tests.sh` | drops caller's only live handle | `*inout` nulled even on refusal | not fixed | `run_attack_tests.sh` | `AUDIT_alloc.md` §1 #4/#5 |
| ALLOC-12 | P2 | bounded `memcpy`/`memmove` | oversized source, unknown capacity | `run_all_evidence.sh` | source OOB read unchecked | dest-only capacity check; unknown unchecked | not fixed | `run_all_evidence.sh` | `AUDIT_alloc.md` §1 `bounded.c` |
| ALLOC-14 | P2 | `aligned_alloc_s` | `size % alignment != 0` | `run_all_crt.sh` | C11 constraint not enforced | missing modulo check | not fixed | `sh tests/crt/run_all_crt.sh` | tracker §5.1 |
| MEMSTR-01 | P2 | `cplus_memcpy` | oversized/overlapping copy | `run_libc_tests.sh` | OOB write, overlap corruption | no capacity/source bound, no overlap check | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| MEMSTR-10 | P2 | `cplus_strncat` | dest too small for `n` | `run_libc_tests.sh` | OOB write | no dest capacity check | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| MEMSTR-13 | P2 | `cplus_strchr`, `cplus_strrchr` | NULL arg / high-bit `int` | `run_libc_tests.sh` | NULL deref; truncated match | no NULL guard; EOF high-bit truncation | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| MEMSTR-14 | P2 | `cplus_strstr` | NULL arg / unterminated input | `run_libc_tests.sh` | NULL deref / OOB scan | no NULL guard, no bound | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| MEMSTR-16 | P2 | `strcasecmp`, `strncasecmp` | NULL arg / non-ASCII | `run_libc_tests.sh` | NULL deref; wrong folding | no NULL guard; ASCII-only | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| MEMSTR-24 | P2 | `cplus_memcpy_bounded` | oversized source | `run_libc_tests.sh` | source OOB read, overlap | no source capacity / overlap check | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §5 #5 |
| MEMSTR-25 | P2 | `cplus_memmove_bounded` | oversized source, unknown mode | `run_libc_tests.sh` | source OOB read | no source capacity; unknown unchecked | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §5 #5 |
| MEMSTR-26 | P2 | `cplus_memcmp_bounded` | insufficient capacity | `run_libc_tests.sh` | refusal indistinguishable from equality | returns 0 on refusal | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §5 #4 |
| MEMSTR-27 | P2 | `cplus_strcpy_bounded` | unterminated source | `run_libc_tests.sh` | strlen over-read before cap check | `strlen` on unbounded source | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| MEMSTR-29 | P2 | `cplus_strcat_bounded` | unterminated dest/source | `run_libc_tests.sh` | OOB scans | `strlen` on unterminated buffers | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| MEMSTR-30 | P2 | `cplus_strncat_bounded` | unterminated dest/source | `run_libc_tests.sh` | OOB scans | unbounded scan to NUL | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| MEMSTR-42 | P2 | `cplus_swab` | negative signed `n` | `run_libc_tests.sh` | large size_t from negative | signed `ssize_t n` unvalidated | not fixed | `run_libc_tests.sh` | `AUDIT_memory_string.md` §1 |
| STDIO-04 | P2 | `stdio2.c`/`stdio3.c` delegates | NULL stream | `run_libc_tests.sh` | NULL-deref UB | unvalidated host delegate | not fixed | `run_libc_tests.sh` | `AUDIT_stdio_format.md` F4 |
| STDIO-05 | P2 | `fread_s`, `fwrite_s` | bogus `bmax` | `run_all_crt.sh` | silent clamp, no errno | trusts caller `bmax` | not fixed | `run_all_crt.sh` | `AUDIT_stdio_format.md` F5 |
| STDIO-09 | P2 | `freopen`, `fdopen` | reopen over registered stream | `run_libc_tests.sh` | stale registry entry | registry not reconciled | not fixed | `run_libc_tests.sh` | `AUDIT_stdio_format.md` F9 |
| STDIO-10 | P2 | checked-format literal gate | literal-gate false positive | `run_all_crt.sh` | `_exit(70)` trap; temp-file leak | trap on false positive | not fixed | `run_all_crt.sh` | `AUDIT_stdio_format.md` F10 |
| NUMWIDE-04 | P2 | `cplus_exp10l` | non-binary64 `long double` ABI | `run_libc_tests.sh` | wrong result `0.0L` | ABI-dependent implementation | not fixed | `run_libc_tests.sh` | `AUDIT_numeric_wide.md` F04 |
| NUMWIDE-05 | P2 | `abs`, `labs`, `llabs`, `imaxabs` | most-negative value | `run_all_crt.sh` | signed overflow UB | negate INT_MIN | not fixed | `run_all_crt.sh` | `AUDIT_numeric_wide.md` F05 |
| NUMWIDE-06 | P2 | `atoi`, `atol` | out-of-range numeric string | `run_all_crt.sh` | silent truncation, no ERANGE | host delegate | not fixed | `run_all_crt.sh` | `AUDIT_numeric_wide.md` F06 |
| NUMWIDE-12 | P2 | `strto*`, `atof` | NULL `nptr` | `run_all_crt.sh` | NULL deref | missing guard present elsewhere | not fixed | `run_all_crt.sh` | `AUDIT_numeric_wide.md` F12 |
| NUMWIDE-13 | P2 | time/strftime/wcsftime | NULL `tp`/`ts`/`req`/`tm` | `run_libc_tests.sh` | NULL deref | missing NULL guards | not fixed | `run_libc_tests.sh` | `AUDIT_numeric_wide.md` F13 |
| LIFE-04 | P2 | signal set wrappers | NULL `sigset_t*` | `test_audit_high.c` | NULL deref → host | inconsistent NULL guards | `5fe387d` | `run_unit_tests.sh` | high-regression map (F4) |
| LIFE-05 | P2 | fenv wrappers | NULL env pointer | `test_audit_high.c` | NULL deref | inconsistent NULL guards | `5fe387d` | `run_unit_tests.sh` | high-regression map (F5) |
| LIFE-06 | P2 | `cplus_libc_log` | `errno` sentinel + failed `write(2)` | `test_audit_high.c` | clobbers caller `errno` | no save/restore | `5fe387d` | `run_unit_tests.sh` | high-regression map (F6) |
| LIFE-07 | P2 | `cplus_libc_log` | nonblocking pipe / EINTR / SIGPIPE | logger tests | dropped/short writes | no retry loop | `5fe387d` | `run_unit_tests.sh` | high-regression map (F7) |
| LIFE-08 | P2 | `safe_runtime` control state | concurrent config access | `run_all_evidence.sh` | data race | racy under C11/C++11 model | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F8 |
| LIFE-09 | P2 | cleanup registry | `thrd_exit`/`pthread_exit` in guard | `run_all_evidence.sh` | TLS cleanups not unwound | registry TLS not unwound | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F9 |
| LIFE-10 | P2 | report/violation handler | re-entrant violation | `run_all_evidence.sh` | unbounded recursion | no recursion latch | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F10 |
| LIFE-11 | P2 | `safe_log`, `safe_stats` | logging from signal handler | `run_all_evidence.sh` | deadlock / recursion | not async-signal-safe | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F11 |
| LIFE-12 | P2 | `mtx_timedlock` fallback | unenforced preconditions | `run_all_evidence.sh` | incorrect blocking | polling loop w/o checks | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F12 |
| LIFE-13 | P2 | `ONCE_FLAG_INIT` | non-matching platform ABI | `run_all_evidence.sh` | ABI mismatch | hardcoded signature | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F13 |
| MEMSTR-12 | P2 | `cplus_memchr` | `memchr(NULL,'x',4)` / huge `n` | `test_string_extra*.c` | NULL deref / OOB read | no NULL guard, no clamp | `5fe387d` | `run_libc_tests.sh` + `run_unit_tests.sh` | high-regression; AUDIT_memstr #3 |
| MEMSTR-33 | P2 | `cplus_stpncpy` | huge `n` tail | `test_string_extra*.c` | tail `memset` OOB | unbounded zero tail | `5fe387d` | `run_libc_tests.sh` + `run_unit_tests.sh` | high-regression; AUDIT_memstr #3 |

### 6.4 P3 — LOW confirmed (7)

| ID | P | Function | Triggering input | Reproducer | Impact | Root cause | Fix | Regression test | Evidence |
|---|---|---|---|---|---|---|---|---|---|
| STDIO-15 | P3 | `fmt_check_dest_bounds` | pointer-into-object arithmetic | `run_all_crt.sh` | overstated capacity | interior-pointer math | not fixed | `run_all_crt.sh` | `AUDIT_stdio_format.md` F15 |
| NUMWIDE-16 | P3 | `cplus_libc_log` (errno) | failed `write(2)` | `test_audit_high.c` | errno clobber | no save/restore | `5fe387d` | `run_unit_tests.sh` | high-regression (F16) |
| LIFE-14 | P3 | errno plumbing | `__cplus_errno_location` use | `run_all_evidence.sh` | incomplete errno mapping | macro absent | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F14 |
| LIFE-15 | P3 | adversarial logging test/notes | stale expectations | `run_all_evidence.sh` | no runtime impact | test drift after hardening | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F15 |
| LIFE-16 | P3 | telemetry | concurrent reset + >64 slots | `run_all_evidence.sh` | dropped entries, racy reset | fixed cap | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F16 |
| LIFE-17 | P3 | TSS destructors, `call_once` | limit/recursion probes | `run_all_evidence.sh` | unchecked delegation | no checks | not fixed | `run_all_evidence.sh` | `AUDIT_lifecycle.md` F17 |
| LIFE-18 | P3 | uchar internal state | `cplus_mbrtoc* ps == NULL` | `run_unit_tests.sh` | process-wide state | fallback storage | not fixed | `run_unit_tests.sh` | `AUDIT_lifecycle.md` F18 |

---

## 7. UNVERIFIED RISK (11) — not confirmed runtime exploits

These require Agent 1 compiler work or a dependency-closed upstream source; SKIPs are
retained and no implementation was fabricated. They are **not** counted as confirmed bugs.

| ID | Source | Severity | What is missing | Status / gate |
|---|---|---|---|---|
| ALLOC-07 | `AUDIT_alloc.md` F07 | MEDIUM | `alloc_size` attributes / `.len` lowering on exported allocators | `GAP-COMPILER` — `docs/patches/agent1-len-managed-allocators.md`; `ACCEPT` is not enforcement |
| STDIO-07 | `AUDIT_stdio_format.md` F7 | MEDIUM | checked-format literal gate reliability; `v*`/direct-call bypass | `GAP-COMPILER` — `COMPILE_TIME_GAPS.md` |
| STDIO-08 | `AUDIT_stdio_format.md` F8 | MEDIUM | Annex-K `scanf_s` divergence (no `rsize_t`, width literal) | `GAP-COMPILER` |
| STDIO-14 | `AUDIT_stdio_format.md` F14 | LOW | `%n` non-standard length prefixes (e.g. MS `I64`) | `GAP-COMPILER` |
| NUMWIDE-11 | `AUDIT_numeric_wide.md` F11 | MEDIUM | format attributes ignored on Apple Clang | `GAP-COMPILER` — `WIDE_FORMAT_CONTRACT.md` |
| NUMWIDE-18 | `AUDIT_numeric_wide.md` F18 | LOW | latent duplicate `cplus_exp10` if toolchain has `__builtin_exp10` | `GAP-COMPILER` |
| NUMWIDE-21 | `AUDIT_numeric_wide.md` F21 | LOW | header uses `long` instead of `time_t` | `GAP-COMPILER` |
| NUMWIDE-07 | `AUDIT_numeric_wide.md` F07 | MEDIUM | 23 C23 math APIs decl-only (link-failing) | `BLOCKED` — `C23_MATH_UPSTREAM.md`; tests SKIP |
| NUMWIDE-08 | `AUDIT_numeric_wide.md` F08 | MEDIUM | `FE_TIESAWAY` absent | `BLOCKED` — `C23_SMALL_UPSTREAM.md` |
| NUMWIDE-09 | `AUDIT_numeric_wide.md` F09 | MEDIUM | `timespec_getres` absent | `BLOCKED` — `C23_SMALL_UPSTREAM.md` |
| NUMWIDE-10 | `AUDIT_numeric_wide.md` F10 | MEDIUM | `mbrtoc8`/`c8rtomb` absent | `BLOCKED` — `UCHARC23_UPSTREAM.md` |

Related cross-cutting themes from `WRAPPER_SAFETY_AUDIT.md` §3 items 9/10 (27 missing C23
APIs; Agent 1 ownership/lifetime contracts) are represented by the rows above and the
compiler-gap docs; no extra IDs were invented.

---

## 8. THEORETICAL / DOCUMENTED-CONTRACT concerns (53)

These are inherent, caller-owned, or host-delegated limitations. The audit documents them;
it does **not** claim they are fixed or that they are safe. Grouped by root cause:

| Group | Representative IDs | What it covers |
|---|---|---|
| Unknown-capacity raw wrappers (caller provenance) | MEMSTR-02..06, -11, -17..19, -31..39, -41, -43, -44 | `cplus_memcpy/memset/memcmp/strlen/strnlen/strdup/…` inherit caller obligations for termination/provenance; default unknown mode is unchecked |
| Direct `_s` / timing-safe tiers trust caller sizes | MEMSTR-49, MEMSTR-50 | no BOS resolution; `BOS_UNKNOWN` on timing-safe comparisons |
| Ctype/ASCII bit-mask definitions | MEMSTR-45, -46 (HIGH, inherent), -48, LIFE-19 | EOF/high-bit defined for correctly cast ints; pure bit masks; host FP/complex semantics |
| Bounded heuristics and classification | ALLOC-04, -13, -15, -16, -17..20 | lock-free host-call window; zero→1 byte; interior pointers; theoretical `uintptr_t` wrap / 64-bit gen wrap (no observation) |
| Reentrancy / host static state | MEMSTR-20, -21, -40; NUMWIDE-02, -03, -14, -15, -17, -19, -20 | host static buffers (time/ctype/strerror), global locale, hostile `struct tm`, huge width DoS, NaN/FP host semantics, divide-by-zero UB |
| Stdio documented divergences | STDIO-06, -11, -12, -13 | `fflush(stdin)` UB, ungated raw formats, truncation/NUL caller duty, Annex-K shape divergence |
| Lifecycle inherent / thread cancellation | LIFE-03 (HIGH: longjmp skips RAII), -20, -21 | recoverable-runtime limitation (`RUNTIME_RECOVERY.md`), cancellation delegated, signal-logging string lifetime |
| Injection / telemetry documented details | MEMSTR-22; LIFE-19 | process-global capacity mode (INFO); host-dependent FP/complex |

`LIFE-03` is HIGH severity but classified DOCUMENTED-CONTRACT (inherent longjmp/RAII
limitation), so it is reported here rather than in the CONFIRMED P0–P3 register.

---

## 9. Reproducer and verification index

Commands recorded in the sources (run from repo root; sanitizer caveats apply — see §11):

| Purpose | Command |
|---|---|
| Aggregate evidence suite | `sh tests/run_all_evidence.sh` |
| Allocation attack/reproducer suite | `sh tests/libc/run_attack_tests.sh` |
| Libc memory/string suite | `sh tests/libc/run_libc_tests.sh` |
| CRT checked-API suite (both libs) | `sh tests/crt/unit/run_unit_tests.sh` |
| CRT checked APIs | `sh tests/crt/run_all_crt.sh` |
| Consolidated HIGH regression suite | `build/audit_high/test_audit_high_{plain,ubsan}`; see `AUDIT_HIGH_REGRESSION.md` §Verification |
| CVE-like baseline vs safe (cases 01–15) | `sh tests/scenarios/cve_like/run_cve_like.sh` (10 passed, 0 failed recorded) |
| Deep stdlib regression | `CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/run_deep_tests.sh` (28×3 = 84 passed) |
| Compile-time safety suite | `sh tests/stdlib/compile_time/run_compile_time.sh` (ACCEPT ≠ enforcement) |

Recorded sanitizer-based baseline evidence for the CVE-like cases (NOTES_1..5):
heap-buffer-overflow (01, 05, 10, 12), negative-size memcpy (02), forged-pointer free (03),
UAF after realloc (04), double-free (07), UAF via stale alias (06, 08), stack-overflow VLA
(09), misaligned `uint64_t` store (11, UBSan), OOB jump-table read + SIGILL control-flow
hijack (13), TSan data race on reset/free (15). Safe variants for all these exit 0 with
clean sanitizer output under both `safe_c.h` and `-DPLAIN_LIBC` layers.

---

## 10. Final audit skeleton (scaffold for completion / sign-off)

The following sections are intentionally left as a structured scaffold. Each `[TBD]` is a
required evidence item before an overall pass/sign-off claim can be made.

1. **Scope & baseline** — DONE (§1–§2).
2. **Severity model & counts** — DONE (§3–§4).
3. **Top confirmed issues** — DONE (§5).
4. **Confirmed register** — DONE (§6).
5. **Unverified risk / theoretical separation** — DONE (§7–§8).
6. **Remaining verification (per group):**
   - P0 (ALLOC-01/02/03, MEMSTR-07/09): [TBD] implement generation-aware free/transfer,
     aligned-free, and bounded `strcpy`/`strcat` (or adopt `_s` tiers); re-run
     `run_attack_tests.sh` + `run_libc_tests.sh` with ASan/UBSan; [TBD] record exact exit codes.
   - P1 (10 HIGH fixed): [TBD] wire `test_audit_high.c` into `run_unit_tests.sh` per
     `AUDIT_HIGH_REGRESSION.md` §Warning 1; confirm 113/113 against current binaries.
   - P2/P3: [TBD] triage the 8 FIXED-MEDIUM regressions and assign owners for the 33 OPEN
     MEDIUM + 6 OPEN LOW.
   - UNVERIFIED RISK: [TBD] Agent 1 hand-off acceptance (`docs/patches/*`,
     `AGENT1_COMPILER_GAPS.md`) and upstream dependency-closure decisions for the 4 BLOCKED.
7. **Test gaps:** [TBD] resolve 27 compile-time capability/tagging gaps (PARTIAL ≠ pass) and
   126-of-381 stdlib APIs not exercised by deep tests (`TASK6_FINAL_REPORT.md` §9);
   ABI evidence is 2/714 matrix rows — layout-only, no calling convention / Windows x86 proof.
8. **Environment caveats to carry into sign-off:**
   - Apple clang 16 ASan runtime broken on host (`sanitizer_malloc_mac.inc:189`); sanitizer
     evidence uses Homebrew clang 23 with explicit SDK. Absence of a report is not a proof.
   - ASan/TSan/libFuzzer on this host require Linux CI; remote Linux CI and SonarCloud gates
     are **UNVERIFIED** (`STATUS.md`, `REMOTE_GATE_EVIDENCE.md`).
   - Build scripts default to a nonexistent `MacOSX27.sdk`; need `C_INCLUDE_PATH` to the
     available `MacOSX.sdk/usr/include` (symptom only, no source defect).
9. **Sign-off matrix (each row requires evidence before PASS):**
   - [ ] All 5 P0 fixed or formally accepted with documented caller-side mitigation.
   - [ ] All 10 P1 regression-guarded and re-verified against current binaries.
   - [ ] 11 UNVERIFIED RISK items have an owner/decision (implement, new toolchain, or accept).
   - [ ] 53 documented-contract items acknowledged as not safety claims.
   - [ ] Remote gates confirmed; no build/emulator result claimed as live success.
10. **Final statement:** [TBD] Insert explicit "no 100% safety claim" statement consistent with
    `WRAPPER_SAFETY_AUDIT.md` §5 and `TASK6_FINAL_REPORT.md` §10.

---

## 11. Honesty statement and limitations

- Every CONFIRMED row is sourced from an existing audit verdict, fix commit, or recorded
  test/sanitizer result. **No vulnerability was invented**, and no build/emulator result is
  presented as live-game or production success.
- FIXED entries require a named commit and an accepted test; P1 entries are additionally
  covered by `audit_high: 113/113` in plain and UBSan modes. The 46 OPEN entries explicitly
  have **no** fix commit in this branch.
- "PENDING" applies only to the three absent task-mentioned paths in §2; their claims are
  backed by the cited present sources, not by the missing documents.
- This is a synthesis/skeleton: it does not itself prove the current binaries are free of
  the OPEN findings, and UBSan passing is not a memory-safety proof. Final PASS remains
  gated on §10 item 9.
