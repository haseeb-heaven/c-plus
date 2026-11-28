# Audit Findings Status Tracker

- **Generated at:** 2026-11-22T21:42:12Z  
- **Branch / baseline:** `agent/modern-stdlib` / commit `595531f`  
- **Machine-readable source of truth:** [`docs/libc/audit_findings.json`](audit_findings.json) (schema_version `1`)  
- **Total findings:** 127 — reconciled exactly against the five family audits and `WRAPPER_SAFETY_AUDIT.md`.

## 1. How to read this tracker

Statuses are deliberately conservative. A finding is **FIXED** only when a named fix commit and an existing acceptance test were verified; a build or emulator result by itself is never enough. **DOCUMENTED-CONTRACT** means the source audit records an inherent/caller-owned limitation and the audit (or a cited evidence doc) is the evidence — it is *not* a safety claim. **GAP-COMPILER** and **BLOCKED** are external dependencies (Agent 1 compiler work and pinned-upstream/toolchain gaps). **OPEN** is the genuinely remaining runtime work.

| Status | Meaning |
|---|---|
| `FIXED` | A named fix commit and an existing acceptance test were verified; no FIXED entry relies on a build or emulator result alone. |
| `DOCUMENTED-CONTRACT` | Inherent/documented limitation or caller responsibility; the audit itself (or a cited evidence doc) is the evidence. Not a claim of safety or of a code fix. |
| `GAP-COMPILER` | Resolution requires an Agent 1 compiler/toolchain change; only a hand-off/gap document exists in this branch. |
| `BLOCKED` | Missing API with no dependency-closed pinned upstream source or toolchain support; SKIPs are retained, not fabricated implementations. |
| `OPEN` | Actionable runtime finding with no fix commit and no documentation-only resolution in the audited sources. |

## 2. Counts

| Status | Count |
|---|---:|
| `FIXED` | 17 |
| `DOCUMENTED-CONTRACT` | 53 |
| `GAP-COMPILER` | 7 |
| `BLOCKED` | 4 |
| `OPEN` | 46 |
| **Total** | **127** |

| Severity | Count |
|---|---:|
| `HIGH` | 16 |
| `MEDIUM` | 68 |
| `LOW` | 38 |
| `DOCUMENTED_LIMIT` | 4 |
| `INFO` | 1 |
| **Total** | **127** |

Severity aggregate: **HIGH 16 / MEDIUM 68 / other 43**, matching `WRAPPER_SAFETY_AUDIT.md` §2 (other = LOW 38 + DOCUMENTED_LIMIT 4 + INFO 1).

| Family | Findings | FIXED | DOCUMENTED-CONTRACT | GAP-COMPILER | BLOCKED | OPEN |
|---|---:|---:|---:|---:|---:|---:|
| ALLOC | 20 | 0 | 8 | 1 | 0 | 11 |
| MEMSTR | 50 | 6 | 30 | 0 | 0 | 14 |
| STDIO | 15 | 3 | 4 | 3 | 0 | 5 |
| NUMWIDE | 21 | 2 | 7 | 3 | 4 | 5 |
| LIFE | 21 | 6 | 4 | 0 | 0 | 11 |
| **Total** | **127** | **17** | **53** | **7** | **4** | **46** |

**Genuinely open: 46** findings. The 53 documented-contract rows are not counted as open, and neither are compiler gaps, blockers or fixed rows.

## 3. ID mapping and reconciliation

ALLOC F-01..F-20, STDIO F1..F15, NUMWIDE F01..F21 and LIFE F1..F21 are source-audit IDs copied verbatim. AUDIT_memory_string.md numbers only its top 5 issues (#1..#5) and otherwise identifies findings by function; MEMSTR-01..MEMSTR-50 are sequential IDs derived in section-2 document order, with the two section-2.7 tiers appended as MEMSTR-49/50. Every MEMSTR entry is marked id_mapping='derived'. source_id is populated only where the audit's numbered top issue maps to that row: #1 -> MEMSTR-47 (isblank), #2 -> MEMSTR-08 (strncpy), #3 -> MEMSTR-15 (memrchr), #4 -> MEMSTR-26 (memcmp_bounded sentinel), #5 -> MEMSTR-24/25 (bounded source capacity).

| Source audit | Source IDs | Tracker prefix | Mapping |
|---|---|---|---|
| `docs/libc/AUDIT_alloc.md` (20) | F-01 … F-20 | ALLOC | verbatim |
| `docs/libc/AUDIT_memory_string.md` (50) | #1 … #5 only | MEMSTR | **derived** |
| `docs/libc/AUDIT_stdio_format.md` (15) | F1 … F15 | STDIO | verbatim |
| `docs/libc/AUDIT_numeric_wide.md` (21) | F01 … F21 | NUMWIDE | verbatim |
| `docs/libc/AUDIT_lifecycle.md` (21) | F1 … F21 | LIFE | verbatim |

### Reconciliation notes / discrepancies

1. AUDIT_memory_string.md provides no individual finding IDs: only #1..#5 in section 5. All 50 MEMSTR IDs are derived; mapping type is recorded per finding.
2. AUDIT_memory_string.md section 4 severity narrative (6/27/16/1) is reconciled by rolling its two ctype Low buckets into MEMSTR-46/48 and counting the two section-2.7 tiers (direct _s, timing-safe) as the remaining Medium entries; the 50 section-2 rows plus 2 tiers minus 2 rolled-up ctype rows keep the family total at 50.
3. AUDIT_memory_string.md section 4 lists four cross-cutting findings (a)-(d) as not attributed to a single function; they are not separately counted here and are represented under the related per-function IDs (MEMSTR-01, -08, -22, -26).
4. cplus_isblank (MEMSTR-47) was fixed in b7d1686/5009b6b BEFORE the 595531f audit baseline but is still reported as High by the audit; FIXED is asserted on those commits plus tests/libc/test_ctype2.c.
5. AUDIT_memory_string.md #1 (isblank), #2 (strncpy family) and #3 (memchr/memrchr/stpncpy) are FIXED, but the High findings for cplus_strcpy (MEMSTR-07) and cplus_strcat (MEMSTR-09) were NOT changed by 5fe387d and remain OPEN; they are not counted as collateral fixes.
6. WRAPPER_SAFETY_AUDIT.md section 3 items 9 (27 missing C23 APIs) and 10 (Agent 1 compiler contracts) are cross-cutting themes, not extra numbered findings; they are represented by NUMWIDE-07..10/-11/-18/-21, ALLOC-07 and STDIO-07/-08/-14. No extra IDs were invented to force the total.
7. NUMWIDE-16 (logger errno) and LIFE-06 share the same log.c fix in 5fe387d; both are independently listed because both source audits numbered them.

## 4. Verified fix commits

| Commit | Description | Role |
|---|---|---|
| `5fe387d` | Fix HIGH/MEDIUM safety findings from systematic audit | post-baseline fix |
| `3131acf` | Add 15 deep stdlib regression tests and 46-case compile-time safety suite | post-baseline test/evidence |
| `d45681b` | Complete compile-time safety suite integration and remaining evidence | post-baseline regression/test integration |
| `b7d1686` | Fix cplus_isblank signed-char UB and land five family safety audits (pre-baseline) | pre-baseline fix (already in audit scope) |
| `5009b6b` | Add stdio.h include for EOF in isblank fix and test (pre-baseline) | pre-baseline fix (already in audit scope) |

HIGH regressions were verified in plain and UBSan modes (`docs/libc/AUDIT_HIGH_REGRESSION.md`: `audit_high: 113/113 checks ok`, 10/10 and 5/5 repeated runs; 92/92 under `LC_ALL=C` with locale-dependent skips).

## 5. Full finding register

### 5.1 Allocation wrappers — source: `docs/libc/AUDIT_alloc.md` (IDs verbatim)

| ID | Source ID | Finding | Severity | Status | Fix evidence | Acceptance test |
|---|---|---|---|---|---|---|
| `ALLOC-01` | F-01 | Checked free/transfer are generation-blind; stale alias frees/transfers the new live block | HIGH | `OPEN` | — — | `sh tests/libc/run_attack_tests.sh` |
| `ALLOC-02` | F-02 | Host free() of a tracked pointer leaves false-live metadata and permanent live_count/live_bytes inflation | HIGH | `OPEN` | — — | `sh tests/libc/run_attack_tests.sh` |
| `ALLOC-03` | F-03 | Two aligned allocators with opposite deallocators; cplus_aligned_alloc untracked; Windows _aligned_free; macro free hazard | HIGH | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `ALLOC-04` | F-04 | Host realloc runs with the metadata lock released; ABA guard protects bookkeeping only | DOCUMENTED_LIMIT | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_alloc.md | `sh tests/run_all_evidence.sh` |
| `ALLOC-05` | F-05 | Recent-free cache false positive: fresh host malloc at a just-freed cplus address refused as double free | MEDIUM | `OPEN` | — — | `sh tests/libc/run_attack_tests.sh` |
| `ALLOC-06` | F-06 | Recent-cache classification heuristic: libc first-slot overwrite when full; SafeCRT direct-mapped collisions | MEDIUM | `OPEN` | — — | `sh tests/libc/run_attack_tests.sh` |
| `ALLOC-07` | F-07 | No alloc_size attributes on exported wrappers; Agent 1 .len/extent lowering incomplete | MEDIUM | `GAP-COMPILER` | — docs/patches/agent1-len-managed-allocators.md | `sh tests/stdlib/compile_time/run_compile_time.sh  # see docs/libc/COMPILE_TIME_GAPS.md: ACCEPT is NOT compile-time enforcement` |
| `ALLOC-08` | F-08 | cplus_calloc overflow sets ENOMEM while calloc_s overflow sets EOVERFLOW | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `ALLOC-09` | F-09 | fail_after(negative) disables in libc layer but fails-all in SafeCRT layer | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `ALLOC-10` | F-10 | Injection state process-global with no libc-layer getter; g_seq counts decisions, not allocations | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `ALLOC-11` | F-11 | cplus_free discards refusal; cplus_free_checked nulls *inout even on refusal, dropping a live handle | MEDIUM | `OPEN` | — — | `sh tests/libc/run_attack_tests.sh` |
| `ALLOC-12` | F-12 | Bounded memcpy/memmove check only destination capacity; source capacity unchecked; unknown unchecked by default | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `ALLOC-13` | F-13 | Bounded string helpers call strlen before capacity validation; unterminated input is caller UB | DOCUMENTED_LIMIT | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_alloc.md | `sh tests/run_all_evidence.sh` |
| `ALLOC-14` | F-14 | aligned_alloc_s does not enforce C11 size % alignment == 0 unlike cplus_aligned_alloc | MEDIUM | `OPEN` | — — | `sh tests/crt/run_all_crt.sh` |
| `ALLOC-15` | F-15 | Zero size uniformly becomes a 1-byte tracked block; realloc(p,0) does not free | DOCUMENTED_LIMIT | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_alloc.md | `sh tests/libc/run_attack_tests.sh` |
| `ALLOC-16` | F-16 | Interior pointers: remaining_capacity supports them but liveness/size/meta APIs report non-live | DOCUMENTED_LIMIT | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_alloc.md | `sh tests/run_all_evidence.sh` |
| `ALLOC-17` | F-17 | g_live/g_live_bytes are size_t; a future bookkeeping error would wrap silently (none observed) | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_alloc.md | `sh tests/run_all_evidence.sh` |
| `ALLOC-18` | F-18 | 64-bit generation counter can wrap to 0 (0 treated as invalid); alias correlation is caller duty | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_alloc.md | `sh tests/run_all_evidence.sh` |
| `ALLOC-19` | F-19 | remaining_capacity uses base+byte_size (theoretical uintptr_t wrap) and an O(g_cap) linear scan | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_alloc.md | `sh tests/run_all_evidence.sh` |
| `ALLOC-20` | F-20 | cplus_aligned_alloc has no RSIZE_MAX_MEM cap and zero size is host-defined | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_alloc.md | `sh tests/run_all_evidence.sh` |

### 5.2 Memory/string/ctype wrappers — source: `docs/libc/AUDIT_memory_string.md` (**IDs derived**)

| ID | Source ID | Finding | Severity | Status | Fix evidence | Acceptance test |
|---|---|---|---|---|---|---|
| `MEMSTR-01` | — | cplus_memcpy has no capacity/source bound and no overlap detection | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-02` | — | cplus_memmove has no capacity/source bound; provenance is caller duty | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-03` | — | cplus_memset has no capacity check; provenance is caller duty | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-04` | — | cplus_memcmp has no capacity/source bound; provenance is caller duty | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-05` | — | cplus_strlen has no bound; unterminated source is caller UB | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-06` | — | cplus_strnlen bounds by n; provenance only | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-07` | — | cplus_strcpy has no capacity at all and writes no NUL on refusal | HIGH | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-08` | #2 | cplus_strncpy huge-n zero-fill overflow and no NUL guarantee | HIGH | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/libc/run_libc_tests.sh && sh tests/crt/unit/run_unit_tests.sh  # test_string_extra*.c + test_audit_high.c` |
| `MEMSTR-09` | — | cplus_strcat has no capacity; scans dest then copies unbounded | HIGH | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-10` | — | cplus_strncat does not check destination capacity | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-11` | — | cplus_strcmp/cplus_strncmp have no capacity; provenance only | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-12` | — | cplus_memchr had no NULL guard and no max-size clamp on n | MEDIUM | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/libc/run_libc_tests.sh && sh tests/crt/unit/run_unit_tests.sh  # test_string_extra*.c + test_audit_high.c` |
| `MEMSTR-13` | — | cplus_strchr/cplus_strrchr have no NULL guard; EOF high-bit truncation | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-14` | — | cplus_strstr has no NULL guard and no bound | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-15` | #3 | cplus_memrchr had huge-n OOB first read and no NULL guard | HIGH | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/libc/run_libc_tests.sh && sh tests/crt/unit/run_unit_tests.sh  # test_string_extra*.c + test_audit_high.c` |
| `MEMSTR-16` | — | cplus_strcasecmp/cplus_strncasecmp have no NULL guard; ASCII-only folding | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-17` | — | cplus_strdup provenance/termination of source is caller responsibility | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-18` | — | cplus_strspn/cplus_strcspn/cplus_strpbrk have no explicit bounds | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-19` | — | cplus_strndup provenance only; source bound by n | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-20` | — | cplus_strtok delegates to host with hidden static state (not reentrant) | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-21` | — | cplus_strerror returns host-owned static buffer; validity host-defined | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-22` | — | cplus_libc_set_unknown_capacity_mode is process-global; default mode is unchecked | INFO | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-23` | — | cplus_memset_bounded checks destination capacity only; provenance is caller duty | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-24` | #5 | cplus_memcpy_bounded never checks source capacity and has no overlap check | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-25` | #5 | cplus_memmove_bounded never checks source capacity; default unknown is unchecked | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-26` | #4 | cplus_memcmp_bounded refusal returns 0, indistinguishable from equality | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-27` | — | cplus_strcpy_bounded calls strlen on (possibly unterminated) source before capacity check | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-28` | — | cplus_strncpy_bounded huge-n default-mode pass-through to raw zero-fill loop | HIGH | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/libc/run_libc_tests.sh && sh tests/crt/unit/run_unit_tests.sh  # test_string_extra*.c + test_audit_high.c` |
| `MEMSTR-29` | — | cplus_strcat_bounded calls strlen on unterminated dest/source; no source bound | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-30` | — | cplus_strncat_bounded unterminated dest/source OOB scans | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-31` | — | cplus_memccpy inherits musl valid-pointer/sufficient-space precondition | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-32` | — | cplus_stpcpy has no NULL guard and no destination capacity | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-33` | — | cplus_stpncpy had huge-n tail memset and no NULL guard | MEDIUM | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/libc/run_libc_tests.sh && sh tests/crt/unit/run_unit_tests.sh  # test_string_extra*.c + test_audit_high.c` |
| `MEMSTR-34` | — | cplus_strtok_r assumes valid s/sep/p per musl contract | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-35` | — | cplus_strsep assumes valid *str/sep and terminated string | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-36` | — | cplus_memmem BOS checks depend on caller-supplied k/l correctness | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-37` | — | cplus_strverscmp assumes terminated strings (musl contract) | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-38` | — | cplus_strcasestr unbounded strlen/strncasecmp on source | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-39` | — | cplus_strchrnul uses host strlen; unterminated source is caller UB | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-40` | — | cplus_strsignal static buffer/thread-safety are host/caller concerns | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-41` | — | cplus_strcoll/cplus_strxfrm do not validate strxfrm destination capacity | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-42` | — | cplus_swab signed ssize_t n can be negative; size validity is caller duty | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-43` | — | cplus_strlcpy/cplus_strlcat are bounded by size but require terminated source | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-44` | — | cplus_strnstr requires valid haystack/needle for len | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-45` | — | cplus_memset_explicit has two mutually exclusive definitions; anti-optimisation only | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-46` | — | Correctly-cast ctype classifiers plus toupper/tolower are defined for EOF/high-bit | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh  # test_ctype2.c: EOF/0x80/0xFF regressions` |
| `MEMSTR-47` | #1 | cplus_isblank passed an uncast int to isblank (signed-char UB) | HIGH | `FIXED` | `b7d1686`, `5009b6b` docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh  # test_ctype2.c: EOF/0x80/0xFF regressions` |
| `MEMSTR-48` | — | cplus_isascii/cplus_toascii are pure bit masks defined for every int | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh  # test_ctype2.c: EOF/0x80/0xFF regressions` |
| `MEMSTR-49` | — | Direct _s entry points trust caller-supplied dmax/smax (no BOS resolution) | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/libc/run_libc_tests.sh` |
| `MEMSTR-50` | — | Timing-safe comparison wrappers pass BOS_UNKNOWN; no object-size check | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_memory_string.md | `sh tests/crt/run_all_crt.sh` |

### 5.3 Stdio/format wrappers — source: `docs/libc/AUDIT_stdio_format.md` (IDs verbatim)

| ID | Source ID | Finding | Severity | Status | Fix evidence | Acceptance test |
|---|---|---|---|---|---|---|
| `STDIO-01` | F1 | cplus_fclose_s double-close UAF: probe-chain deletion without tombstones | HIGH | `FIXED` | `5fe387d`, `d45681b` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `STDIO-02` | F2 | Plain cplus_fclose false rejection/fd leak and cross-registry bypass | HIGH | `FIXED` | `5fe387d`, `d45681b` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `STDIO-03` | F3 | Bounded/evicted double-close caches (256-ring, 512-slot tables) | HIGH | `FIXED` | `5fe387d`, `d45681b` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `STDIO-04` | F4 | stdio2.c/stdio3.c unvalidated delegates (NULL stream UB) | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `STDIO-05` | F5 | fread_s/fwrite_s trust bmax and silently clamp without errno | MEDIUM | `OPEN` | — — | `sh tests/crt/run_all_crt.sh` |
| `STDIO-06` | F6 | fflush(stdin) undefined and update-stream sequencing not enforced (caller) | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_stdio_format.md | `sh tests/libc/run_libc_tests.sh` |
| `STDIO-07` | F7 | Checked-format literal gate reliability; v*/direct-call bypass and toolchain-dependent detection | MEDIUM | `GAP-COMPILER` | — docs/libc/COMPILE_TIME_GAPS.md | `sh tests/stdlib/compile_time/run_compile_time.sh  # see docs/libc/COMPILE_TIME_GAPS.md: ACCEPT is NOT compile-time enforcement` |
| `STDIO-08` | F8 | scanf_s Annex-K divergence (no rsize_t argument, width must be literal) | MEDIUM | `GAP-COMPILER` | — docs/libc/AUDIT_stdio_format.md | `sh tests/stdlib/compile_time/run_compile_time.sh  # see docs/libc/COMPILE_TIME_GAPS.md: ACCEPT is NOT compile-time enforcement` |
| `STDIO-09` | F9 | freopen/fdopen do not reconcile the stream registry | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `STDIO-10` | F10 | Default _exit(70) trap on a literal-gate false positive; temp-file cleanup not guaranteed | MEDIUM | `OPEN` | — — | `sh tests/crt/run_all_crt.sh` |
| `STDIO-11` | F11 | Raw format wrappers ungated by design | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_stdio_format.md | `sh tests/libc/run_libc_tests.sh` |
| `STDIO-12` | F12 | Truncation/NUL termination remain caller duties | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_stdio_format.md | `sh tests/libc/run_libc_tests.sh` |
| `STDIO-13` | F13 | sprintf_s/fopen_s API-shape divergence from Annex K (documented) | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_stdio_format.md | `sh tests/libc/run_libc_tests.sh` |
| `STDIO-14` | F14 | %n parser does not recognise non-standard length prefixes (e.g. MS I64) | LOW | `GAP-COMPILER` | — docs/libc/AUDIT_stdio_format.md | `sh tests/stdlib/compile_time/run_compile_time.sh  # see docs/libc/COMPILE_TIME_GAPS.md: ACCEPT is NOT compile-time enforcement` |
| `STDIO-15` | F15 | fmt_check_dest_bounds pointer-into-object arithmetic can overstate capacity | LOW | `OPEN` | — — | `sh tests/crt/run_all_crt.sh` |

### 5.4 Math/time/wide wrappers — source: `docs/libc/AUDIT_numeric_wide.md` (IDs verbatim)

| ID | Source ID | Finding | Severity | Status | Fix evidence | Acceptance test |
|---|---|---|---|---|---|---|
| `NUMWIDE-01` | F01 | Wide format %n pass-through with no effective compile-time format checking | HIGH | `FIXED` | `5fe387d` docs/libc/WIDE_FORMAT_CONTRACT.md | `sh tests/libc/run_libc_tests.sh  # test_wchar_format.c Tier-2 %n refusals + test_audit_high.c` |
| `NUMWIDE-02` | F02 | Non-reentrant static-buffer time APIs (gmtime/localtime/asctime/ctime/ecvt family) | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_numeric_wide.md | `sh tests/libc/run_libc_tests.sh` |
| `NUMWIDE-03` | F03 | setlocale/localeconv global mutable state; static lconv | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_numeric_wide.md | `sh tests/libc/run_libc_tests.sh` |
| `NUMWIDE-04` | F04 | cplus_exp10l returns 0.0L on non-binary64 long double ABIs | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `NUMWIDE-05` | F05 | abs/labs/llabs/imaxabs negate the most-negative value (UB) | MEDIUM | `OPEN` | — — | `sh tests/crt/run_all_crt.sh` |
| `NUMWIDE-06` | F06 | atoi/atol silently truncate overflow without ERANGE | MEDIUM | `OPEN` | — — | `sh tests/crt/run_all_crt.sh` |
| `NUMWIDE-07` | F07 | 23 C23 math APIs are DECL-ONLY (declared but link-failing) | MEDIUM | `BLOCKED` | — docs/libc/C23_MATH_UPSTREAM.md | `sh tests/libc/run_libc_tests.sh  # test_math_gaps.c / test_math_c23_upstream.c / test_c23_small_gaps.c: SKIPs are expected until a backend lands` |
| `NUMWIDE-08` | F08 | FE_TIESAWAY absent (compiler macro; no host/upstream value) | MEDIUM | `BLOCKED` | — docs/libc/C23_SMALL_UPSTREAM.md | `sh tests/libc/run_libc_tests.sh  # test_math_gaps.c / test_math_c23_upstream.c / test_c23_small_gaps.c: SKIPs are expected until a backend lands` |
| `NUMWIDE-09` | F09 | timespec_getres absent (no pinned upstream source) | MEDIUM | `BLOCKED` | — docs/libc/C23_SMALL_UPSTREAM.md | `sh tests/libc/run_libc_tests.sh  # test_math_gaps.c / test_math_c23_upstream.c / test_c23_small_gaps.c: SKIPs are expected until a backend lands` |
| `NUMWIDE-10` | F10 | mbrtoc8/c8rtomb absent (no pinned musl source) | MEDIUM | `BLOCKED` | — docs/libc/UCHARC23_UPSTREAM.md | `sh tests/libc/run_libc_tests.sh  # test_math_gaps.c / test_math_c23_upstream.c / test_c23_small_gaps.c: SKIPs are expected until a backend lands` |
| `NUMWIDE-11` | F11 | format(wprintf/wscanf) attributes ignored on Apple Clang; diagnostics suppressed | MEDIUM | `GAP-COMPILER` | — docs/libc/WIDE_FORMAT_CONTRACT.md | `sh tests/libc/run_libc_tests.sh  # test_wchar_format.c Tier-2 %n refusals + test_audit_high.c` |
| `NUMWIDE-12` | F12 | strto*/atof lack the NULL nptr guard present in atoi/strtoimax | MEDIUM | `OPEN` | — — | `sh tests/crt/run_all_crt.sh` |
| `NUMWIDE-13` | F13 | NULL tp/ts/req and NULL tm to time/strftime/wcsftime are not guarded | MEDIUM | `OPEN` | — — | `sh tests/libc/run_libc_tests.sh` |
| `NUMWIDE-14` | F14 | Hostile struct tm values delegated unchecked (caller UB) | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_numeric_wide.md | `sh tests/libc/run_libc_tests.sh` |
| `NUMWIDE-15` | F15 | Unbounded wide-string operations have no capacity parameter/guard | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_numeric_wide.md | `sh tests/libc/run_libc_tests.sh` |
| `NUMWIDE-16` | F16 | Logger write(2) could clobber errno when enabled and stderr fails | LOW | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `NUMWIDE-17` | F17 | NaN payload / FP-exception / signed-zero semantics are host-specific | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_numeric_wide.md | `sh tests/libc/run_libc_tests.sh` |
| `NUMWIDE-18` | F18 | Latent duplicate cplus_exp10 definition if a toolchain provides __builtin_exp10 | LOW | `GAP-COMPILER` | — docs/libc/AUDIT_numeric_wide.md | `sh tests/libc/run_libc_tests.sh  # test_math_gaps.c / test_math_c23_upstream.c / test_c23_small_gaps.c: SKIPs are expected until a backend lands` |
| `NUMWIDE-19` | F19 | Huge width/precision wide formats permit CPU/memory DoS | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/WIDE_FORMAT_CONTRACT.md | `sh tests/libc/run_libc_tests.sh  # test_wchar_format.c Tier-2 %n refusals + test_audit_high.c` |
| `NUMWIDE-20` | F20 | div/ldiv/lldiv/imaxdiv divide-by-zero and wcstok state misuse are UB | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_numeric_wide.md | `sh tests/crt/run_all_crt.sh` |
| `NUMWIDE-21` | F21 | Public header uses long instead of time_t for cplus_time/cplus_gmtime | LOW | `GAP-COMPILER` | — docs/libc/AUDIT_numeric_wide.md | `sh tests/stdlib/compile_time/run_compile_time.sh  # see docs/libc/COMPILE_TIME_GAPS.md: ACCEPT is NOT compile-time enforcement` |

### 5.5 Lifecycle/concurrency/logging — source: `docs/libc/AUDIT_lifecycle.md` (IDs verbatim)

| ID | Source ID | Finding | Severity | Status | Fix evidence | Acceptance test |
|---|---|---|---|---|---|---|
| `LIFE-01` | F1 | cplus_thrd_create detached-thread start context tied to caller stack | HIGH | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `LIFE-02` | F2 | uchar used 4-byte unsigned where host requires 128-byte mbstate_t | HIGH | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `LIFE-03` | F3 | longjmp recovery cannot run C++ destructors/RAII (documented, inherent) | HIGH | `DOCUMENTED-CONTRACT` | — docs/libc/RUNTIME_RECOVERY.md | `sh tests/run_all_evidence.sh` |
| `LIFE-04` | F4 | Signal wrappers passed NULL sigset_t* straight to host | MEDIUM | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `LIFE-05` | F5 | fenv NULL hardening inconsistent across exports | MEDIUM | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `LIFE-06` | F6 | cplus_libc_log could clobber errno, contradicting its guarantee | MEDIUM | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `LIFE-07` | F7 | Logger ignored short writes, EINTR and SIGPIPE | MEDIUM | `FIXED` | `5fe387d` docs/libc/AUDIT_HIGH_REGRESSION.md | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `LIFE-08` | F8 | safe_runtime control state racy under C11/C++11 memory model | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-09` | F9 | Cleanup registry TLS not unwound on thrd_exit/pthread_exit inside a guard | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-10` | F10 | Unbounded recursion between report/violation and the user handler | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-11` | F11 | safe_log/safe_stats not async-signal-safe; logger recursion can deadlock | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-12` | F12 | mtx_timedlock fallback polling loop with unenforced preconditions | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-13` | F13 | ONCE_FLAG_INIT hardcodes a platform ABI signature | MEDIUM | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-14` | F14 | errno plumbing incomplete/dead (__cplus_errno_location macro absent) | LOW | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-15` | F15 | Adversarial logging test and notes stale relative to hardened logger | LOW | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-16` | F16 | Telemetry fixed 64-slot cap and racy reset | LOW | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-17` | F17 | TSS destructor limits and call_once recursion delegated without checks | LOW | `OPEN` | — — | `sh tests/run_all_evidence.sh` |
| `LIFE-18` | F18 | uchar internal state process-wide when ps == NULL | LOW | `OPEN` | — — | `sh tests/crt/unit/run_unit_tests.sh  # builds both libs; runs test_audit_high.c (113/113) + test_fclose_s.c` |
| `LIFE-19` | F19 | Complex/fenv wrappers rely entirely on host semantics for hostile inputs | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_lifecycle.md | `sh tests/run_all_evidence.sh` |
| `LIFE-20` | F20 | Thread cancellation unhandled across pthread wrappers | MEDIUM | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_lifecycle.md | `sh tests/run_all_evidence.sh` |
| `LIFE-21` | F21 | Signal-safe logging reads caller strings without lifetime guarantees | LOW | `DOCUMENTED-CONTRACT` | — docs/libc/AUDIT_lifecycle.md | `sh tests/run_all_evidence.sh` |

## 6. Genuinely open work (priority order)

| ID | Severity | Finding |
|---|---|---|
| `ALLOC-01` | HIGH | Checked free/transfer are generation-blind; stale alias frees/transfers the new live block |
| `ALLOC-02` | HIGH | Host free() of a tracked pointer leaves false-live metadata and permanent live_count/live_bytes inflation |
| `ALLOC-03` | HIGH | Two aligned allocators with opposite deallocators; cplus_aligned_alloc untracked; Windows _aligned_free; macro free hazard |
| `ALLOC-05` | MEDIUM | Recent-free cache false positive: fresh host malloc at a just-freed cplus address refused as double free |
| `ALLOC-06` | MEDIUM | Recent-cache classification heuristic: libc first-slot overwrite when full; SafeCRT direct-mapped collisions |
| `ALLOC-08` | MEDIUM | cplus_calloc overflow sets ENOMEM while calloc_s overflow sets EOVERFLOW |
| `ALLOC-09` | MEDIUM | fail_after(negative) disables in libc layer but fails-all in SafeCRT layer |
| `ALLOC-10` | MEDIUM | Injection state process-global with no libc-layer getter; g_seq counts decisions, not allocations |
| `ALLOC-11` | MEDIUM | cplus_free discards refusal; cplus_free_checked nulls *inout even on refusal, dropping a live handle |
| `ALLOC-12` | MEDIUM | Bounded memcpy/memmove check only destination capacity; source capacity unchecked; unknown unchecked by default |
| `ALLOC-14` | MEDIUM | aligned_alloc_s does not enforce C11 size % alignment == 0 unlike cplus_aligned_alloc |
| `MEMSTR-01` | MEDIUM | cplus_memcpy has no capacity/source bound and no overlap detection |
| `MEMSTR-07` | HIGH | cplus_strcpy has no capacity at all and writes no NUL on refusal |
| `MEMSTR-09` | HIGH | cplus_strcat has no capacity; scans dest then copies unbounded |
| `MEMSTR-10` | MEDIUM | cplus_strncat does not check destination capacity |
| `MEMSTR-13` | MEDIUM | cplus_strchr/cplus_strrchr have no NULL guard; EOF high-bit truncation |
| `MEMSTR-14` | MEDIUM | cplus_strstr has no NULL guard and no bound |
| `MEMSTR-16` | MEDIUM | cplus_strcasecmp/cplus_strncasecmp have no NULL guard; ASCII-only folding |
| `MEMSTR-24` | MEDIUM | cplus_memcpy_bounded never checks source capacity and has no overlap check |
| `MEMSTR-25` | MEDIUM | cplus_memmove_bounded never checks source capacity; default unknown is unchecked |
| `MEMSTR-26` | MEDIUM | cplus_memcmp_bounded refusal returns 0, indistinguishable from equality |
| `MEMSTR-27` | MEDIUM | cplus_strcpy_bounded calls strlen on (possibly unterminated) source before capacity check |
| `MEMSTR-29` | MEDIUM | cplus_strcat_bounded calls strlen on unterminated dest/source; no source bound |
| `MEMSTR-30` | MEDIUM | cplus_strncat_bounded unterminated dest/source OOB scans |
| `MEMSTR-42` | MEDIUM | cplus_swab signed ssize_t n can be negative; size validity is caller duty |
| `STDIO-04` | MEDIUM | stdio2.c/stdio3.c unvalidated delegates (NULL stream UB) |
| `STDIO-05` | MEDIUM | fread_s/fwrite_s trust bmax and silently clamp without errno |
| `STDIO-09` | MEDIUM | freopen/fdopen do not reconcile the stream registry |
| `STDIO-10` | MEDIUM | Default _exit(70) trap on a literal-gate false positive; temp-file cleanup not guaranteed |
| `STDIO-15` | LOW | fmt_check_dest_bounds pointer-into-object arithmetic can overstate capacity |
| `NUMWIDE-04` | MEDIUM | cplus_exp10l returns 0.0L on non-binary64 long double ABIs |
| `NUMWIDE-05` | MEDIUM | abs/labs/llabs/imaxabs negate the most-negative value (UB) |
| `NUMWIDE-06` | MEDIUM | atoi/atol silently truncate overflow without ERANGE |
| `NUMWIDE-12` | MEDIUM | strto*/atof lack the NULL nptr guard present in atoi/strtoimax |
| `NUMWIDE-13` | MEDIUM | NULL tp/ts/req and NULL tm to time/strftime/wcsftime are not guarded |
| `LIFE-08` | MEDIUM | safe_runtime control state racy under C11/C++11 memory model |
| `LIFE-09` | MEDIUM | Cleanup registry TLS not unwound on thrd_exit/pthread_exit inside a guard |
| `LIFE-10` | MEDIUM | Unbounded recursion between report/violation and the user handler |
| `LIFE-11` | MEDIUM | safe_log/safe_stats not async-signal-safe; logger recursion can deadlock |
| `LIFE-12` | MEDIUM | mtx_timedlock fallback polling loop with unenforced preconditions |
| `LIFE-13` | MEDIUM | ONCE_FLAG_INIT hardcodes a platform ABI signature |
| `LIFE-14` | LOW | errno plumbing incomplete/dead (__cplus_errno_location macro absent) |
| `LIFE-15` | LOW | Adversarial logging test and notes stale relative to hardened logger |
| `LIFE-16` | LOW | Telemetry fixed 64-slot cap and racy reset |
| `LIFE-17` | LOW | TSS destructor limits and call_once recursion delegated without checks |
| `LIFE-18` | LOW | uchar internal state process-wide when ps == NULL |

## 7. Verification policy

- Run `sh tests/run_all_evidence.sh` for the aggregate suite; each suite writes its own log under `build/`.
- Test DEFAULT and STRICT modes with sanitizers disabled first; sanitizers are additional evidence, never the sole safety mechanism.
- Fatal cases run in subprocesses with timeouts; exact exit codes are recorded.
- Allocation failures use `stdlib_alloc_fail_at/after/reset`, never memory exhaustion.
- Each fix group must update the relevant contract/limitation doc in the same change; no 100% claim from a passing harness alone.
