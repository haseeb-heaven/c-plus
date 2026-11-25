# Agent 2 — Phase 9 final synthesis (2026-11-23)

Owner: Agent J (10 of 10), **docs-only**. Branch `agent/modern-stdlib`,
HEAD `ab8cb51` at synthesis time. PR #17 remains open; nothing pushed or merged.
This agent did **not** edit source, did not run `git add`/`git commit`, and did
**not** re-execute suites — it re-read the recorded artifacts named below and
consolidated them. Where a number is a recorded result, the artifact that
produced it is cited; where an artifact had not landed, it is marked **PENDING**
with the exact command to land/verify it. No result is inferred from a build
alone, and no doc-only study is credited as an implementation.

## Reading rules for this report

1. **API reachability is not runtime coverage.** A public name with a matching
   call-site token in a test corpus is *reachable by that measurement*, not
   executed, not branch-covered, and not proven safe. Function-level runtime
   attribution for `.hp` headers is unavailable on this host.
2. **No 100% stdlib-safety claim.** Platform modules are static-inspection only;
   46 audit findings remain OPEN; 27 compile-time capability gaps exist; ASan is
   unsupported on this host; Windows x86 ABI and remote CI/Sonar gates are
   UNVERIFIED. Matching Rust checksums are equality of one deterministic
   integer, not equivalence of safety mechanisms.
3. **STRICT `--sanitize=none` is the primary execution configuration** for the
   runtime suites below. Sanitizer runs are reported separately and never
   folded into the pass count.

---

## 1. Public APIs inventoried — implemented / tested / partial / not-implemented / blocked

All denominators are from recorded inventories; fractions are written as
`n of N` rather than invented percentages.

| API surface | Denominator | Implemented | Tested | Partial | Not implemented | Blocked |
| --- | --- | --- | --- | --- | --- | --- |
| `stdlib/*.hp` public callable entries | **381 entries / 21 headers** (232 distinct bare names; 149 overload/cross-header repeats) | 381 of 381 entries defined in headers (static parse) | **255 of 381** have a static deep-test call-site (234 literal calls + 21 compiler-inserted `drop()` credits); strict code-only variant **250 of 381** | 5 credits rest on comment/string tokens only (D7); `string_view`, `slice` are caller-responsible (documented gap) | **126 of 381** have no deep-test call-site | 2 corpus tests excluded with recorded reasons (see §2) |
| ISO C API inventory | **948** | 872 of 948 (derived: 948 − 27 − 49) | exercised by libc/CRT suites (counts in §2) | — | **27 of 948 MISSING** (23 C23 math + `timespec_getres`, `FE_TIESAWAY`, `mbrtoc8`, `c8rtomb`) | **49 of 948 UNSUPPORTED** on this host |
| SafeCRT / safeclib surface | **437** | **40 of 437 integrated** | `test_safecrt_upstream` all checks passed, rc 0 | **10 of 437 adapter-only** | **387 of 437 not integrated** | — |
| Wrapper-safety audit findings | **127** | **17 of 127 FIXED** (each with a regression) | see §4 | **53 of 127 documented-contract decisions** | **46 of 127 OPEN** | **4 of 127 BLOCKED**; **7 of 127 GAP-COMPILER** |
| Benchmark `cplus_safe` rows | **20 driver rows** | 15 measured `ok` rows | 15 of 15 validated (`BENCH_VALID 192 0`) | — | — | **5 of 20 blocked** by `CPLUS_EXIT_ARITHMETIC` (no binary produced) |

Source of truth: `docs/stdlib/REGRESSION_INVENTORY.md` (§§2–7),
`tests/stdlib/agent2/FINAL_AUDIT.md` (§§2, 14),
`docs/stdlib/TASK6_FINAL_REPORT.md` (§3, §10),
`docs/libc/AUDIT_FINDINGS_TRACKER.md`,
`docs/MODERN_STDLIB.md` §10,
`benchmarks/safety_compare/RESULTS.md`.

**PENDING — Agent B's consolidated API inventory has not landed:**

- `docs/stdlib/API_INVENTORY_AGENT2.md` — **PENDING at synthesis time.**
  Verify with `test -f docs/stdlib/API_INVENTORY_AGENT2.md && echo LANDED || echo PENDING`.
  Underlying source data is already recorded in
  `docs/stdlib/REGRESSION_INVENTORY.md` and
  `tests/stdlib/agent2/API_COVERAGE_REPORT.md`; regenerate the inventory with
  `python3 tests/stdlib/coverage.py` (parser `entries()` over `stdlib/*.hp`).

---

## 2. Tests — discovered / executed / pass / fail / timeout / blocked

**Primary configuration: `--strict --sanitize=none` with timeouts and byte-exact
fixtures.** These are recorded results from the named artifacts, not re-runs by
this agent.

| Suite (recorded artifact) | Discovered / executed | PASS | FAIL | BUILD_FAIL | TIMEOUT | SKIPPED | BLOCKED | Command |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| Agent 2 aggregate runner (`tests/stdlib/agent2/FINAL_AUDIT.md`) | **50 / 50** (28 `deep_*.cp` + 11 `agent2/*.cp` + 11 curated) | **50** | 0 | 0 | 0 | 0 | 0 | `sh tests/stdlib/agent2/run_all.sh --clean` |
| Deep runner — strict-none mode (`docs/stdlib/TASK6_FINAL_REPORT.md`) | **28 / 28** | **28** | 0 | 0 | 0 | 0 | 0 | `CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/run_deep_tests.sh` |
| Deep runner — all three modes | **84 invocations** (28 × 3: default, strict-none, ubsan) | **84** | 0 | 0 | 0 | 0 | 0 | same runner |
| Phase-3 discovery audit (`tests/audit_phase3/work/results.tsv`) | **99 / 99** | **88** | **1** | **9** | **1** | 0 | 0 | `bash tests/audit_phase3/run_all.sh` (exit 1 — see below) |
| Aggregate evidence (`tests/run_all_evidence.sh`) | **23 suites** | **23** | 0 | — | — | — | — | `sh tests/run_all_evidence.sh` |
| Compile-time suite (`tests/stdlib/compile_time/`) | 8 manifests / **52 cases** | 6 of 9 REJECT, 19 of 19 ACCEPT, 24 of 24 RUNTIME; **27 capability/tagging gaps not counted as passes** | — | — | — | — | — | `CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/compile_time/run_compile_time.sh` |
| Upstream-integration regressions (`docs/stdlib/upstream-reuse.md` §10.5) | **9 / 9** independently built and executed | **9** | 0 | — | — | — | — | per-test commands in that section (UBSan canonical blocked by runtime skew) |
| Agent C source-level audit tests (`docs/stdlib/UPSTREAM_REUSE_AUDIT_AGENT2.md`) | **19 named tests built and run** (7 C+ integration + 1 safecrt + 10 vendor + 1 LLVM C23) | **18** | **1** (`test_vendor_mem`, §7) | 0 | 0 | 2 documented SKIPs (LLVM C23) | 0 | `timeout 30` per test; exact flags in that document's consolidated table |
| Sanitizer matrix (`tests/stdlib/agent2/SANITIZER_MATRIX.md`) | 72 builds attempted; **27 executed** | **23 of 27** | 0 | — | 0 | — | **45 of 72 UNSUPPORTED** | see §7 |

### Honest accounting of the phase-3 audit's 11 non-passes

`tests/audit_phase3/run_all.sh` uses the same strict/no-sanitizer protocol but a
broader discovery corpus (groups: deep 28, breaker_v3 25, agent2 16, regression
10, breaker_probe 10, stress10 10). Its recorded totals are
`discovered/executed=99 PASS=88 FAIL=1 BUILD_FAIL=9 TIMEOUT=1 BLOCKED=0`, exit 1.
These are **not** hidden or folded into the 50/50 figure:

- **FAIL (1):** `regression/adv_json_fs_net` — runtime exit 1,
  `C+ error: index 8 is outside the bounds of 'rb' (length 8)`.
- **TIMEOUT (1):** `regression/adv_containers` — run exceeded 30 s (rc=134/SIGABRT);
  the harness classifies `rc>=128` as TIMEOUT, never a pass.
- **BUILD_FAIL (9):** strict analyser rejected programs that expect to run —
  `adv_exact_fit`, `adv_long_input` (`[CPLUS-FFI] ... unverifiable extent`);
  `breaker_v3/10_signal_outside_handler` (`[CPLUS-FFI] ... unverifiable provenance`);
  `breaker_v3/14_resource_transfer` (use-after-move);
  `breaker_v3/23_double_free_trap`, `24_invalid_free_trap`,
  `stress10/10_trap_double_free`, `breaker_probe/cplus_probe_double_release`
  (`[CPLUS-OWNER]`); `breaker_v3/25_freed_len_trap` (`[CPLUS-LIFETIME]`).
  Four other compile-time rejections in the corpus were correctly classified
  PASS as *expected* rejections.

### Recorded in-test SKIPs (not passes, not silent)

`net_invalid_address`, `json_alloc_failure_hook` (yyjson allocator not
injectable), `err_code_name_mapping`, conditional `FS5_PERMISSIONS`,
`SKIP abi_custom_alloc`, `SKIP drop_with_waiter`, and the `abi_safety_safecrt`
build variant (requires `-DABI_SAFETY_SAFECRT -Istdlib/safe` +
`build/safe/libcplus_safecrt.a`). Two existing tests are excluded from the
runtime corpus with reasons: `adversarial.cp` (strict compile-fail case) and
`safety_regressions.cp` (needs `<uv.h>`, absent).

---

## 3. Upstream implementations ACTUALLY reused (source-level audit, no doc-only credit)

Credit is given **only** where a sibling changed a source file **and** executed a
regression test (rule in `docs/stdlib/upstream-reuse.md` §10.1). The nine rows
below are the source-level integrations landed in commit `ce018ca` and
independently rebuilt and re-run; each reports a self-checking result with rc 0.

**Agent C's source-level audit has LANDED** as
`docs/stdlib/UPSTREAM_REUSE_AUDIT_AGENT2.md` (Phase 4 audit). It read the
vendored upstream and C+ destination files on disk, grepped ported markers,
`nm`-checked exported symbols and rebuilt/ran the named tests under
`timeout 30`. Its headline: **8 projects genuinely contributed code** (MS STL
vector+string counted as one, GSL, SaferCPlusPlus, Fil-C predicates,
SoftBoundCETS predicates, safeclib/Secure-CRT including the 10 vendor groups,
mimalloc, LLVM `memset_explicit`); **jemalloc RESEARCH-ONLY** and **Cake
REJECTED** get no credit. **19 named tests built and run, 18 of 19 PASS**; the
one failure is `test_vendor_mem` (`vendor_mem: 23 1`), a pre-existing deliberate
C+ security-patch vs upstream-test-expectation mismatch (`safe_mem_constraint.h`
`C+ SECURITY PATCH (2026-11-23)` makes `handle_mem_error()` a no-op for the
destination to avoid a caller-supplied-`dmax` overrun; the test still expects the
upstream destination wipe while the correct `ESNULLP` 400 is returned). No
source was edited to "fix" it. No UBSan pass is claimed for the seven C+
integration tests (canonical UBSan blocked by `CPLUS_EXIT_ARITHMETIC`); the
LLVM C23 `memset_explicit` test did run with `-fsanitize=undefined` and passed
(`c23_upstream_small: ok`, with 2 documented SKIPs).

| # | Upstream + pin | License | Changed C+ source file(s) | Executed regression (result) | ABI-visible change |
| --- | --- | --- | --- | --- | --- |
| I1 | `rurban/safeclib` `90b076e94c4b0fb202dddf948d5d620e5395c1b0` (`bsearch_s`, `memrchr_s`) | MIT | `libc/src/string/upstream_safecrt.c` (new), `libc/install-include/cplus_safecrt_upstream.h` (new) | `test_safecrt_upstream` — all checks passed, rc 0 | two new prefixed symbols only |
| I2 | `microsoft/mimalloc` `c683b7c60c91cb2809977e37d3f4d084d7497180` (overflow/size-class predicates) | MIT | `stdlib/alloc_policy.hp` (new, 177 lines) | `ALLOC_POLICY 42 / FAILURES 0`, rc 0 | none (`static inline` scalars) |
| I3 | `pizlonator/fil-c` `a80e32804fd44d295deb1ce1b0098962022da0a3` (liveness/bounds queries) | BSD-2-Clause-style | `stdlib/allocator.hp` (modified, +108 lines) | `ALLOC_DIAG 26 0`, rc 0 | none (read-only queries) |
| I4 | `microsoft/GSL` (per-file `417ef68`, `a75212b`, `08687fb`) — `narrow`/`not_null`/`span` | MIT | `stdlib/gsl_adapt.hp` (new, 434 lines) | `GSL_ADAPT 80 0`, rc 0 | none (new `gsl_*` names; pointer sizes asserted) |
| I5 | `ibireme/yyjson` `be9bd74bb3c96e00006c2075c330a6b8b90123dc` v0.13.0 (depth/length budgets) | MIT | `stdlib/json.hp` (modified, +122 lines) | `JSON_YYJSON 35 0`, rc 0 | none (additive budget API; single-member layout unchanged) |
| I6 | `duneroadrunner/SaferCPlusPlus` `5edaf67acf746d9ee472a06a51094b31a3db57c0` (lifetime tokens) | BSL-1.0 | `stdlib/lifetime_adapt.hp` (new, 268 lines) | `LIFETIME_ADAPT 25 0`, rc 0 | none (two additive value descriptors) |
| I7 | `santoshn/softboundcets-34` `9a9c09f04e16f2d1ef3a906fd138a7b89df44996` (check predicates) | BSD-3-Clause tree / per-file MIT-style | `stdlib/memory.hp` (modified, +130 lines) | `SOFTBOUND 51 0`, rc 0 | none (`static inline` on existing side table) |
| I8 | `microsoft/STL` `0cabaf3c7e0a73fe3040aabce40cd8833e80392b` (string growth / overlap-safe append) | Apache-2.0 WITH LLVM-exception | `stdlib/string.hp` (modified) | `upstream-string-checks 28 / failures 0`, rc 0 | none (same 3 fields, additive helper) |
| I9 | `microsoft/STL` same pin (vector growth / commit-last guards) | Apache-2.0 WITH LLVM-exception | `stdlib/vector.hp` (modified) | `VECTOR_STL 14 0`, rc 0 | none (two additive helpers) |

**Already present before this phase (verbatim, with provenance):** LLVM libc
`memset_explicit` (`llvmorg-20.1.1`, Apache-2.0 WITH LLVM-exception) and the
vendored safeclib/Intel SafeStringLib units compiled by `scripts/build_safecrt.sh`
(40 integrated / 10 adapter-only / 387 not integrated of 437).

**Explicitly NO reuse credit (doc-only or rejected):** Cake (`GPL-3.0-or-later`
+ AI-training addendum — REJECTED, no source copied); TrapC (no repository,
license UNDETERMINED, docs only); Fil-C pointer representation / InvisiCaps / GC
and SoftBoundCETS trie / shadow stack / key-lock words / `-fsoftboundcets` and
TrapC managed-pointer RTTI (all ABI-breaking — only *ideas* adapted into I3/I6/I7);
STL 1.5× growth, iterators and exception machinery; GSL generic templates;
Sanitizer container annotations; `benchmarks/safety_compare/` artifacts
(performance comparison, not an integration).

**LANDED — Agent C's source-level audit:**

- `docs/stdlib/UPSTREAM_REUSE_AUDIT_AGENT2.md` — **LANDED** (verified with
  `test -f docs/stdlib/UPSTREAM_REUSE_AUDIT_AGENT2.md && echo LANDED || echo PENDING`).
  Its per-project evidence complements `docs/stdlib/upstream-reuse.md`
  §§10.1–10.7 and the nine sibling notes under `docs/stdlib/upstream_reuse/`
  and `tests/stdlib/upstream_reuse/`. Re-verify a row with, e.g.,
  `timeout 30 build/c+ build tests/stdlib/upstream_reuse/vector_stl.cp
  --strict --sanitize=none -o /tmp/vector_stl && timeout 30 /tmp/vector_stl`
  (expect `VECTOR_STL 14 0`).

---

## 4. Confirmed bugs fixed — reproducer, before/after, regression test

Every row below has a reproducer and a permanent regression test; nothing was
"fixed to pass" without evidence. Full catalogue:
`tests/stdlib/agent2/FINAL_AUDIT.md` §10,
`docs/stdlib/TASK6_FINAL_REPORT.md` §8, `docs/libc/AUDIT_FINDINGS_TRACKER.md`,
and the `tests/defense/*/HARDENING.md` reports.

| Defect | Reproducer | Before → After | Regression test |
| --- | --- | --- | --- |
| `file::read_line` rejected exact-fit lines at EOF / before `\n` (D-FILE-9) | `"abcd"` in a 5-byte buffer | `FAIL/101` in `file.hp` → `OK` (genuine overlong still drains + `BOUNDS`); commit `1438bf2` | `tests/stdlib/deep_file_task2.cp`, `DEEP_FILE_TASK2_CONTRACT.md` |
| `io::read_line` exact-fit false BOUNDS and CRLF stray-LF leak (D-IO-1/2) | 1-byte and overlong/CRLF fixtures | false `BOUNDS`/leaked LF → peek + CRLF consume; `OK` and byte-exact output | `deep_io_1byte.cp`, `deep_io_task2.cp`, `extra_io_whitespace.cp` |
| Detached-thread start context on caller stack (D-THREAD-1) | `test_threads_c11.c` | SIGBUS crash 3/3 → heap context freed in trampoline; clean run | `AUDIT_lifecycle.md` F1, `deep_thread_task5.cp` |
| `static unsigned` used as a 128-byte `mbstate_t*` (D-UCHAR-1) | canary + split UTF-8 sequence | 4-byte write into 128-byte object → real `mbstate_t` + surrogate word | `test_uchar.c`, `AUDIT_lifecycle.md` F2 |
| stdio close-table deletion broke probe chains (D-STDIO-1..3) | colliding `FILE*` | broken chain / wrapped closed ring → tombstones + compaction + fail-closed `EMFILE` | `test_fclose_s.c`, `AUDIT_stdio_format.md` |
| Huge-`n` OOB in memchr/memrchr/strncpy/stpncpy (D-STR-1..5) | `SIZE_MAX` + canary | OOB scan/write → `RSIZE_MAX_MEM` guards + NULL checks | `test_string_extra*.c` |
| Logger clobbered `errno`, dropped short writes (D-LOG-1/2) | sentinel `errno`; nonblocking pipe | corrupted errno / lost bytes → save/restore + bounded write loop | `test_logging_telemetry.c`, `deep_log_task5.cp`, `extra_log_edges.cp` |
| `%n` detector bypass via repeated positional specifiers (F1) | `"%1$2$n"`, `"%11$22$n"` | real `%n` write, rc=4, wild-vararg SIGSEGV → `REFUSED`, rc=-1, `errno=22`, sink canary intact | `tests/redteam/format_io/repro_f1_pctn_bypass.c`, `tests/defense/format_io/HARDENING.md` |
| Source over-read in `memcpy_s_auto` / `memmove_s_auto` | tracked 8-byte source, request 32 bytes, 64-byte dest canary | 9 failures, 8 `'A'` bytes copied, errno 0 → `r == NULL`, `errno == ERANGE`, canary intact | `tests/defense/memory/test_memory_hardening.c` T1/T2/T3 |
| Unbounded `strlen()` in `_auto` string wrappers | buffer with no NUL | unbounded over-read before constraint check → bounded scan, `NULL` + `ERANGE` | `tests/defense/string/` (`unterminated_*`) |
| Recent-free cache collision misclassified double free (F-06) | two freed addresses colliding in one bucket | second free counted `invalid_free` → `double_free` correctly classified | `tests/defense/alloc/run_before_after.sh`, `DEFENSE_ALLOC 4149 0` |
| `stdlib_mem_key_current` failed OPEN for NULL/untracked/stale with `key==0` | redteam `F_KEY_CURRENT` / `F_NULL_ARGS` | returned 1 for NULL/untracked → returns 0; live tracked `key==0` unchanged | `tests/redteam/bounds/bounds_redteam.cp`, `tests/defense/lifetime/HARDENING.md` |

**Documented, not "fixed":** `fs_mkdtemp` consumes its `XXXXXX` template on
failure (retry needs reseeding); Tier-1 wide `%n` formatting remains caller
responsibility; unknown-capacity raw pointers cannot be bounds-checked by the
library; interior pointers require callers to pass `(base, offset)`.

**PENDING — phase-9 blue-team hardening notes have not landed** (existing
phase-7 notes at `tests/defense/{alloc,format_io,lifetime,memory,string}/HARDENING.md`
are present and were read). Verify each with
`test -f <path> && echo LANDED || echo PENDING`:

- `tests/defense/memory2/HARDENING.md` — PENDING (dir exists with `probe.c` + `out/`; no runner/doc yet). Re-run with `sh tests/defense/memory2/run.sh`.
- `tests/defense/containers2/HARDENING.md` — PENDING. Re-run with `sh tests/defense/containers2/run.sh`.
- `tests/defense/io_json2/HARDENING.md` — PENDING. Re-run with `sh tests/defense/io_json2/run.sh`.
- `tests/defense/thread2/HARDENING.md` — PENDING. Re-run with `sh tests/defense/thread2/run.sh`.
- `tests/defense/fs_net2/HARDENING.md` — PENDING. Re-run with `sh tests/defense/fs_net2/run.sh`.

---

## 5. ABI test results

Source: `docs/abi/safe_layer_fingerprint.md`, `tests/abi/fingerprints/RESULTS.md`,
`docs/stdlib/FINAL_STDC_ABI_SAFETY_VERIFICATION.md`. All commands below exited 0
when recorded; no ignore file was passed and no result was masked.

| Check | Result |
| --- | --- |
| Fingerprint capture | `sh tools/abi_compare/run.sh capture` → **1162 records** (89 layout/enum, 27 compiler defines, 12 function-signature assertions, 1030 exported symbols, 4 metadata) |
| Fingerprint compare vs authoritative baseline | `sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt` → **MATCH**, diff **0 bytes**; SHA-256 of both files `352b10e8ce6b7f2c1f071ff6d0240bb08aa99de7f9132027314634331681027c` |
| C compatibility suite | `sh tests/abi/run_abi_compat.sh` → **69 passed, 0 failed** (pointer ABI 20/20; layouts 80/80; T5 symbol checks; no C++ mangling on all three `nm` outputs; T6 header-neutrality diff empty, 1822 bytes each) |
| Layout suite | `CPLUS_CLANG=/usr/bin/clang sh tests/abi/run_abi_layouts.sh` → **70/70 layouts match** on host and C+ builds; `diff -u` empty |
| C interop, both directions | T3a: ordinary C driver calls functions emitted by the C+ driver (compile/archive/run/summary PASS). T3b: C+ driver links and calls a precompiled C archive (compile/direct-link/caller build/run/summary PASS) |
| Precompiled libraries | `nm` checks pass on `build/libc/libcplus_hardened.a` (**784** symbols) and `build/safe/libcplus_safecrt.a` (**244** symbols); no export renamed or removed |
| Function signatures | 12 `F` records enforced by `_Static_assert(__builtin_types_compatible_p(...))`; fresh capture compiles with `-Werror` |
| ABI verdict | Checked layer is `static inline` + macro remapping only — **no ABI drift** on this host. **Windows x86 / MSVC / UCRT ABI is UNVERIFIED** (separate evidence in `tests/abi/`); this run's host is arm64-apple-darwin27.2.0, Apple clang 16, LP64. |

The two benign `clang: warning: -Wunused-command-line-argument` messages in the
layout script are the driver's compile/link argument forwarding, not ABI findings.

---

## 6. Benchmark results — real medians

Source: `benchmarks/safety_compare/RESULTS.md` (driver artifacts generated
2026-11-23T04:27:57Z). Protocol: 7 runs after 1 warmup, median of samples, p95
by linear interpolation; matched checksums verify deterministic work, not
correctness or safety. Host: macOS arm64 / Apple M3 Pro / Apple clang 16 /
rustc 1.100.0 / Goose v2026.4 / C+ `-O2 --release`.

**Driver headline:** `sh benchmarks/safety_compare/run_all.sh` →
`counts = {"ok": 15, "blocked": 5, "fail": 0}`, required_failures 0, status PASS,
exit 0. **Zero real failures.** The 5 `cplus_safe` rows are externally blocked
build attempts (`CPLUS_EXIT_ARITHMETIC`), not timings.

| Workload | Variant | median (s) | p95 (s) | status |
| --- | --- | ---: | ---: | --- |
| alloc_bench | Rust -O | 0.010404 | 0.013873 | ok |
| alloc_bench | C+ legacy | 0.039720 | 0.049571 | ok |
| bench_modern | Rust -O | 0.023018 | 0.026586 | ok |
| bench_modern | C+ legacy | 0.020533 | 0.024590 | ok |
| legacy_speed/loop | C -O2 | 0.002890 | 0.003327 | ok |
| legacy_speed/nested | C -O2 | 0.004658 | 0.008925 | ok |
| loop | C -O2 | 0.005660 | 0.006696 | ok |
| loop | Rust -O | 0.005310 | 0.008098 | ok |
| modern | Rust -O | 0.009449 | 0.011401 | ok |
| modern | C+ legacy | 0.008306 | 0.010272 | ok |
| nested | C -O2 | 0.004812 | 0.009405 | ok |
| nested | Rust -O | 0.003341 | 0.006325 | ok |
| safety_matrix | Rust -O | 0.005170 | 0.014102 | ok |
| safety_matrix/matrix | C+ legacy | 0.006687 | 0.014287 | ok |
| validate_results/…/evidence.csv | C+ legacy | 0.004316 | 0.008333 | ok |

Modern-stdlib workload, 7 interleaved runs (allocation-heavy `vector`/`string`,
checksum `2200000200000` for every variant): C+ safe **0.0140 s**
(min 0.0134, max 0.0188); C+ legacy **0.0061 s** (0.0052–0.0077); Rust
**0.0060 s** (0.0048–0.0105); Goose **0.0044 s** (0.0041–0.0090). Ratio of
medians vs C+ legacy: safe **2.31×**, Rust 0.99×, Goose 0.72×. Earlier integer
workloads (`loop sum (2e8)`, `nested (4000²)`): plain C 0.0036/0.0023 s,
C+ safe 0.1075/0.0090 s, C+ legacy 0.0038/0.0023 s, Rust 0.0042/0.0026 s,
Goose 0.0043/0.0024 s — the non-instrumented compilers folded the loops, so
those numbers are optimisation differences, not language-speed verdicts.

Validator (fixed and verified): `BENCH_BLOCKED 5`, `BENCH_VALID 192 0`, exit 0
(pre-fix `BENCH_VALID 192 5`, exit 1). Fil-C not measured — no toolchain installed.

**PENDING — phase-8 benchmark report has not landed:**

- `benchmarks/phase8/RESULTS.md` — **PENDING at synthesis time** (partial logs
  `abi-1-fingerprint-compare.log` … `abi-5b-sdk-consumer-appleclang.log` and
  sibling defence harnesses exist, but the report docs have not landed). Verify
  with `test -f benchmarks/phase8/RESULTS.md && echo LANDED || echo PENDING`.
  Reproduce the recorded numbers with `sh benchmarks/safety_compare/run_all.sh`
  and read `benchmarks/safety_compare/results/summary.json` +
  `results/table.txt`. Do not substitute the blocked `cplus_safe` rows for
  measured medians.

---

## 7. Remaining work / blockers

1. **External `CPLUS_EXIT_ARITHMETIC` runtime skew (Agent 1).** The external
   `c-plus-core/runtime/cplus_runtime.c:129` uses `CPLUS_EXIT_ARITHMETIC`, absent
   from this repo's `runtime/include/cplus/cplus.h` (enum ends at
   `CPLUS_EXIT_IO = 109`; external header has `= 110`). Every instrumented
   runtime compilation fails. Recorded downstream impact: `tests/run_tests.sh`
   **51 compilation failures** (99 pass); `tests/stdlib/run_deep_tests.sh`
   **58 build failures** (28 pass, 1 unrelated skip); 5 benchmark `cplus_safe`
   rows blocked. Agent 2 will not edit compiler/runtime — see
   `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` for Options A/B and exact
   re-verification commands. Environment-only bridge used for some sibling
   re-runs: `CPLUS_INCLUDE=.../c-plus-core/runtime/include`.
2. **ASan unsupported.** Backend probe: `error: sanitizer 'asan' is not supported
   by the selected backend (/usr/bin/clang)`. Separately, Apple clang's ASan
   runtime aborts with `sanitizer_malloc_mac.inc:189 CHECK failed` even for a
   trivial program; a subset verified with Homebrew clang + explicit
   CommandLineTools SDK. Leak/Thread/Memory/CFI likewise UNSUPPORTED (45 of 72
   matrix builds rejected). **0 of 9 tests ran under ASan**; UBSan passing is not
   a memory-safety proof. Four genuine `-fsanitize=integer` findings in
   `runtime/cplus_runtime.c:123` (`cplus_hash`, intentional wrapping multiply)
   are outside Agent 2 scope.
3. **Host/platform scope.** Verified on **macOS arm64 only** (Darwin 27.2, Apple
   clang 16). Linux CI (incl. SDK consumer), SonarCloud, Windows x86
   ABI/DLL behaviour and live-game/injected-DLL results are **UNVERIFIED**.
4. **No 100% safety claim.** 46 OPEN findings, 7 compiler-gap findings,
   27 compile-time capability/tagging gaps, 123/948 (27+49) ISO C missing or
   unsupported, 387/437 SafeCRT not integrated, 126/381 `.hp` entries without a
   deep-test call-site; 9 platform modules (async, audio, graphics, gui, http,
   image, input, video, window) are static-source-inspection only. The
   phase-3 audit retains 1 FAIL, 1 TIMEOUT and 9 BUILD_FAIL (§2). Reachability
   ≠ execution ≠ branch coverage ≠ safety. Overall verdict remains
   **PARTIALLY COMPLETE**.
5. **Documented test/expectation mismatch (not a defect introduced here):**
   `test_vendor_mem` reports `vendor_mem: 23 1` — the test still expects upstream
   `memccpy_s` to wipe the destination on a NULL source, while the vendored
   `safe_mem_constraint.h` C+ security patch deliberately leaves it intact to
   avoid a caller-supplied-`dmax` overrun. The return code is the correct
   `ESNULLP` (400). No source was edited to "fix" the test; see Agent C's audit
   §7.
6. **Pending sibling artifacts** — §§1, 4, 6 list exact paths and verification
   commands; the parent must re-run this synthesis once they land. Agent C's
   upstream-reuse audit (§3) has landed.

---

## 8. Commit list (`git log`, no commit made by this agent)

HEAD at synthesis: **`ab8cb51`**; 356 commits on `agent/modern-stdlib`. Relevant
recent hashes (newest first) for the parent to reference:

| Hash | Subject |
| --- | --- |
| `ab8cb51` | Add dual pure-C/modern-C+ hexdump tool with per-standard proof |
| `717b68c` | Close all red-team memory corruption findings with root-cause fixes |
| `1b656ff` | Fix %n bypass; verify real-project C+ conversions and C-standard matrix |
| `55694b5` | Fix confirmed http.hp OOB write and add standard/battle/fuzz results |
| `d3ffb90` | Add game A C+ conversion and hardening/fuzz findings |
| `49aa068` | Download and verify real permissive-licensed C projects + fuzz10k results |
| `07c14d8` | Add second-wave Rust comparisons and GUI/secure aggregates |
| `f233fa9` | Add modern Nuklear/SDL3 GUI apps, secure apps, and real-world utilities |
| `e71b7fd` | Add runtime hostile-evidence and dual ABI preservation docs |
| `b5bc37d` | Add three-way runtime-safety comparison: Rust / Fil-C / Trap-C |
| `c1bd15e` | Add screenshot evidence in examples/ showing stdlib safety refusals |
| `4cddcfb` | Add final verification docs and new games_2 sources |
| `f4b64ee` | Finalize expanded Rust comparisons: CLI/games 9/9, DB/engine/safety 10/10 MATCH |
| `c6195a0` | Finalize expanded examples adversarial suite: 135 cases, 135 refusals, 0 failures |
| `c08c9d0` | Finalize expanded examples aggregate: 6/6 groups PASS |
| `58860a8` | Finalize example-vs-Rust aggregate: 49 examples, 43 MATCH/5 DIFFERENT/1 BLOCKED |
| `727fa43` | Add standard-C ABI+safety final proof and example comparison results |
| `f022d28` | Add standard-C hostile-input safety proof |
| `2a2e13f` | Close 27 missing ISO C APIs and add runtime-safety wrappers |
| `42cd699` | Finalize 100% method-test aggregate runner and audit |
| `8a00f33` | Add 100% per-method test suites for all standard library headers |
| `de79021` | Vendor already-made safeclib implementations into the checked runtime |
| `a11de7f` | Add deterministic fuzz harnesses for the checked ordinary-C layer |
| `4ec4ca2` | Add ABI-preserving ordinary-C checked layer and close release blockers |
| `511afc9` | Reconcile final benchmark validator and upstream reuse evidence |
| `2294cb1` | Finalize C+/Rust benchmark proof and untrack generated binaries |
| `ce018ca` | Integrate proven upstream implementations into C+ stdlib (I1–I9) |
| `a85f7f5` | Add resource exhaustion evidence notes |
| `c5d2582` | Complete Task 6 security hardening and fix reproduced defects |
| `1df837f` | ABI-preserving runtime safety, P0 Safe CRT hardening and compiler contracts |
| `a90b69c` | Complete upstream reuse study and final 50-test verification |
| `e1ce162` | Add Agent 2 final audit corpus, aggregate runner and evidence |
| `11831a2` | Add 9-check local-only net failure and interruption test for Task 5 |
| `54280c8` | Deliver Task 6 final report with measured counts and honest limitations |
| `584f290` | Add byte-exact output comparison to deep runner and expectation fixtures |
| `1438bf2` | Fix file::read_line exact-fit classification and add uncovered Task 2 tests |
| `a8dd030` | Integrate cross-family test and fix aggregate evidence failures |
| `d45681b` | Complete compile-time safety suite integration and remaining evidence |
| `3131acf` | Add 15 deep stdlib regression tests and 46-case compile-time safety suite |
| `5fe387d` | Fix HIGH/MEDIUM safety findings from systematic audit |
| `595531f` | Deliver systematic wrapper safety audit with 127 findings and work order |

Full list: `git log --oneline -356`. This agent created no commit and staged nothing.

---

## 9. Next active task

**Land the seven remaining PENDING sibling artifacts (§§1, 4, 6), then have the
parent re-run this synthesis.** Concretely, in order:

1. Verify each with `test -f <path> && echo LANDED || echo PENDING`; regenerate
   via the owning agent's command recorded in §§1/4/6.
2. Re-read `docs/stdlib/API_INVENTORY_AGENT2.md`, the five
   `tests/defense/*2/HARDENING.md` files and `benchmarks/phase8/RESULTS.md`, and
   replace every PENDING marker with the recorded counts (keeping the
   reachability-vs-runtime and no-100%-safety caveats). Agent C's
   `docs/stdlib/UPSTREAM_REUSE_AUDIT_AGENT2.md` has landed and is already
   synthesised in §§2/3/7.
3. In parallel, escalate the `CPLUS_EXIT_ARITHMETIC` interface skew to Agent 1
   (`docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md` Options A/B); closing it
   unblocks 51 compiler tests, 58 deep-mode builds, 45 sanitizer builds and the
   5 blocked benchmark rows. Only then re-run the canonical suites and update
   §2/§6 with fresh numbers.

*End of report. Verdict: PARTIALLY COMPLETE — all runnable library tests in the
canonical strict/no-sanitizer runner pass and every fixed defect has a
regression, but compiler-dependent gaps, open findings, untested platform
modules, unsupported sanitizers, a broader audit's 11 non-passes and unverified
remote/Windows gates remain.*
