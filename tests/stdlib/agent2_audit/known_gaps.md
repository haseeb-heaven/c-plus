# Known gaps — Agent 2 audit (2026-11-23)

> Point-in-time evidence inventory from branch `agent/modern-stdlib`. No test runner was executed for this document. Every entry names an evidence path (a repository document or a reproducer path) and a responsible component. **No gap is claimed fixed without a named fix commit plus an existing acceptance test as recorded in the cited source.** Statuses from `AUDIT_FINDINGS_TRACKER.md` are the 2026-11-22 baseline at commit `595531f`; this audit found no later evidence and therefore converts no row.

## 0. Honesty rules and headline counts

- `OPEN` means actionable runtime finding with no fix commit (46 rows). `GAP-COMPILER` needs Agent 1 (7). `BLOCKED` has no pinned upstream/toolchain (4). `DOCUMENTED-CONTRACT` (53) is an inherent/caller-owned limitation, **not** a fix and not counted as open. `FIXED` (17) is asserted only on the tracker's verified commits/tests.
- Percentages are intentionally omitted; all figures are exact counts with an explicit denominator from the named source. Two conflicting snapshots are kept side by side, not averaged or reconciled by assumption.

| Headline count | Denominator / source |
|---|---|
| 127 wrapper-safety findings: 46 OPEN, 7 GAP-COMPILER, 4 BLOCKED, 53 DOCUMENTED-CONTRACT, 17 FIXED | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §2, §5; baseline commit `595531f` |
| 16 HIGH / 68 MEDIUM / 43 other (38 LOW + 4 DOCUMENTED_LIMIT + 1 INFO) | `AUDIT_FINDINGS_TRACKER.md` §2 |
| 948 ISO C APIs across 27 headers: 359 HOST/COMPILER-PROVIDED, 476 PLATFORM_BACKED, 29 IMPLEMENTED_BUNDLED, 8 IMPLEMENTED_UPSTREAM, 27 MISSING, 49 UNSUPPORTED | `docs/libc/iso_c_inventory.json` `counts` (per-API records; its `counts.by_header` summary disagrees — contradiction C1 in §e) |
| 714 matrix APIs: 606 TESTED, 73 RUNTIME-ENFORCED, 10 COMPILER-ENFORCED, 12 IMPLEMENTED, 13 UNSUPPORTED, 0 PLANNED | `docs/libc/api_status_matrix.json` `counts.by_status` |
| ABI evidence 2 of 714 (712 NO EVIDENCE); contract 345 of 714 (369 none); logging 69 of 714 (645 none); boundary 508; fuzz/differential 308 | `api_status_matrix.json` `counts.evidence_coverage` |
| Compile-time: 52 cases in 8 manifests → 5 tagged `[CPLUS-` REJECT, 7 untagged PARTIAL, 40 compiles-clean; 27 declared capability/tagging gaps | `docs/stdlib/TASK6_FINAL_REPORT.md` §4; `docs/libc/COMPILE_TIME_GAPS.md` addendum |
| Deep static inventory: 126 of 381 entries have no deep-test textual reference (250/131 under a code-only variant) | `docs/stdlib/REGRESSION_INVENTORY.md` §§3–6; `tests/stdlib/agent2_audit/public_api_inventory.md` |
| SafeCRT: 387 of 437 distinct vendored public APIs NOT_INTEGRATED (40 INTEGRATED, 10 ADAPTER_ONLY, 0 UNAVAILABLE); 188 upstream `.c` not compiled | `docs/libc/IMPLEMENTATION_PLAN.md` §1.8; `docs/libc/COVERAGE.md` |

## (a) Library defects still open — 46 findings

Each row is `OPEN` in the tracker with no fix evidence recorded. The acceptance command is the tracker's own verification path (not re-run here). Five are HIGH: ALLOC-01/02/03 and MEMSTR-07/09.

| ID | Severity | Finding | Evidence path | Responsible component |
|---|---|---|---|---|
| `ALLOC-01` | HIGH | Checked free/transfer are generation-blind; stale alias frees/transfers the new live block | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-01`; acceptance: `sh tests/libc/run_attack_tests.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-02` | HIGH | Host free() of a tracked pointer leaves false-live metadata and permanent live_count/live_bytes inflation | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-02`; acceptance: `sh tests/libc/run_attack_tests.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-03` | HIGH | Two aligned allocators with opposite deallocators; cplus_aligned_alloc untracked; Windows _aligned_free; macro free hazard | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-03`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-05` | MEDIUM | Recent-free cache false positive: fresh host malloc at a just-freed cplus address refused as double free | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-05`; acceptance: `sh tests/libc/run_attack_tests.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-06` | MEDIUM | Recent-cache classification heuristic: libc first-slot overwrite when full; SafeCRT direct-mapped collisions | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-06`; acceptance: `sh tests/libc/run_attack_tests.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-08` | MEDIUM | cplus_calloc overflow sets ENOMEM while calloc_s overflow sets EOVERFLOW | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-08`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-09` | MEDIUM | fail_after(negative) disables in libc layer but fails-all in SafeCRT layer | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-09`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-10` | MEDIUM | Injection state process-global with no libc-layer getter; g_seq counts decisions, not allocations | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-10`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-11` | MEDIUM | cplus_free discards refusal; cplus_free_checked nulls *inout even on refusal, dropping a live handle | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-11`; acceptance: `sh tests/libc/run_attack_tests.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-12` | MEDIUM | Bounded memcpy/memmove check only destination capacity; source capacity unchecked; unknown unchecked by default | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-12`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `ALLOC-14` | MEDIUM | aligned_alloc_s does not enforce C11 size % alignment == 0 unlike cplus_aligned_alloc | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.1 row `ALLOC-14`; acceptance: `sh tests/crt/run_all_crt.sh` | A2-runtime (`libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`) |
| `MEMSTR-01` | MEDIUM | cplus_memcpy has no capacity/source bound and no overlap detection | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-01`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-07` | HIGH | cplus_strcpy has no capacity at all and writes no NUL on refusal | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-07`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-09` | HIGH | cplus_strcat has no capacity; scans dest then copies unbounded | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-09`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-10` | MEDIUM | cplus_strncat does not check destination capacity | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-10`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-13` | MEDIUM | cplus_strchr/cplus_strrchr have no NULL guard; EOF high-bit truncation | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-13`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-14` | MEDIUM | cplus_strstr has no NULL guard and no bound | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-14`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-16` | MEDIUM | cplus_strcasecmp/cplus_strncasecmp have no NULL guard; ASCII-only folding | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-16`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-24` | MEDIUM | cplus_memcpy_bounded never checks source capacity and has no overlap check | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-24`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-25` | MEDIUM | cplus_memmove_bounded never checks source capacity; default unknown is unchecked | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-25`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-26` | MEDIUM | cplus_memcmp_bounded refusal returns 0, indistinguishable from equality | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-26`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-27` | MEDIUM | cplus_strcpy_bounded calls strlen on (possibly unterminated) source before capacity check | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-27`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-29` | MEDIUM | cplus_strcat_bounded calls strlen on unterminated dest/source; no source bound | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-29`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-30` | MEDIUM | cplus_strncat_bounded unterminated dest/source OOB scans | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-30`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `MEMSTR-42` | MEDIUM | cplus_swab signed ssize_t n can be negative; size validity is caller duty | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.2 row `MEMSTR-42`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/string/*`, `stdlib/safe/src/safe_memory.c`) |
| `STDIO-04` | MEDIUM | stdio2.c/stdio3.c unvalidated delegates (NULL stream UB) | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.3 row `STDIO-04`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/stdio/*`, `stdlib/safe/src/safe_io.c`, `safe_format.c`) |
| `STDIO-05` | MEDIUM | fread_s/fwrite_s trust bmax and silently clamp without errno | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.3 row `STDIO-05`; acceptance: `sh tests/crt/run_all_crt.sh` | A2-runtime (`libc/src/stdio/*`, `stdlib/safe/src/safe_io.c`, `safe_format.c`) |
| `STDIO-09` | MEDIUM | freopen/fdopen do not reconcile the stream registry | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.3 row `STDIO-09`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/stdio/*`, `stdlib/safe/src/safe_io.c`, `safe_format.c`) |
| `STDIO-10` | MEDIUM | Default _exit(70) trap on a literal-gate false positive; temp-file cleanup not guaranteed | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.3 row `STDIO-10`; acceptance: `sh tests/crt/run_all_crt.sh` | A2-runtime (`libc/src/stdio/*`, `stdlib/safe/src/safe_io.c`, `safe_format.c`) |
| `STDIO-15` | LOW | fmt_check_dest_bounds pointer-into-object arithmetic can overstate capacity | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.3 row `STDIO-15`; acceptance: `sh tests/crt/run_all_crt.sh` | A2-runtime (`libc/src/stdio/*`, `stdlib/safe/src/safe_io.c`, `safe_format.c`) |
| `NUMWIDE-04` | MEDIUM | cplus_exp10l returns 0.0L on non-binary64 long double ABIs | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.4 row `NUMWIDE-04`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/{math,time,stdlib,wchar}/*`) |
| `NUMWIDE-05` | MEDIUM | abs/labs/llabs/imaxabs negate the most-negative value (UB) | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.4 row `NUMWIDE-05`; acceptance: `sh tests/crt/run_all_crt.sh` | A2-runtime (`libc/src/{math,time,stdlib,wchar}/*`) |
| `NUMWIDE-06` | MEDIUM | atoi/atol silently truncate overflow without ERANGE | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.4 row `NUMWIDE-06`; acceptance: `sh tests/crt/run_all_crt.sh` | A2-runtime (`libc/src/{math,time,stdlib,wchar}/*`) |
| `NUMWIDE-12` | MEDIUM | strto*/atof lack the NULL nptr guard present in atoi/strtoimax | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.4 row `NUMWIDE-12`; acceptance: `sh tests/crt/run_all_crt.sh` | A2-runtime (`libc/src/{math,time,stdlib,wchar}/*`) |
| `NUMWIDE-13` | MEDIUM | NULL tp/ts/req and NULL tm to time/strftime/wcsftime are not guarded | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.4 row `NUMWIDE-13`; acceptance: `sh tests/libc/run_libc_tests.sh` | A2-runtime (`libc/src/{math,time,stdlib,wchar}/*`) |
| `LIFE-08` | MEDIUM | safe_runtime control state racy under C11/C++11 memory model | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-08`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-09` | MEDIUM | Cleanup registry TLS not unwound on thrd_exit/pthread_exit inside a guard | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-09`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-10` | MEDIUM | Unbounded recursion between report/violation and the user handler | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-10`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-11` | MEDIUM | safe_log/safe_stats not async-signal-safe; logger recursion can deadlock | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-11`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-12` | MEDIUM | mtx_timedlock fallback polling loop with unenforced preconditions | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-12`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-13` | MEDIUM | ONCE_FLAG_INIT hardcodes a platform ABI signature | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-13`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-14` | LOW | errno plumbing incomplete/dead (__cplus_errno_location macro absent) | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-14`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-15` | LOW | Adversarial logging test and notes stale relative to hardened logger | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-15`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-16` | LOW | Telemetry fixed 64-slot cap and racy reset | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-16`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-17` | LOW | TSS destructor limits and call_once recursion delegated without checks | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-17`; acceptance: `sh tests/run_all_evidence.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |
| `LIFE-18` | LOW | uchar internal state process-wide when ps == NULL | `docs/libc/AUDIT_FINDINGS_TRACKER.md` §5.5 row `LIFE-18`; acceptance: `sh tests/crt/unit/run_unit_tests.sh` | A2-runtime (`stdlib/safe/src/safe_runtime.c`, `libc/src/thread/*`, `libc/log/log.c`) |

Not counted as open here: the 53 `DOCUMENTED-CONTRACT` rows (inherent caller responsibilities recorded in the five audits), the 17 `FIXED` rows, 7 compiler gaps (§b) and 4 blocked rows (§d). The tracker also records a cross-audit duplicate (logger `errno`: NUMWIDE-16 and LIFE-06 share one fix) — see contradictions C10 in §e; the 127 total is reported as-is, not deduplicated further.

## (b) Compiler-dependent gaps with reproducer paths

### b.1 Tracker `GAP-COMPILER` findings (7)

| ID | Finding | Reproducer / acceptance path | Responsible component |
|---|---|---|---|
| `ALLOC-07` | No alloc_size attributes on exported wrappers; Agent 1 .len/extent lowering incomplete | `sh tests/stdlib/compile_time/run_compile_time.sh` (see `docs/libc/COMPILE_TIME_GAPS.md`) | A1-compiler |
| `STDIO-07` | Checked-format literal gate reliability; v*/direct-call bypass and toolchain-dependent detection | `sh tests/stdlib/compile_time/run_compile_time.sh` (see `docs/libc/COMPILE_TIME_GAPS.md`) | A1-compiler |
| `STDIO-08` | scanf_s Annex-K divergence (no rsize_t argument, width must be literal) | `sh tests/stdlib/compile_time/run_compile_time.sh` (see `docs/libc/COMPILE_TIME_GAPS.md`) | A1-compiler |
| `STDIO-14` | %n parser does not recognise non-standard length prefixes (e.g. MS I64) | `sh tests/stdlib/compile_time/run_compile_time.sh` (see `docs/libc/COMPILE_TIME_GAPS.md`) | A1-compiler |
| `NUMWIDE-11` | format(wprintf/wscanf) attributes ignored on Apple Clang; diagnostics suppressed | `sh tests/libc/run_libc_tests.sh` (see `docs/libc/COMPILE_TIME_GAPS.md`) | A1-compiler |
| `NUMWIDE-18` | Latent duplicate cplus_exp10 definition if a toolchain provides __builtin_exp10 | `sh tests/libc/run_libc_tests.sh` (see `docs/libc/COMPILE_TIME_GAPS.md`) | A1-compiler |
| `NUMWIDE-21` | Public header uses long instead of time_t for cplus_time/cplus_gmtime | `sh tests/stdlib/compile_time/run_compile_time.sh` (see `docs/libc/COMPILE_TIME_GAPS.md`) | A1-compiler |

### b.2 Agent 1 hand-off items (12)

None of these passes compile-time enforcement in the recorded baseline; an `ACCEPT` in a manifest means the compiler accepted the unsafe program.

| # | Missing capability | Reproducer path | Evidence path | Responsible component |
|---:|---|---|---|---|
| 1 | Class-field lifetime tracking (`drop`/`close`, double `drop`, reset misuse) | `tests/stdlib/compile_time/{vector_use_after_drop,collections_use_after_drop,net_use_after_close,file_double_close,error_use_after_reset}.cp` | `docs/libc/IMPLEMENTATION_PLAN.md` §5 item 1; `docs/libc/COMPILE_TIME_GAPS.md` §7.1 | A1-compiler |
| 2 | Guard ownership state (owner, locked/unlocked, guard→mutex liveness) | `tests/stdlib/compile_time/thread_{double_unlock,unlock_without_ownership,guard_after_destroy,invalid_mutex_owner}.cp` | `IMPLEMENTATION_PLAN.md` §5 item 2; `COMPILE_TIME_GAPS.md` §7.2 | A1-compiler |
| 3 | Alias analysis / exclusivity for class owners, slice→source linking | `tests/stdlib/compile_time/{vector_mutable_alias,collections_mutable_alias,vector_invalid_slice,string_dangling_view}.cp` | `IMPLEMENTATION_PLAN.md` §5 item 3; `COMPILE_TIME_GAPS.md` §7.3 | A1-compiler |
| 4 | `[CPLUS-` tags on use-after-move / escaped-borrow / copied-owner rejections | `tests/stdlib/compile_time/{collections,vector}_use_after_move.cp`, `net_use_after_transfer.cp`, `string_invalid_transfer.cp`, `file_{use_after_move,invalid_borrow}.cp` | `IMPLEMENTATION_PLAN.md` §5 item 4; `COMPILE_TIME_GAPS.md` §7.4 | A1-compiler |
| 5 | Stdlib class bounds metadata (constant OOB via container methods, stale slices) | `tests/stdlib/compile_time/{vector_const_oob,vector_invalid_slice}.cp` | `IMPLEMENTATION_PLAN.md` §5 item 5; `COMPILE_TIME_GAPS.md` §7.5 | A1-compiler |
| 6 | Dynamic function-pointer dispatch bounds (UBSan-only runtime today) | `tests/compile_fail/case13_dispatch_const.c`, `tests/runtime_fail/case13_dispatch_runtime.c` | `IMPLEMENTATION_PLAN.md` §5 item 6; `docs/libc/DISPATCH_BOUNDS_HANDOFF.md` | A1-compiler |
| 7 | `.len` lowering for SafeCRT/libc allocators; `alloc_size` attributes; `&var` false positive | `tests/compile_fail/alloc_size_oob.c`, `docs/patches/agent1-len-managed-allocators.md` | `IMPLEMENTATION_PLAN.md` §5 item 7; `docs/libc/KNOWN_COMPILER_GAPS.md` §3 | A1-compiler |
| 8 | Interprocedural/pointer-extent/heap-capacity bounds; 547 undiagnosed stack-escape cases | `tests/scenarios/scenario_06.c`, `tests/compile_fail/stack_escape_*` (fixtures proposed in top_gaps rank 4) | `IMPLEMENTATION_PLAN.md` §5 item 8; `docs/libc/KNOWN_COMPILER_GAPS.md` §§1–4 | A1-compiler |
| 9 | Destructor-aware unwinding / exception-based recovery | `tests/runtime/run_runtime_tests.sh`, `docs/libc/RUNTIME_RECOVERY.md` | `IMPLEMENTATION_PLAN.md` §5 item 9; `docs/libc/AUDIT_lifecycle.md` F3 | A1-compiler (mitigation: A2-runtime) |
| 10 | Wide-format attributes and tagged narrow-format literal gate | `tests/libc/test_wchar_format.c`, `scratch/bugs{19,20,42,56}.cp` | `IMPLEMENTATION_PLAN.md` §5 item 10; `docs/libc/AUDIT_stdio_format.md` §11 | A1-compiler |
| 11 | Once-flag initialization from target SDK rather than hard-coded constant | `libc/install-include/cplus_threads.h:60-69`, compile-time `static_assert` vs host `PTHREAD_ONCE_INIT` | `IMPLEMENTATION_PLAN.md` §5 item 11; `docs/libc/AUDIT_lifecycle.md` F13 | A1-compiler + A2-runtime |
| 12 | Re-validate compiler-readable contract schema against Agent 1's real file | `compiler/Analysis/libc_contracts.json` (absent on this branch); `python3 tests/libc/test_compiler_contract_alignment.py` | `IMPLEMENTATION_PLAN.md` §5 item 12; `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` | A1-compiler |

### b.3 Compile-time manifest gap detail

- 46-case snapshot: 9 expected REJECT → only 5 tagged; **5 of 7 families (`thread`, `collections`, `vector`, `net`, `file`) have zero tagged REJECTs**. Evidence: `docs/libc/COMPILE_TIME_GAPS.md` §§3–5.
- 4 REJECT-expected cases are untagged: `net_use_after_transfer.cp`, `string_invalid_transfer.cp`, `file_use_after_move.cp`, `file_invalid_borrow.cp`; 2 RUNTIME-expected cases are untagged: `collections_use_after_move.cp`, `vector_use_after_move.cp`. Evidence: `COMPILE_TIME_GAPS.md` §4.
- 8-manifest addendum: `json.json` contributes **zero** tagged REJECTs, one extra untagged PARTIAL (`json_invalid_transfer`), and reports that including `stdlib/json.hp`/`yyjson.h` suppresses the AST safety pass. Evidence: `COMPILE_TIME_GAPS.md` addendum; `tests/stdlib/compile_time/json.json`.
- Manifest contradictions remain on disk: `net.json` marks `net_use_after_transfer.cp` REJECT with `tagged: false`; `file.json` marks `file_invalid_borrow.cp` REJECT while its note says PARTIAL. Evidence: `COMPILE_TIME_GAPS.md` §2; contradiction C8 in §e.

## (c) Unsupported platform/toolchain configurations

| Configuration | Observed limitation | Evidence path | Responsible component |
|---|---|---|---|
| Apple clang 16 + ASan on this macOS host | Startup abort `sanitizer_malloc_mac.inc:189 CHECK failed` even for a trivial program; ASan absence is not a memory-safety proof | `docs/stdlib/TASK6_FINAL_REPORT.md` §6; `docs/libc/ASAN_AUDIT.md` §§90–112; `docs/libc/IMPLEMENTATION_PLAN.md` §1.6 | A2-runtime (documentation) + CI/SEC |
| Homebrew clang 23 + ASan | Works only with explicit `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`; default `MacOSX27.sdk` missing | `IMPLEMENTATION_PLAN.md` §1.6, §6.4 | CI/SEC |
| TSan locally | Needs `DYLD_INSERT_LIBRARIES=.../libclang_rt.tsan_osx_dynamic.dylib`; otherwise "Interceptors are not working"; NOT VERIFIED | `IMPLEMENTATION_PLAN.md` §1.6, §6.4; `docs/libc/COVERAGE.md` §§121–123 | CI/SEC |
| libFuzzer locally | `libclang_rt.fuzzer_osx.a` absent; only deterministic fuzz harnesses pass | `IMPLEMENTATION_PLAN.md` §1.6 | CI/SEC |
| Linux CI (`linux-crt`, `asan-libc-crt`), Linux SDK consumer, win32 cross-build | Not verifiable from the macOS arm64 host; local green export is not a remote result | `IMPLEMENTATION_PLAN.md` §6.5; `docs/libc/REMOTE_GATE_EVIDENCE.md`; `api_status_matrix.json:top_gaps` rank 13 | CI/SEC |
| Windows x86 ABI / calling convention | No Windows evidence; only layout measured by a 70-layout host/C+ agreement test; 2 of 714 matrix rows have ABI evidence and none establish calling convention | `TASK6_FINAL_REPORT.md` §10; `api_status_matrix.json:counts.evidence_coverage.abi_test` | CI/SEC + A2-runtime |
| Non-binary64 `long double` target (e.g. 80-bit x86) | `cplus_exp10l` returns literal `0.0L`; cannot be fully verified on macOS arm64 where `long double == double` | `AUDIT_FINDINGS_TRACKER.md` NUMWIDE-04; `IMPLEMENTATION_PLAN.md` §3.4 F04 | A2-runtime |
| Windows aligned free path | `_aligned_free` compile check cannot be verified on this host; `cplus_aligned_alloc` untracked (ALLOC-03) | `IMPLEMENTATION_PLAN.md` §2 ALLOC F-03 | A2-runtime + CI/SEC |
| Apple clang 16 host headers | `<threads.h>` and `<uchar.h>` not provided by the host (`-std=c11`/`-std=c17` "file not found") | `docs/libc/iso_c_inventory.json` `notes[3]` | A2-runtime (host-backed classification) |
| `ONCE_FLAG_INIT` | Hardcodes Apple private ABI constant `0x30B1BCBA`; portable per-platform source file needed | `AUDIT_FINDINGS_TRACKER.md` LIFE-13; `IMPLEMENTATION_PLAN.md` §3.5 F13 | A2-runtime + A1-compiler |
| Hosts lacking long-double/complex symbols | Portability of ~155 math + 69 complex wrappers unverified; host-capability CMake probe not implemented | `api_status_matrix.json:top_gaps` rank 10; `IMPLEMENTATION_PLAN.md` §6.6 | A2-runtime + CI/SEC |
| Non-standard `%n` length prefixes (MS `I64`) | Parser does not recognise them | `AUDIT_FINDINGS_TRACKER.md` STDIO-14 | A1-compiler + A2-runtime |
| macOS Annex K | No Annex K hooks; `scanf_s` diverges (no `rsize_t`, literal width); Annex K macro names are not linkable symbols | `AUDIT_FINDINGS_TRACKER.md` STDIO-08; `api_status_matrix.json:top_gaps` rank 8 | A2-runtime + A2-docs |
| Agent 1 contract schema | `compiler/Analysis/libc_contracts.json` absent on this branch; 32 emitted entries vs 29 requested names; schema unvalidated | `IMPLEMENTATION_PLAN.md` C9; `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` | A1-compiler |

## (d) Missing/unsupported APIs with exact counts

| Category | Exact count | Detail | Evidence path | Responsible component |
|---|---:|---|---|---|
| ISO C MISSING | **27** | 23 C23 math (`nextup/f/l`, `nextdown/f/l`, `fadd/f/l`, `fsub/f/l`, `fmul/f/l`, `fdiv/f/l`, 3-arg `hypot`, `frexp16`, `fma16`, `logb16`, `exp16`) + `timespec_getres`, `FE_TIESAWAY`, `mbrtoc8`, `c8rtomb` | `docs/libc/iso_c_inventory.json` per-API records; `IMPLEMENTATION_PLAN.md` §1.2; `docs/libc/C23_MATH_UPSTREAM.md` | A2-runtime (needs pinned upstream) |
| ISO C UNSUPPORTED | **49** | 34 `<stdatomic.h>`, 13 `<stdio.h>` (`gets`, `tmpnam`, Annex K macro names, `gets_s`, `tmpnam_s`), 2 `<stdlib.h>` (`bsearch_s`, `qsort_s`) | `iso_c_inventory.json` per-API records; `IMPLEMENTATION_PLAN.md` §1.2, §6.3 | A2-runtime + A2-docs (product/ABI decision) |
| BLOCKED tracker findings | **4** | NUMWIDE-07 (23 DECL-ONLY math), -08 (`FE_TIESAWAY`), -09 (`timespec_getres`), -10 (`mbrtoc8`/`c8rtomb`); SKIPs retained | `AUDIT_FINDINGS_TRACKER.md` §5.4; `IMPLEMENTATION_PLAN.md` §6.1–6.2 | A2-runtime (external unblock condition) |
| Matrix MISSING / UNSUPPORTED (different snapshot) | 13 / 13 | `counts.by_classification.MISSING = 13`; `counts.by_status.UNSUPPORTED = 13` at HEAD `3131acf`; do not add to the 27/49 figures | `api_status_matrix.json:counts` | A2-docs |
| SafeCRT vendored APIs not integrated | **387 of 437** | 40 INTEGRATED, 10 ADAPTER_ONLY, 387 NOT_INTEGRATED, 0 UNAVAILABLE; 18 upstream + 1 adapter sources compiled; 188 upstream `.c` not compiled | `IMPLEMENTATION_PLAN.md` §1.8; `docs/libc/COVERAGE.md` §§68–83 | A2-runtime |
| Deep stdlib entries not referenced | **126 of 381** | input 10, video 22, audio 21, gui 16, http 14, image 9, graphics 9, async 9, window 6, plus small gaps (io 1, json 1, log 2, net 1, thread 4, vector 1) | `docs/stdlib/REGRESSION_INVENTORY.md` §5; `tests/stdlib/agent2_audit/public_api_inventory.md` | A2-docs + test owner |
| Placeholder `_s` contracts | **8** | e.g. `strcpy_s`, `strncpy_s`, `strcat_s`; text "exact signature from Agent 2 capability layer" not verified | `api_status_matrix.json:top_gaps` rank 3; `docs/libc/CONTRACT_STATUS.md` §3 | A2-docs |
| Stale summary disagreement (not an extra gap) | 34 vs 23 | JSON `counts.by_header` reports 34 MISSING for `math.h`; per-API records and `counts.by_classification` give 27 total / 23 math — the 27/23 figure is authoritative | `iso_c_inventory.json`; `IMPLEMENTATION_PLAN.md` C1–C2 | A2-docs |

`stdckdint.h` is explicitly out of scope in `iso_c_inventory.json` `notes[1]`; it is not counted as a gap here. No fabricated constant, algorithm, upstream file, rating or ABI symbol is proposed to "close" any row in this section.

## (e) Documentation and verification gaps

### e.1 Contradictions recorded but not resolved by the inputs

| # | Contradiction | Evidence path | Responsible component |
|---:|---|---|---|
| C1 | `iso_c_inventory.json:counts.by_header` MISSING counts disagree with per-API records (math 34 vs 23; threads 41 vs 0; uchar 9 vs 2; wchar 11 vs 0; complex 18 vs 0; stdio 1 vs 0; string 1 vs 0). | `docs/libc/iso_c_inventory.json`; `docs/libc/IMPLEMENTATION_PLAN.md` §8 | A2-docs |
| C2 | `C23_MATH_UPSTREAM.md:5-9` says exactly 23 math MISSING, contradicting the JSON summary 34. | `docs/libc/C23_MATH_UPSTREAM.md`; `docs/libc/IMPLEMENTATION_PLAN.md` §8 | A2-docs |
| C3 | `COVERAGE_100_AUDIT.md` prose says 28 MISSING / 7 UPSTREAM (internally 27 vs 28), JSON says 27 / 8. | `docs/libc/COVERAGE.md:74`; `docs/libc/AUDIT_numeric_wide.md:100` | A2-docs |
| C4 | ABI evidence: `COVERAGE.md:77` says 644 of 646 NO EVIDENCE; matrix says 712 of 714. | `docs/libc/COVERAGE.md:77`; `docs/libc/api_status_matrix.json` | A2-docs |
| C5 | C17 gap-closing APIs: matrix 203 vs `COVERAGE.md:23` 202. | `docs/libc/api_status_matrix.json:counts`; `docs/libc/COVERAGE.md:23` | A2-docs |
| C6 | `cplus_isblank` open High vs "FIXED in this pass" (both 2026-11-23). | `docs/libc/AUDIT_memory_string.md`; `docs/libc/WRAPPER_SAFETY_AUDIT.md:59-61` | A2-docs |
| C7 | Lifecycle F3 severity High in source audit vs MED in umbrella audit. | `docs/libc/AUDIT_lifecycle.md:44,86`; `docs/libc/WRAPPER_SAFETY_AUDIT.md:64` | A2-docs |
| C8 | `net.json`/`file.json` REJECT vs untagged PARTIAL notes; `COMPILE_TIME_GAPS.md` resolves to PARTIAL but manifests remain inconsistent on disk. | `docs/libc/COMPILE_TIME_GAPS.md` §2; `tests/stdlib/compile_time/net.json`; `tests/stdlib/compile_time/file.json` | A2-docs |
| C9 | Compiler contract schema emits 32 entries vs 29 requested and is unvalidated; multi-TU `.hp` linkage reported already working despite an existing patch. | `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md`; `docs/libc/KNOWN_COMPILER_GAPS.md:62` | A2-docs |
| C10 | Logger-`errno` defect counted in two audits (NUMWIDE-16 / LIFE-06); wide `%n` appears in two audits. | `docs/libc/AUDIT_numeric_wide.md`; `docs/libc/AUDIT_lifecycle.md`; `docs/libc/WRAPPER_SAFETY_AUDIT.md` | A2-docs |
| C11 | STDIO F10 severity dual (temp-file aspect LOW, trap raises to MED); flagged not to double-count. | `docs/libc/AUDIT_stdio_format.md:313-325` | A2-docs |
| C12 | `COVERAGE.md:20` calls 91.80% stale in favour of 91.74% but prints both; function coverage 97.90% "last recorded, not re-measured". | `docs/libc/COVERAGE.md:20` | A2-docs |

### e.2 Traceability, snapshot and runner-evidence gaps

| Gap | Evidence path | Responsible component |
|---|---|---|
| ABI evidence 2 of 714; no exported-symbol/visibility or calling-convention test; no symbol-versioning/SONAME policy | `api_status_matrix.json:top_gaps` ranks 2 and 18 | A2-runtime + CI/SEC |
| 369 of 714 APIs lack a machine-readable contract; 8 `_s` entries are placeholders | `api_status_matrix.json:top_gaps` rank 3; `CONTRACT_STATUS.md` §3 | A2-docs |
| 645 of 714 APIs have logging NO EVIDENCE; only 10 symbols referenced by `test_logging_telemetry.c`; error/result lines cannot be asserted | `api_status_matrix.json:top_gaps` ranks 7 and 19 | A2-runtime + A2-docs |
| Boundary/fuzz evidence (508 / 308) is mostly token mentions in generic corpora, not per-symbol assertions | `api_status_matrix.json:top_gaps` rank 15 | A2-docs |
| C/C++ consumer header matrix incomplete; Linux CI and Windows x86 ABI unverified | `api_status_matrix.json:top_gaps` rank 1; `docs/libc/GAP_ANALYSIS.md` | CI/SEC |
| `SYMBOL_AUDIT.md` generated for 233 symbols at HEAD `9a0b179`, stale vs 714 at HEAD `3131acf` | `api_status_matrix.json:top_gaps` rank 11 | A2-docs |
| All 9 groups of `METHOD_TEST_MATRIX.md` still `_pending` | `api_status_matrix.json:top_gaps` rank 12; `docs/libc/METHOD_TEST_MATRIX.md` | A2-docs |
| FFI/Rust equivalence unproven: SAFETY_GATES 0 PROVEN, 11 PARTIAL, 1 NOT_PROVEN; 600-case comparison has 119 documented differences (outcome equality is not safety equivalence) | `docs/libc/SAFETY_GATES.json`; `TASK6_FINAL_REPORT.md` §5 | A1-compiler + A2-docs |
| Local green runs are not remote results: Linux CI and SonarCloud post-change gates UNVERIFIED (external 23.4% duplication / security D / reliability E not re-scanned) | `TASK6_FINAL_REPORT.md` §10; `docs/libc/REMOTE_GATE_EVIDENCE.md`; `docs/libc/SONAR_TRIAGE.md` | CI/SEC |
| Expectation fixtures are regression baselines captured from a green run, not independent proof of correctness; `coverage_exec.sh` ignores exit status via `|| true` and has no timeout | `TASK6_FINAL_REPORT.md` §1; `docs/stdlib/COVERAGE_TOOL_LIMITATIONS.md` | A2-docs |
| Deep inventory credits five comment/string-only tokens and contains one parser artifact (`listener.listen`); strict code-only variant is 250/131, not 255/126 | `REGRESSION_INVENTORY.md` D5/D7; `tests/stdlib/agent2_audit/public_api_inventory.md` §§2–4 | A2-docs |
| `TASK6_FINAL_REPORT.md` §4 arithmetic: 5 tagged + 7 PARTIAL + 40 clean = 52, but 52 − 5 tagged − 19 expected ACCEPT = 28, not the stated 27 gap cases; its table also says REJECT "6 as expected" against 5 tagged observed. Not recomputed from manifests here — reported as an internal documentation inconsistency | `docs/stdlib/TASK6_FINAL_REPORT.md` §4 | A2-docs |
| Cross-family harness caveat: `run_deep_tests.sh` added the yyjson triplet only for `deep_json*` at contract-writing time, so `deep_cross_family.cp` was verified with explicit commands, not via the full runner. Reconcile against the reported 28-test runner sweep before treating it as runner-covered | `docs/stdlib/DEEP_CROSS_FAMILY_CONTRACT.md` §4 "Harness note"; `TASK6_FINAL_REPORT.md` §2 | A2-docs + test owner |
| No 100% ISO C coverage, complete memory safety, or Rust-equivalence claim is made in any input; this document repeats that boundary | `IMPLEMENTATION_PLAN.md` §0.4; `docs/libc/WRAPPER_SAFETY_AUDIT.md:97` | A2-docs |

## (f) Cross-module scenarios not yet tested

"Not tested" below means **no matching static call-site reference in the deep corpus** or no dedicated suite recorded in the cited evidence. It is not proof that no other shallow suite touches the symbol.

| Scenario | What is missing | Evidence path | Responsible component |
|---|---|---|---|
| Whole stdlib families | deep corpus has zero references to `input.hp` (10 entries), and near-zero coverage of `video.hp` (3/25), `audio.hp` (8/29), `gui.hp` (2/18), `http.hp` (5/19), `image.hp` (1/10), `graphics.hp` (7/16), `async.hp` (5/14), `window.hp` (5/11) | `REGRESSION_INVENTORY.md` §§4–5; `TASK6_FINAL_REPORT.md` §9 | test owner + A2-docs |
| Cross-family interactions beyond CF1–CF6 | `deep_cross_family.cp` covers only string→strmap key ownership, error+vector OOM, moved intmap+generation alias, file/io read_line, string→json and a single error callback. Fatal-safety probes are explicitly excluded; no HTTP+JSON, audio+thread, SDL input+window+graphics pipeline, or async+thread scenario | `docs/stdlib/DEEP_CROSS_FAMILY_CONTRACT.md` §§1–2 | test owner |
| Thread/signal lifecycle across modules | `cplus_pthread_cond_wait` and `cplus_sigaction` appear in only 2 files each (mostly corpus_null); no dedicated concurrency lifecycle or signal-state restoration test; no TSan evidence | `api_status_matrix.json:top_gaps` rank 16; `AUDIT_FINDINGS_TRACKER.md` LIFE-11; `docs/stdlib/DEEP_THREAD_TASK5_CONTRACT.md` (scope: `thread.hp` only) | A2-runtime + test owner |
| Cross-layer stdio registry handoff | Plain `cplus_fclose`, `cplus_fclose_s`, `freopen`/`fdopen` registry reconciliation and 257/513-entry cache wrap not exercised across both layers | `AUDIT_FINDINGS_TRACKER.md` STDIO-02, STDIO-09, STDIO-10; `IMPLEMENTATION_PLAN.md` §2 F1–F3 | A2-runtime |
| Generation-blind stale alias across families | Checked free/transfer across containers, file handles and sockets not exercised | `AUDIT_FINDINGS_TRACKER.md` ALLOC-01, ALLOC-02 | A2-runtime |
| Logging + cleanup under concurrency | Async-signal-safe logging, handler recursion, `thrd_exit` cleanup unwinding and racy runtime config not cross-tested | `AUDIT_FINDINGS_TRACKER.md` LIFE-08..LIFE-11 | A2-runtime |
| FFI / language-boundary equivalence | No dedicated FFI boundary test; 547 undiagnosed stack-escape scenario classes | `api_status_matrix.json:top_gaps` rank 4; `docs/libc/SAFETY_GATES.json` gate 12 | A1-compiler + CI/SEC |
| Cross-platform cross-module runtime | Deep suite recorded only for `build/c+` on this host (default, strict-none, ubsan); no Linux/Windows cross-family execution | `TASK6_FINAL_REPORT.md` §§2, 10 | CI/SEC |

## Top five gaps (this audit's ranking)

1. **46 OPEN wrapper-safety defects, 5 HIGH** (ALLOC-01/02/03, MEMSTR-07/09) — `AUDIT_FINDINGS_TRACKER.md` §6; §(a) above. No fix evidence recorded for any OPEN row.
2. **27 MISSING + 49 UNSUPPORTED ISO C APIs and 387/437 SafeCRT APIs not integrated** — `iso_c_inventory.json`, `IMPLEMENTATION_PLAN.md` §1.8; §(d) above.
3. **27 compile-time capability/tagging gaps plus 7 GAP-COMPILER findings**; 5 of 7 families have zero tagged REJECTs — `COMPILE_TIME_GAPS.md`, `TASK6_FINAL_REPORT.md` §4; §(b) above.
4. **Traceability evidence is thin**: ABI 2 of 714, contract 345 of 714, logging 69 of 714; 12 unresolved input contradictions (C1–C12) — `api_status_matrix.json`, `IMPLEMENTATION_PLAN.md` §8; §(e) above.
5. **126 of 381 stdlib entries have no deep-test reference**, whole modules untouched and cross-module scenarios limited to CF1–CF6 — `REGRESSION_INVENTORY.md` §5, `DEEP_CROSS_FAMILY_CONTRACT.md`; §(f) above.
