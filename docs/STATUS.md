# Capability matrix

`Implemented` means code exists in the compiler/runtime. `Tested` means an
automated case exists. `Verified` means the behaviour was actually observed in
the repository's test/demo runs. "partial" is never counted as complete.

| feature | implemented | tested | verified | known limitations |
| --- | --- | --- | --- | --- |
| Memory safety (supported subset) | yes | yes | yes | no UAF detection, no pointer-arithmetic checks; ASan is UNSUPPORTED on the final Agent 2 sanitizer-matrix backend (`/usr/bin/clang`, Apple clang 16 / arm64 probe rejection) — 0 of 9 selected tests executed under ASan. An earlier Homebrew-clang subset result is not a language guarantee. See `docs/libc/COVERAGE.md` and `tests/stdlib/agent2/SANITIZER_MATRIX.md`. |
| Compile-time bad array index | yes | yes | yes | constant and simple counted loops only |
| Compile-time null pointer dereference | yes | yes | yes | Clang AST constant-initializer tracking |
| Runtime bounds/null/alignment checks | yes | yes | yes | UBSan-based; aborts on first violation |
| Return-stack-address / uninitialized checks | yes | yes | yes | Clang warnings promoted to errors |
| Pointer-leak / raw realloc / strict-aliasing checks | yes | yes | yes | token-level static pass |
| String-literal write / sizeof decay / format security | yes | yes | yes | Clang warnings promoted to errors |
| Unsequenced / shift / sign-compare checks | yes | yes | yes | Clang warnings promoted to errors |
| memcpy overlap / direct data-race checks | yes | yes | yes | token-level static pass (conservative) |
| Pointer arithmetic / pointer compare / signal / ctype / I/O checks | yes | yes | yes | static pass + promoted Clang warnings |
| setjmp/longjmp, NaN, VLA sizeof, macro, zero-alloc, signal-flag checks | yes | yes | yes | static pass + promoted Clang warnings |
| C23-specific unsafe constructs rejected / auto made qualifier-safe | yes | yes | yes | static pass + __typeof__ lowering |
| C2y-style defer / countof / if-declarations / 0o literals | partial | yes | yes | see docs/C2Y.md; no full C2y backend |
| fork/restrict/bit-field/fflush/VLA/defer-control checks | yes | yes | yes | static pass |
| Double/invalid release detection | yes | yes | yes | stale pointer after address reuse may be misattributed |
| RAII: scope exit / return / break / continue | yes | yes | yes | `goto`/`longjmp`/`exit` bypass cleanup |
| RAII reverse destruction order | yes | yes | yes | one owner per declaration |
| Ownership transfer (`cplus_move`, return) | yes | yes | yes | resource locals only |
| Structured borrow checking | partial | yes | yes | no aliases in containers/fields; no lifetime annotations |
| Classes: fields + methods | yes | yes | yes | no virtual dispatch, inheritance or access control |
| Class constructors | yes | yes | yes | no return type, no early return |
| Class destructors + RAII | yes | yes | yes | destructor must tolerate zeroed handle |
| Lightweight `class` sugar (`p.m()`) | yes | yes | yes | receiver class must be visible in the same TU |
| Managed allocation + `.len` | yes | yes | yes | `.len` folds when allocation visible; pointer args fall back to side table |
| Fixed array `.len` | yes | yes | yes | compile-time literal |
| C compatibility / C ABI / libc + POSIX | yes | yes | yes | C17 + GNU extensions; legacy C not safe |
| `.cp` / `.hp` extensions | yes | yes | yes | `.c`/`.h` still accepted; header lowering per TU |
| `.hp` standard library layer | yes | yes | yes | thin wrappers over libc/POSIX; not a libc rewrite |
| Native executable generation | yes | yes | yes | host Clang driver; macOS arm64 verified here |
| `--legacy-c` mode | yes | yes | yes | explicitly **not** memory safe |
| Format-free `print` / `scan` | yes | yes | yes | no struct reflection; printer state is not thread-safe |
| Default function arguments | yes | yes | yes | same translation unit only |
| Range-based `for` | yes | yes | yes | simple lvalue ranges with `.len` only |
| Safe concurrency (mutex + TLS + threads) | partial | yes | yes | no race detection; feature is experimental |
| Networking demo (sockets) | yes | yes | yes | uses existing POSIX socket API |
| Reproducible benchmarks | yes | yes | yes | 8 microbenchmarks, local machine only |
| GC / managed object graph | no | — | — | out of scope; scope-based allocator instead |
| Full Rust-style borrow checker | no | — | — | roadmap |
| Data-race detection / Sendable types | no | — | — | roadmap |
| Incremental build cache / dependency tracking | no | — | — | roadmap |
| Linux x86-64 / ARM64 targets verified | no | — | — | no Linux runner in this environment |
| Windows x86-64 target | no | — | — | roadmap |
| Production backend/database/game-engine frameworks | no | — | — | target workloads, not claims |

## Coverage / test-count reconciliation (2026-11-23)

Case counts below were re-derived from the on-disk sources and runners; the
pass/fail figures are recorded results in the named documents, not re-executed
by this reconciliation. Full accounting lives in `docs/libc/COVERAGE.md`.

| Suite | Count | Recorded result |
|---|---:|---|
| C+ compiler tests (`tests/run_tests.sh`) | 150 | 149 pass, 1 pre-existing warning-only case |
| libc tests (`tests/libc/run_libc_tests.sh`) | 85 test sources (97 `test_*.c` minus 12 excluded adversarial/corpus files) | 85 pass, 0 fail |
| CRT unit (`tests/crt/unit/run_unit_tests.sh`) | 80 test sources | 80 pass, 0 fail |
| Rust-vs-C+ paired runtime (6 families x 100) | 600 | 473 matched, 119 documented differences, 8 both-skip, 0 failures |
| Per-family checked suites (10 sources x plain+UBSan) | 20 variants | runner reports pass/fail per variant |
| Hundred-scenario per-method suites | 30 variants | 30/30 pass |
| Compile-time suite (8 manifests) | 52 cases | 6 of 9 REJECT as expected, 19 of 19 ACCEPT, 24 of 24 RUNTIME; 27 of 52 cases are capability/tagging gaps, reported but not counted as passes |
| Deep stdlib tests (28 sources x 3 modes) | 84 invocations | 84 of 84 passed, 0 failed, 0 skipped, with byte-exact stdout/stderr fixtures (`tests/stdlib/run_deep_tests.sh`) |
| Agent 2 aggregate runner (`sh tests/stdlib/agent2/run_all.sh --clean`) | 50 tests (28 deep + 11 `agent2/*.cp` + 11 curated existing) | 50 of 50 PASS; 0 FAIL, 0 BUILD_FAILURE, 0 TIMEOUT, 0 SKIPPED, 0 BLOCKED |
| Aggregate evidence (`tests/run_all_evidence.sh`) | 23 suites | 23 of 23 passed, 0 failed |
| End-to-end integration suite | 8 checks | local driver+compiler+libc+SafeCRT run, macOS only |

Honest limits — do not read the `verified` column as completeness or as a remote result:

- **27 APIs MISSING** and **49 UNSUPPORTED** in `docs/libc/iso_c_inventory.json`
  (948 APIs, 27 headers). No 100% ISO C claim.
- **387 of 437 vendored safeclib public APIs NOT_INTEGRATED** (only 40 integrated,
  10 adapter-only); 188 upstream `.c` files are uncompiled.
- **ABI evidence limited**: `abi_test` is NO EVIDENCE for 644 of 646 declared
  APIs; only `tests/cases/c_abi.c` and `tests/compat/` exist. Windows x86 ABI is
  unverified locally.
- **127 wrapper-safety audit findings** in `docs/libc/AUDIT_FINDINGS_TRACKER.md`:
  17 of 127 FIXED, 46 of 127 OPEN, 7 of 127 GAP-COMPILER, 4 of 127 BLOCKED and
  53 of 127 DOCUMENTED-CONTRACT (severity: 16 High, 68 Medium, 43 Low/Other).
  Only the 46 OPEN rows are genuinely remaining runtime work.
- **Sanitizer matrix** (`tests/stdlib/agent2/SANITIZER_MATRIX.md`): 72 builds
  attempted (9 tests x 8 configurations); 27 of 72 accepted by the driver probe
  and executed; 23 of 27 PASS, 4 of 27 FAIL under `--sanitize=integer` (genuine
  unsigned-overflow report in runtime hashing, outside Agent 2 scope); 45 of 72
  rejected at build time as UNSUPPORTED (ASan, Leak, Thread, Memory, CFI).
- **Remote CI UNVERIFIED**: Linux `linux-crt` / `asan-libc-crt` jobs and any
  GitHub Actions result must be observed after push; local clean-export builds
  are not remote green results.
- **SonarCloud UNVERIFIED**: no local token/credentials; the external 23.4%
  duplication / security D / reliability E ratings were not re-scanned and no
  gate pass is claimed (`docs/libc/SONAR_TRIAGE.md`).
- Passing tests are evidence of observed behaviour only (TESTED != PROVEN), not
  absence of UB, memory safety, or Rust equivalence. No live-game/injected-DLL
  result is claimed from any of these runners.

Commands that produced the "verified" column (results recorded, not re-run here;
run from the repository root):

```sh
ctest --test-dir build --output-on-failure           # 2/2 suites
sh tests/run_tests.sh                                # 150 compiler tests (149 pass, 1 warning-only)
sh tests/libc/run_libc_tests.sh                      # 85 libc tests
sh tests/crt/unit/run_unit_tests.sh                  # 80 CRT unit tests
sh tests/rust_vs_cplus/run_compare.sh                # 600 cases, 6 families
sh tests/family_gaps/run_family_tests.sh             # 20 variants
sh tests/methods/run_hundred.sh                      # 30 variants
sh tests/stdlib/compile_time/run_compile_time.sh     # 8 manifests, 52 cases (27 gaps)
sh tests/stdlib/run_deep_tests.sh                    # 28 sources, 84 invocations
sh tests/stdlib/agent2/run_all.sh --clean            # 50 runner tests
sh tests/integration/run_integration.sh              # 8 checks
sh tests/run_all_evidence.sh                         # 23 suites, aggregate entry point
```

## Agent 2 stdlib safety-hardening snapshot (2026-11-23)

This section records the current, honest state of the stdlib hardening work.
Source of truth: `tests/stdlib/agent2/FINAL_AUDIT.md`,
`docs/stdlib/TASK6_FINAL_REPORT.md`, `docs/stdlib/REGRESSION_INVENTORY.md`,
`docs/libc/AUDIT_FINDINGS_TRACKER.md`,
`tests/stdlib/agent2/results/summary.txt` and
`tests/stdlib/agent2/SANITIZER_MATRIX.md`. Section 3 of that last document
records the sanitizer outcome; §4 of `AGENT1_PRIORITY_HANDOFF.md` records the
runtime checks that these priorities are meant to replace.

### Branch / PR state

- Branch `agent/modern-stdlib`; **PR #17 open**. Nothing pushed or merged from
  this work. The final phase began at HEAD `11831a2`; the aggregate summary is
  stamped `0261270`; the findings tracker was generated at the `595531f`
  baseline. These are different point-in-time snapshots, not one commit.
- **Agent 2 has stopped adding new stdlib surface.** No new stdlib modules,
  syntax, GUI wrappers or networking libraries were added in this cycle. The
  focus has moved to compiler-enforced safety, which is Agent 1 work; Agent 2
  supplies reproducers, contracts and regression fixtures only.

### Owner's priority order (compiler-enforced safety)

| Priority | Engineering requirement | Agent 2 evidence / reproducer directory |
| --- | --- | --- |
| P0 | Complete UAF and double-free analysis | `AGENT1_COMPILER_GAPS.md` §2; `COMPILE_TIME_GAPS.md` 27 gaps; `string_double_drop.cp` tagged `[CPLUS-OWNER]` only for managed pointers — `tests/stdlib/agent1_handoff/uaf_double_free/` |
| P0 | CFG-based ownership analysis | Compile-time cases accept use-after-drop and two mutable aliases — `tests/stdlib/agent1_handoff/cfg_ownership/` |
| P0 | Pointer arithmetic and bounds verification | Only fixed-array literal subscripts get `[CPLUS-BOUNDS]` — `tests/stdlib/agent1_handoff/pointer_bounds/` |
| P0 | Sound alias analysis | `vector_mutable_alias.cp`, `collections_mutable_alias.cp` accepted — `tests/stdlib/agent1_handoff/alias_analysis/` |
| P0 | Cross-function lifetime analysis | `string_dangling_view.cp`, `json_dangling_ref.cp` accepted — `tests/stdlib/agent1_handoff/cross_function_lifetime/` |
| P1 | Cross-translation-unit analysis | Compile-time suite is single-TU only — `tests/stdlib/agent1_handoff/cross_tu/` |
| P1 | Definite initialization on every path | `[CPLUS-LIFETIME] uninitialized` on direct variables, silent on branch/path cases — `tests/stdlib/agent1_handoff/definite_init/` |
| P1 | Strict-safe mode (reject unproven operations) | `--strict` differs mainly by sanitizer settings; no proof-of-safety gate — `tests/stdlib/agent1_handoff/strict_safe_mode/` |
| P1 | Thread-safety analysis | `thread.hp` has runtime guards; no static data-race analysis — `tests/stdlib/agent1_handoff/thread_safety/` |
| P2 | Fuzzing and compiler differential testing | stdlib fuzz 1200 / libc fuzz 4800 / Rust-vs-C+ 600 cases; no false-negative measurement — `tests/stdlib/agent1_handoff/fuzz_diff/` |
| P2 | Linux, macOS, Windows verification | macOS local; Linux CI jobs exist but UNVERIFIED remotely; Windows x86 DLL script only — `tests/stdlib/agent1_handoff/portability/` |
| P2 | Performance vs Clang and Fil-C | Local C+/Rust/Goose medians; Fil-C toolchain absent — `tests/stdlib/agent1_handoff/performance/` |

Every current runtime protection (UBSan bounds/null/alignment, the allocation
side table, `cplus_check_live()`, RAII cleanup) is dynamic or opt-in; none is a
substitute for the P0 compiler enforcement above. See
`docs/libc/AGENT1_PRIORITY_HANDOFF.md` Appendix A for the exact inventory.

### Recorded results (counts with denominators)

| Area | Count with denominator | Recorded result |
| --- | --- | --- |
| Agent 2 aggregate runner | 50 of 50 tests (28 `deep_*.cp` + 11 `agent2/*.cp` + 11 curated existing) | 50 PASS, 0 FAIL, 0 BUILD_FAILURE, 0 TIMEOUT, 0 SKIPPED, 0 BLOCKED |
| Deep stdlib runner | 28 tests x 3 modes = 84 of 84 invocations | 84 passed, 0 failed, 0 skipped; byte-exact stdout/stderr fixtures |
| Aggregate evidence | 23 of 23 suites (`tests/run_all_evidence.sh`) | 23 passed, 0 failed |
| Compile-time suite | 52 cases across 8 manifests: 6 of 9 REJECT, 19 of 19 ACCEPT, 24 of 24 RUNTIME | 27 of 52 cases are capability/tagging gaps, never counted as passes |
| Sanitizer matrix | 72 builds attempted (9 tests x 8 configurations); 27 of 72 executed | 23 of 27 PASS, 4 of 27 FAIL (`--sanitize=integer`, runtime hashing); 45 of 72 rejected at build as UNSUPPORTED |
| ASan | 0 of 9 selected tests executed under ASan | UNSUPPORTED by driver probe on `/usr/bin/clang` (Apple clang 16 / arm64); no ASan claim |

### Inventory / gaps (counts with denominators)

- **stdlib API surface**: 21 `stdlib/*.hp` headers, 381 public callable entries;
  255 of 381 have a static deep-test call-site reference (234 literal calls +
  21 compiler-inserted `drop()` credits) and 126 of 381 do not. This is a
  **static textual call-site inventory**, not execution coverage or a safety
  proof (`docs/stdlib/REGRESSION_INVENTORY.md` §7). No function-level runtime
  attribution is available on this host.
- **Audit findings**: 127 total — 17 of 127 FIXED, 46 of 127 OPEN, 7 of 127
  GAP-COMPILER, 4 of 127 BLOCKED, 53 of 127 DOCUMENTED-CONTRACT.
- **Safe CRT**: 40 of 437 APIs integrated, 10 of 437 adapter-only, 387 of 437
  not integrated (`docs/stdlib/upstream-reuse.md` §4).
- **ISO C inventory**: 27 of 948 APIs MISSING and 49 of 948 UNSUPPORTED
  (`docs/libc/iso_c_inventory.json`). No 100% ISO C claim.
- **ABI evidence**: 2 of 714 ABI matrix rows have evidence
  (`docs/stdlib/TASK6_FINAL_REPORT.md` §10); a separate older measure in this
  document records `abi_test` as no evidence for 644 of 646 declared APIs —
  different denominators/inventories, neither is a Windows x86 proof. A
  70-layout host/C+ agreement test measures layout only. No calling-convention
  evidence.

### Explicitly UNVERIFIED

Remote Linux CI, SonarCloud gates, Windows x86 ABI / DLL behaviour and
live-game (injected-DLL) results are all **UNVERIFIED**. Local green runs and
emulator results are not remote results and are not live-game success. A
sanitizer pass, where one exists, is not a memory-safety proof and is not
Rust equivalence. The overall verdict remains **PARTIALLY COMPLETE**: all
runnable library tests pass and every fix has a regression, but compiler gaps,
46 open findings, untested platform modules, unsupported sanitizers and
unverified remote gates remain.
