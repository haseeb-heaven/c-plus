# Capability matrix

`Implemented` means code exists in the compiler/runtime. `Tested` means an
automated case exists. `Verified` means the behaviour was actually observed in
the repository's test/demo runs. "partial" is never counted as complete.

| feature | implemented | tested | verified | known limitations |
| --- | --- | --- | --- | --- |
| Memory safety (supported subset) | yes | yes | yes | no UAF detection, no pointer-arithmetic checks; ASan verified only for 12 API-level allocator scenarios via Homebrew clang 23, not as a language guarantee (Apple clang 16 unusable here). See `docs/libc/COVERAGE.md`. |
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
| libc tests (`tests/libc/run_libc_tests.sh`) | 83 | 83 pass, 0 fail |
| CRT unit (`tests/crt/unit/run_unit_tests.sh`) | 79 | 79 pass, 0 fail |
| Rust-vs-C+ paired runtime (6 families x 100) | 600 | 473 matched, 119 documented differences, 8 both-skip, 0 failures |
| Per-family checked suites (10 sources x plain+UBSan) | 20 variants | runner reports pass/fail per variant |
| Hundred-scenario per-method suites | 30 variants | 30/30 pass |
| Compile-time suite (7 manifests) | 46 cases | 9 REJECT, 17 ACCEPT, 20 RUNTIME (RUNTIME = documented compiler gap, covered by runtime suites) |
| Deep stdlib tests (15 sources x 3 modes) | 45 variants | runner reports pass/fail/skip per mode |
| End-to-end integration suite | 8 checks | local driver+compiler+libc+SafeCRT run, macOS only |

Honest limits — do not read the `verified` column as completeness or as a remote result:

- **27 APIs MISSING** and **49 UNSUPPORTED** in `docs/libc/iso_c_inventory.json`
  (948 APIs, 27 headers). No 100% ISO C claim.
- **387 of 437 vendored safeclib public APIs NOT_INTEGRATED** (only 40 integrated,
  10 adapter-only); 188 upstream `.c` files are uncompiled.
- **ABI evidence limited**: `abi_test` is NO EVIDENCE for 644 of 646 declared
  APIs; only `tests/cases/c_abi.c` and `tests/compat/` exist. Windows x86 ABI is
  unverified locally.
- **127 open wrapper-safety audit findings** (16 High, 68 Medium, 43 Low/Other)
  in `docs/libc/WRAPPER_SAFETY_AUDIT.md`.
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
sh tests/libc/run_libc_tests.sh                      # 83 libc tests
sh tests/crt/unit/run_unit_tests.sh                  # 79 CRT unit tests
sh tests/rust_vs_cplus/run_compare.sh                # 600 cases, 6 families
sh tests/family_gaps/run_family_tests.sh             # 20 variants
sh tests/methods/run_hundred.sh                      # 30 variants
sh tests/stdlib/compile_time/run_compile_time.sh     # 7 manifests, 46 cases
sh tests/stdlib/run_deep_tests.sh                    # 15 sources, 45 variants
sh tests/integration/run_integration.sh              # 8 checks
sh tests/run_all_evidence.sh                         # aggregate entry point
```
