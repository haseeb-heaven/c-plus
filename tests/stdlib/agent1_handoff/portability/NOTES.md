# P2 — Linux, macOS and Windows verification (actual evidence notes)

Handoff note, Agent 1. Scope: record **what jobs exist**, **which OSes are really
exercised locally vs only in CI**, **which Windows x86 DLL checks exist**, **which
results are UNVERIFIED remotely**, and the **exact reason ASan is unavailable on
this host**. No remote/CI result is claimed as green anywhere in this note.

## 0. Provenance of this note

| Item | Value |
| --- | --- |
| Document | `tests/stdlib/agent1_handoff/portability/NOTES.md` (only file written by this agent) |
| Repository | `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` |
| Branch / HEAD at inspection | `agent/modern-stdlib` / `a90b69c` |
| Host actually measured here | `uname -m = arm64`; `Apple clang version 16.0.0 (clang-1600.0.26.6)`, target `arm64-apple-darwin27.2.0` |
| Method | Read the named source documents and verified on-disk paths, job names, toolchain and branch with read-only shell commands. **No build, test, compiler or runtime was re-run by this agent**; every "VERIFIED" below means *a recorded observation in the cited document*, not a fresh run performed for this note and never a remote result. |
| Status legend | **VERIFIED** = observed locally per the cited evidence; **UNVERIFIED** = no observation/artifact exists (default for every remote gate); **BLOCKED** = cannot be attempted in this environment or toolchain. |

## 1. Requested paths vs paths that actually exist

The handoff brief named several paths that are not present at those locations in
this repository. This matters for the gap list, so it is recorded verbatim:

| Path named in the brief | Actual on-disk state | Evidence |
| --- | --- | --- |
| `.github/workflows/*.yml` | One file only: `.github/workflows/libc-crt.yml`. No macOS job, no Windows job, no matrix strategy. | `glob .github/workflows/*.yml` (hidden included) |
| `tools/build-win32-dll.sh` | **Absent.** `tools/` contains only `verify.sh`. | `read tools`; `glob **/build-win32-dll.sh` → no files |
| `docs/LIMITATIONS.md` | Present, read in full. | 278 lines; UAF/ASan discussion at lines 43, 58–59, 139–143, 274 |
| `docs/stdlib/agent2/SANITIZER_MATRIX.md` | **Absent at that path.** Actual file: `tests/stdlib/agent2/SANITIZER_MATRIX.md` (302 lines). | `glob **/SANITIZER_MATRIX.md` |
| `docs/libc/REMOTE_GATE_EVIDENCE.md` | Present, read in full. Explicitly a *template, not a result*. | 280 lines; header lines 1–18 |
| `AGENTS.md` | **Absent from this repository.** The only `AGENTS.md` is the parent guide `/Users/haseeb-mir/Documents/Code/AGENTS.md`, which is the *Project I.G.I. Mod Base* agent guide (a different project). It prescribes `tools/build-win32-dll.sh --release --target <target>`, the Unicorn harness and "do not claim live-game success from a build or emulator result alone" — none of those scripts/rules belong to this repo's CI. | `glob **/AGENTS.md`; parent file exists (2001 bytes) |

## 2. Jobs that actually exist (`.github/workflows/libc-crt.yml`)

Workflow name: `libc-crt`. Triggers: `push` on `develop`, `agent/modern-stdlib`,
`agent/core-safety`; `pull_request` on `develop`. **Three jobs, all
`runs-on: ubuntu-latest`:**

| Job | Line | Runs-on | Steps (as defined) |
| --- | --- | --- | --- |
| `linux-crt` | 10–102 | `ubuntu-latest` | Install clang/cmake/ninja; build safe CRT; CRT unit + all checks; libc tests; **"Libc line coverage (>= 80% gate)"** (lines 24–41); attack/corpus/scenario/method suites; conformance; Rust-vs-C+; install smoke; compat matrix; external SDK consumer; fuzz smoke (UBSan); CTL/yyjson/exception/runtime tests; stdlib compile-time suite; libc fuzz; combined fuzz; safety scenario matrix (UBSan); **"ASan+UBSan+TSan smoke"** (lines 88–102, also builds a TSan binary at lines 101–102) |
| `asan-libc-crt` | 108–117 | `ubuntu-latest` | `CPLUS_CLANG: clang`; install clang; **`sh tests/crt/asan/run_asan.sh`**. No `ASAN_OPTIONS` suppressions; ASan errors intentionally fail the build (comment lines 104–107). |
| `stdlib-deep-family-integration` | 124–139 | `ubuntu-latest` | `CPLUS_CLANG: clang`; install clang/cmake/ninja; `tests/stdlib/run_deep_tests.sh`; `tests/stdlib/compile_time/run_compile_time.sh`; `tests/family_gaps/run_family_tests.sh`; `tests/integration/run_integration.sh`. No suppressions, no relaxed gates (comment lines 119–123). |

Corroboration: `docs/libc/REMOTE_GATE_EVIDENCE.md` §3.1 also names exactly these
three jobs and warns that older docs (`docs/GAP_ANALYSIS.md`,
`docs/CRITICAL_ISSUES_RESPONSE.md`, `docs/libc/COVERAGE.md`) mention only two.

**No job exists for macOS or Windows.** There is no Windows runner, no Windows
toolchain step and no DLL artifact step in this workflow.

## 3. Which OSes are actually exercised

- **macOS arm64 only, locally.** `tests/stdlib/agent2/SANITIZER_MATRIX.md` §1 records
  host `Darwin ... RELEASE_ARM64_T81 / arm64`, backend `/usr/bin/clang` Apple clang 16,
  driver `$ROOT/build/c+`. `docs/STATUS.md` lines 37 and 69 mark native executable
  generation and the 8-check end-to-end integration suite as "macOS arm64 verified
  here" / "macOS only". `docs/LIMITATIONS.md` line 232 records benchmark hardware as
  arm64 macOS, Apple clang 16.
- **Linux, CI only.** `ubuntu-latest` is the sole runner for all three jobs. There
  is no Linux runner in this environment (`docs/STATUS.md` line 49: "Linux x86-64 /
  ARM64 targets verified = no … no Linux runner in this environment";
  `docs/libc/REMOTE_GATE_EVIDENCE.md` §1 row 1).
- **Windows, nowhere.** No CI job and no local Windows toolchain/target
  (`docs/STATUS.md` line 50 "Windows x86-64 target = no / roadmap";
  `docs/libc/REMOTE_GATE_EVIDENCE.md` §1 row 3). Windows x86 ABI has no evidence
  (`docs/STATUS.md` lines 77–79: `abi_test` is NO EVIDENCE for **644 of 646**
  declared APIs).

## 4. Windows x86 DLL checks that exist

All Windows DLL checks are **external/prescribed**, not runnable here:

1. The parent (different-project) guide prescribes
   `tools/build-win32-dll.sh --release --target <target>` with paths supplied by
   environment variables or arguments. That script is **not in this repository's
   `tools/`**, so it cannot be executed locally. `docs/libc/REMOTE_GATE_EVIDENCE.md`
   §1 row 3 and §2.4 state this explicitly.
2. `docs/libc/REMOTE_GATE_EVIDENCE.md` §2.4 and §3.3 define the required evidence
   fields, all still placeholders: `<<WIN32_BUILD_COMMAND>>`, `<<WIN32_TARGET>>`
   (target triple / arch), `<<WIN32_BUILD_EXIT>>`, `<<WIN32_DLL_PATH>>` +
   `<<WIN32_DLL_SHA256>>`, `<<WIN32_BUILD_LOG_URL>>`. Gate condition: exit 0 **and**
   a DLL artifact for the PR #17 head.
3. Explicit ABI caveat (§3.3): a successful DLL build **alone does not close the
   ABI-evidence gap** (644/646 APIs have no `abi_test` evidence; no raw game-memory
   offsets to be added where an `IGI::` wrapper exists, per the parent guide).
4. Live-game/full-consumer checks are a separate external gate (§2.5, §3.4):
   `<<UNICORN_RESULT>>`/`<<UNICORN_TRACE_URL>>`,
   `<<LIVE_GAME_RESULT>>`/`<<LIVE_GAME_TRACE_URL>>`,
   `<<FULL_LIBC_CONSUMER_RESULT>>`. Harness first, then game/Wine; a build or
   emulator run alone is not live-game success.
5. `docs/libc/REMOTE_GATE_EVIDENCE.md` rollback item 3 (line 228–231) covers a
   failing Windows build but does not record a passing one.

No Windows DLL build result, artifact, SHA256 or log exists in any inspected file.

## 5. Verification table

Status is one token per row. Where a row is local-only, its CI column says "none"
and its Status is VERIFIED **for local scope only**; this must not be read as
cross-platform verification. UNVERIFIED means no observation exists (especially
for every remote gate). BLOCKED means the check cannot be attempted here.

| Check | Local evidence | CI job | Status |
| --- | --- | --- | --- |
| macOS arm64 native deep-stdlib build + run (9 tests, strict baseline) | `tests/stdlib/agent2/SANITIZER_MATRIX.md` §4/§6.1: 9/9 `strict_none` builds exit 0, runs exit 0, byte-exact `.expected`/`.stderr.expected`; doc HEAD `11831a2`. `docs/STATUS.md` line 69: E2E integration 8 checks, macOS only. Recorded, not re-run here. | none (no `macos-*` job in `libc-crt.yml`) | VERIFIED |
| UBSan deep stdlib on this host | `tests/stdlib/agent2/SANITIZER_MATRIX.md` §4/§6.1: `--strict --sanitize=undefined` 9/9 PASS; `docs/libc/COVERAGE.md` lines 109–110 recorded pass on Apple clang 16/arm64 with `-fsanitize=undefined -fno-sanitize-recover=all`. | `linux-crt` (UBSan steps), `stdlib-deep-family-integration` | VERIFIED |
| UBSan on Linux | none (no Linux runner on this host) | `linux-crt`, `stdlib-deep-family-integration` | UNVERIFIED |
| ASan via default backend/driver on this host | `tests/stdlib/agent2/SANITIZER_MATRIX.md` §3/footnote 1/§6.3: all 9 `--sanitize=address` builds exit 1, no binary, no run; verbatim: `error: sanitizer 'address' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe`. `docs/LIMITATIONS.md` lines 139–143 and `docs/libc/ASAN_AUDIT.md` §3.1/§4. | `asan-libc-crt` (Linux only) | BLOCKED |
| ASan API-level allocator audit on this host (Homebrew clang 23 + explicit `-isysroot`) | `docs/libc/COVERAGE.md` lines 111–120 and `docs/libc/ASAN_AUDIT.md` §3.2/§4: 12 API-level allocator scenarios classify correctly (`heap-buffer-overflow`, `heap-use-after-free`, clean valid cases). API-level only, not a language guarantee and not remote. | `asan-libc-crt` (different, Linux allocator integration path) | VERIFIED |
| ASan allocator integration on Linux | none locally | `asan-libc-crt` → `sh tests/crt/asan/run_asan.sh` | UNVERIFIED |
| TSan on this host | `docs/libc/COVERAGE.md` lines 121–123: Apple clang 16 needs `DYLD_INSERT_LIBRARIES=.../libclang_rt.tsan_osx_dynamic.dylib` and reports "Interceptors are not working" without it; driver probe rejects `--sanitize=thread` (`SANITIZER_MATRIX.md` §3). | `linux-crt` "ASan+UBSan+TSan smoke" (lines 101–102) | BLOCKED |
| TSan on Linux | none locally | `linux-crt` (lines 101–102) | UNVERIFIED |
| Leak / Memory / CFI sanitizers via driver on this host | `tests/stdlib/agent2/SANITIZER_MATRIX.md` §3/§7: build exit 1, no run for `leak`, `memory`, `cfi` (same probe rejection as `address`); 45 of 72 configs rejected overall. | none defined for `memory`, `cfi` or `leak` | BLOCKED |
| Integer sanitizer deep stdlib on this host | `tests/stdlib/agent2/SANITIZER_MATRIX.md` §4/§6.2/§7: probe accepted; 5/9 PASS byte-exact, 4 genuine findings exit 134 (`cplus_runtime.c:174:5: runtime error: unsigned integer overflow … uintptr_t`, `-fno-sanitize-recover=all`). Not a host abort; unresolved. | none | BLOCKED |
| `linux-crt` build + test suite (CRT/libc/stdlib/conformance/fuzz/compat/SDK) on Linux | none (no Linux runner); local clean-export build `CLEAN_EXPORT_BUILD_OK` is macOS-only and is not a remote result | `linux-crt` | UNVERIFIED |
| Libc line coverage >= 80% gate on Linux | local recorded 91.74% exists but `docs/libc/REMOTE_GATE_EVIDENCE.md` §2.2 forbids copying it into the remote slot | `linux-crt` "Libc line coverage (>= 80% gate)" | UNVERIFIED |
| Deep-stdlib / compile-time / family-gap / end-to-end integration on Linux | none locally (listed as macOS-only in `docs/STATUS.md` line 69) | `stdlib-deep-family-integration` | UNVERIFIED |
| SonarCloud quality gate | no token/credentials; BEFORE 23.4% / D / E are historical external inputs from `docs/libc/SONAR_TRIAGE.md` §1, not re-scanned; AFTER unmeasured | not part of `libc-crt.yml` (separate remote scanner) | UNVERIFIED |
| Windows x86 DLL build, local prescribed script | `tools/build-win32-dll.sh` absent; `tools/` has only `verify.sh`; no Windows toolchain/target; parent guide's command cannot run here | no Windows job | BLOCKED |
| Windows x86 DLL build, remote/external artifact | no command, target, exit code, DLL path/SHA256 or log recorded — all placeholders in `docs/libc/REMOTE_GATE_EVIDENCE.md` §2.4/§3.3 | no Windows job | UNVERIFIED |
| Windows x86 ABI evidence (`abi_test`) | `docs/STATUS.md` lines 77–79: NO EVIDENCE for 644 of 646 declared APIs; Windows x86 ABI unverified locally | none | UNVERIFIED |
| Unicorn harness + game/Wine injected DLL + full-libc consumer | none locally; external gates only (`docs/libc/REMOTE_GATE_EVIDENCE.md` §2.5/§3.4) | none | UNVERIFIED |
| External SDK consumer (clean prefix, `find_package`) | no local observation in the inspected documents | `linux-crt` "External SDK consumer (clean prefix, find_package)" | UNVERIFIED |
| Local clean-export build used as if it were a remote gate | `docs/libc/CI_RUN128_FIX_EVIDENCE.md` `CLEAN_EXPORT_BUILD_OK` produced locally with `git archive HEAD | tar -x -C /tmp/ci128-sim` on macOS arm64 (`REMOTE_GATE_EVIDENCE.md` §5) | none | UNVERIFIED |

## 6. Exact reason ASan is unavailable on this host

There are two distinct layers; both are recorded, and the nuance matters because
ASan is *not* globally impossible on this machine.

1. **Default backend / C+ driver layer (primary reason for the matrix result).**
   The driver's own compile-and-run probe rejects the sanitizer with the verbatim
   message, for every one of the nine selected deep tests:
   `error: sanitizer 'address' is not supported by the selected backend (/usr/bin/clang); detected by a compile-and-run probe`.
   Build exit 1, no executable, no run (`tests/stdlib/agent2/SANITIZER_MATRIX.md`
   §3, footnote 1, §6.3, §7). The same applies to `leak`, `thread`, `memory`,
   `cfi` — 45 of 72 configurations rejected. The selected backend is
   `/usr/bin/clang` (Apple clang 16).
2. **Host toolchain runtime layer (underlying cause).** Apple clang 16 on macOS
   arm64 aborts **during ASan initialisation, before main**, with
   `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`
   (`docs/LIMITATIONS.md` lines 139–143: "even a trivial program aborts during
   initialisation"; `docs/libc/COVERAGE.md` lines 116–118;
   `tests/crt/asan/run_asan.sh` lines 29–31). The runner detects this via a grep
   for `CHECK failed: sanitizer_malloc_mac.inc`, `Interceptors are not working`
   or `ASan runtime does not come first` and prints
   `HOST_UNSUPPORTED: ASan runtime cannot initialize on this host`, explicitly
   labelled a platform limitation, not a test failure (`run_asan.sh` lines
   240–249). `docs/libc/ASAN_AUDIT.md` §3.1 confirms `-shared-libasan` and
   `ASAN_OPTIONS` do not help with Apple clang 16.
3. **Working exception (do not overstate).** ASan *does* work here with
   **Homebrew clang 23 plus explicit
   `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`** (no such
   sysroot makes the link fail on missing `MacOSX27.sdk`). That path verified 12
   API-level allocator scenarios (`docs/libc/ASAN_AUDIT.md` §3.2/§4;
   `docs/libc/COVERAGE.md` lines 111–120). `docs/LIMITATIONS.md` lines 263–266
   also notes Homebrew LLVM's `clang` binary otherwise fails at runtime with a
   missing `@rpath/libLLVM.23.1.dylib`, which is why it is not the default
   backend. The default remains `/usr/bin/clang`, so the driver-level ASan
   result is BLOCKED; the 12-scenario Homebrew result is a narrow local audit,
   **not** a language guarantee, not use-after-free detection in safe C+
   (`docs/LIMITATIONS.md` lines 43 and 58–59), and **not** a remote gate result.

## 7. Explicitly UNVERIFIED remotely — no green claims

Every item below is unobserved; remote placeholders from
`docs/libc/REMOTE_GATE_EVIDENCE.md` are not evidence:

- Linux CI: all three jobs (`linux-crt`, `asan-libc-crt`,
  `stdlib-deep-family-integration`), the Checks-tab `libc-crt` conclusion, the
  coverage-gate conclusion and run URL/ID/head SHA — `UNVERIFIED`.
- SonarCloud: gate status, scan URL, CPD, security and reliability ratings,
  issue triage — `UNVERIFIED` (no credentials; BEFORE values historical only).
- Windows x86 DLL: build command, target, exit code, artifact path + SHA256,
  build log — `UNVERIFIED` (and locally BLOCKED because the prescribed script
  is absent).
- Live game / Wine / Unicorn / full-libc consumer: every trace field —
  `UNVERIFIED`; a DLL build or emulator result alone is never live-game success.
- `docs/libc/REMOTE_GATE_EVIDENCE.md` §5 non-claims reiterated: local
  clean-export builds are not remote green; local ASan/UBSan/TSan/fuzz results
  are not remote green (ASan only 12 scenarios with Homebrew clang 23; TSan NOT
  VERIFIED here); all suite counts (libc 83/83, CRT 79/79, 91.74% local
  coverage, 600-case comparison, etc.) are local recordings; prior Sonar
  numbers are historical; no Windows DLL or injected-DLL success is claimed.

## 8. Headline gaps

1. **Linux is CI-only.** All three `ubuntu-latest` jobs exist, but none has been
   observed locally or remotely; Linux x86-64/ARM64 verification is absent.
2. **macOS arm64 is locally verified but has no CI job** — local passes cannot be
   generalised to Linux or Windows.
3. **Windows is unexercised end-to-end.** No CI job, no local toolchain, and the
   prescribed `tools/build-win32-dll.sh` is not in this repository; all DLL
   evidence fields remain placeholders. ABI evidence is missing for 644/646 APIs.
4. **ASan is BLOCKED for the default C+ driver/backend** by the probe, rooted in
   Apple clang 16's `sanitizer_malloc_mac.inc:189` startup abort; only a narrow
   Homebrew-clang-23 API-level audit works locally, and no ASan/UBSan/TSan CI
   job has been observed green.
5. **Four unresolved integer-sanitizer findings** (exit 134 at
   `cplus_runtime.c:174:5`) mean even the sanitizer that *does* run is not clean
   across all nine deep tests.
6. **Remote gates are all placeholders**, including SonarCloud and live-game;
   no document in the inspected set records a green remote build.
7. **Path drift in the handoff brief** (`docs/stdlib/agent2/...`,
   `tools/build-win32-dll.sh`, in-repo `AGENTS.md`) risks citing evidence that
   does not exist at those paths; this note records the real locations above.
