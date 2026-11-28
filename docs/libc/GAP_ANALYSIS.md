# Agent 2 gap analysis — next assignment

Baseline: local branch `agent/modern-stdlib` @ `4fbb2c2`, **28 commits ahead of
origin**, **11 behind develop**. The 11 develop commits touch only
`README.md` and `docs/ROADMAP.md` (verified with `git diff --stat
HEAD...origin/develop`). No merge is required for functional parity; a docs
sync can be done at release time.

Legend: **DONE** (local evidence), **PARTIAL**, **NOT VERIFIABLE HERE**, **GAP**.

## Phase 1 — verify previous work

| Item | Status | Evidence |
|---|---|---|
| Previous CI failures (`safe_lib_errno.h`) | DONE locally | Header checked in; `scripts/build_safecrt.sh:44` generates into `$OUT` with `-I$OUT`; `sh scripts/build_safecrt.sh` passes |
| Linux CI green | NOT VERIFIABLE HERE | Local host is macOS arm64; workflow has `linux-crt` + `asan-libc-crt` jobs. Must be observed on GitHub after push |
| SonarCloud D/E + 23.4% duplication | PARTIAL | Generated fixtures excluded in `sonar-project.properties`; production paths and thresholds unchanged. Remote gate unmeasured — see `SONAR_TRIAGE.md` |
| Complete existing test suite | DONE locally | libc 72/72, CRT unit 78/78, attack 23/23, corpora 20/20, scenarios 52/52, ASan 4 clean gates, SDK consumer ok, contracts valid |
| Quality gates not weakened | DONE | No rule/threshold changes; exclusions limited to generated files |

## Phase 2 — standard C API compatibility

| Item | Status | Evidence / gap |
|---|---|---|
| Installed SDK + unchanged C spellings | DONE | `cplus_libc_safe.h`, `stdlib/safe/safe_c.h`; `tests/sdk/run_sdk_consumer.sh` → `sdk consumer: ok` |
| Headers stdlib/string/stdio/stdint/stddef/errno/ctype/limits/assert | PARTIAL | All are system headers consumed unchanged; C+ adds optional overlays. No conflict diagnostics in 72 libc tests, but a dedicated C **and C++ consumer matrix per header** is missing |
| Incompatible signatures | DONE for known cases | Fixed `lroundf/llroundf` ISO return types, `fexcept_t` fenv signatures, `toupper(EOF)`; see `ADVERSARIAL_FINDINGS.md` |
| Unsupported APIs documented | DONE | `docs/libc/ISO_C_AUDIT.md`, `FUNCTION_MATRIX.md` |

**Highest-priority gap:** C and C++ consumer compile matrix for the nine listed
headers (`tests/compat/`).

## Phase 3 — memory/allocation safety

DONE (local): overflow checks, zero-size, double-free refusal, UAF heuristic
detection, bounded-capacity writes, ABA generation guards in both allocators.
Tests: `test_alloc_attack`, `corpus_uaf`, `scenario_01..11`, `test_safe_c_api`,
`test_libc_safe_api`.

Honest limits: raw `free()` of a tracked pointer is caller UB (may yield false
"live"); bounded 1024-entry recent-free caches; arbitrary alias provenance is
not provable. No claim of UAF/double-free prevention for raw C pointers.

## Phase 4 — file and I/O safety

DONE locally: `fread`/`fwrite` product-overflow guards, NULL/stream guards,
double-close refusal (`cplus_fclose_s` open addressing), fd-leak regression
fixed, format `%n`/non-literal rejection in checked APIs.
NOT VERIFIABLE: Linux-specific CRT behaviour (macOS host only).

## Phase 5 — contracts

DONE: `libc_contracts.json` stale "planned" claims replaced; `validate_contracts.py`
explicit-failure based and C17-aware; `CONTRACT_STATUS.md` lists limitations.
GAP: contracts that require Agent 1 (static bounds for runtime indices,
ownership/lifetime, FFI) are documented in `DISPATCH_BOUNDS_HANDOFF.md` and
`KNOWN_COMPILER_GAPS.md` but not enforced by this branch.

## Phase 6 — external SDK consumer

DONE: `tests/sdk/run_sdk_consumer.sh` installs into a temp prefix, builds a
separate CMake project with only `CMAKE_PREFIX_PATH`, uses `find_package`,
links `cplus::cplus_hardened_libc`, runs `sdk consumer: ok`.
NOT VERIFIABLE: Linux repeat (workflow job does not yet run this script — gap).

## Phase 7 — portability/security

DONE locally: macOS arm64 builds/tests, ASan via Homebrew clang + explicit SDK,
UBSan, TSan documented unavailable, provenance files for musl/safeclib/ctl.
GAP: Linux x86_64/arm64 evidence must come from CI; no local claim.

## Phase 8 — tests/fuzzing

DONE: unit, negative, boundary, differential fuzz (13,256 inputs), corpora
(~81k cases), ASan API-level tests, UBSan everywhere, per-defect regression
tests. IN FLIGHT: 10 per-method log/unit suites + 10 × 100-scenario suites
(agents still generating). All reproducible with fixed seeds.

## Phase 9 — documentation

DONE: `README`, `libc/README`, `CRT_MATRIX`, `COVERAGE`, `ISO_C_AUDIT`,
`FUNCTION_MATRIX`, `API_TEST_MATRIX`, `C_BUG_CLASS_MATRIX`, `SAFETY_GATES`,
`CPLUS_RUST_CONFORMANCE`, `ORDINARY_C_SAFETY`, `ASAN_AUDIT`, `SDK_CONSUMER`,
`ASSESSMENT_RESPONSE`.
GAP: no single consolidated "Implemented / Tested / Compiler-enforced /
Runtime-enforced / Planned / Unsupported" table — to be added.

## Highest-priority work order

1. C/C++ header consumer compatibility matrix (`tests/compat/`) + docs table.
2. Add Linux step for the SDK consumer to CI (currently only local).
3. Finish and integrate the 20 in-flight method suites; triage defects.
4. Consolidated status table with the six required categories.
5. Push for remote CI/Sonar evidence (cannot be done locally).
