# Remote-verification evidence template — PR #17 (`agent/modern-stdlib`)

**This is a template, not a result.** It exists so that post-push verification is
recorded honestly and reproducibly. Every remote gate below is **UNVERIFIED** as
of this writing. All `<<...>>` tokens are placeholders: replace them **only**
with values actually observed on the remote, and only after PR #17 is pushed.
If a value cannot be observed, leave the placeholder and the `UNVERIFIED` mark
in place. Do not copy a local number into a remote slot.

Scope inputs used to build this template: `.github/workflows/libc-crt.yml`,
`sonar-project.properties`, `docs/libc/CRITICAL_ISSUES_RESPONSE.md`,
`docs/STATUS.md`, `docs/libc/COVERAGE.md`.

Status legend used throughout:

- `UNVERIFIED` — no remote observation exists yet (the default for every item here).
- `<<PLACEHOLDER>>` — fill from a named remote artifact, never inferred.
- `VERIFIED` — reserved for a completed remote observation with a URL/artifact attached.

---

## 1. What CANNOT be verified locally

These four gates cannot be closed from this macOS arm64 host or from any local
build/test run. Only a remote run or a separate environment can close them.

| # | Gate | Why it cannot be verified locally | Honest local fact |
|---|---|---|---|
| 1 | **Linux CI green** | No Linux runner in this environment. The workflow `.github/workflows/libc-crt.yml` runs on `ubuntu-latest`; the host here is macOS arm64. | `docs/STATUS.md`: "Linux x86-64 / ARM64 targets verified = no … no Linux runner in this environment"; `docs/libc/COVERAGE.md` §"Remote CI is UNVERIFIED". |
| 2 | **SonarCloud quality gate** | No SonarCloud token/credentials locally; the remote scanner and gate status cannot be run or queried. | `sonar-project.properties` projectKey `haseeb-heaven_c-plus`, org `haseeb-heaven`; `docs/libc/SONAR_TRIAGE.md` §6: "Local verification of the SonarCloud gate is impossible without credentials." |
| 3 | **Windows x86 DLL build** | No Windows job in `.github/workflows/libc-crt.yml` and no Windows toolchain/target here. | `docs/STATUS.md`: "Windows x86-64 target = no / roadmap"; `docs/libc/COVERAGE.md`: "Windows x86 ABI … unverified locally"; `abi_test` = NO EVIDENCE for 644 of 646 declared APIs (`API_STATUS_MATRIX.md`). The prescribed DLL path (`tools/build-win32-dll.sh`, per the repository agent guide) is not present in this repository's `tools/` and must be treated as an external/prescribed step, not a runnable local command. |
| 4 | **Live game / full-libc consumers** | No live-game, Wine, injected-DLL, or real-world full-libc consumer execution in this environment. Only local harness/smoke runs exist. | `docs/STATUS.md`: "No live-game/injected-DLL result is claimed from any of these runners"; `docs/libc/CONTRACT_STATUS.md`: no claim about "the retail Windows executable or any injected DLL workflow." |

Corroborating statements that remote status is not verified:
`docs/libc/GAP_ANALYSIS.md` (Phase 1 "Linux CI green = NOT VERIFIABLE HERE";
Phase 6 "Linux repeat NOT VERIFIABLE"; Phase 7 "Linux x86_64/arm64 evidence
must come from CI; no local claim"), `docs/libc/CRITICAL_ISSUES_RESPONSE.md`
(Issues 5 and 6 "NOT CLAIMED"), `docs/libc/CI_RUN128_FIX_EVIDENCE.md`
("no CI result is claimed from this local evidence alone").

---

## 2. Post-push verification steps (every status UNVERIFIED)

Run these **after PR #17 is pushed**. Both the push to `agent/modern-stdlib`
and the pull request targeting `develop` trigger the `libc-crt` workflow
(triggers: push on `develop`, `agent/modern-stdlib`, `agent/core-safety`;
pull_request on `develop`).

### 2.1 Push and locate the run

- [ ] `UNVERIFIED` — Confirm PR #17 head SHA: `<<PR17_HEAD_SHA>>`
- [ ] `UNVERIFIED` — Workflow run URL: `<<LINUX_CI_RUN_URL>>`
- [ ] `UNVERIFIED` — Run triggered by: `<<PUSH_OR_PR>>` (push / pull_request)
- [ ] `UNVERIFIED` — Overall workflow conclusion: `<<WORKFLOW_CONCLUSION>>`

### 2.2 Linux CI (GitHub Actions, workflow `libc-crt`)

- [ ] `UNVERIFIED` — Open `<<LINUX_CI_RUN_URL>>`; record the run ID `<<RUN_ID>>`.
- [ ] `UNVERIFIED` — For the PR, open the Checks tab of PR #17 and record the
      `libc-crt` check-run conclusion: `<<PR_CHECK_CONCLUSION>>`.
- [ ] `UNVERIFIED` — Record each job conclusion from the run page (all must be
      `success` for CI to be considered green):
      - `linux-crt`: `<<JOB_LINUX_CRT>>`
      - `asan-libc-crt`: `<<JOB_ASAN_LIBC_CRT>>`
      - `stdlib-deep-family-integration`: `<<JOB_STDLIB_DEEP>>`
- [ ] `UNVERIFIED` — If any job failed, paste the failing step name and the
      first error line: `<<FAILED_STEP>>` / `<<FIRST_ERROR_LINE>>`.
- [ ] `UNVERIFIED` — Coverage gate step "Libc line coverage (>= 80% gate)"
      conclusion: `<<COVERAGE_GATE_CONCLUSION>>`; reported coverage
      `<<LINUX_LIBC_COVERAGE_PCT>>` (note: local recorded 91.74% is a **local**
      figure and must not be copied here).
- [ ] `UNVERIFIED` — Artifact/log links: `<<LINUX_CI_LOG_URLS>>`.

### 2.3 SonarCloud quality gate

Requires a token supplied at runtime; never commit it.

```sh
# Quality gate status of the branch (replace token via environment, never literal)
curl -sS -u "$SONAR_TOKEN:" \
  "https://sonarcloud.io/api/qualitygates/project_status?projectKey=haseeb-heaven_c-plus&branch=agent%2Fmodern-stdlib"

# Issues, paginated (repeat with p=2,3,... until empty)
curl -sS -u "$SONAR_TOKEN:" \
  "https://sonarcloud.io/api/issues/search?componentKeys=haseeb-heaven_c-plus&ps=500&p=1"
```

- [ ] `UNVERIFIED` — Gate status returned: `<<SONAR_GATE_STATUS>>`
- [ ] `UNVERIFIED` — Analysis/scan URL: `<<SONAR_SCAN_URL>>`
- [ ] `UNVERIFIED` — Measured duplication (CPD): `<<SONAR_DUP_PCT_AFTER>>`
- [ ] `UNVERIFIED` — Measured security rating: `<<SONAR_SECURITY_AFTER>>`
- [ ] `UNVERIFIED` — Measured reliability rating: `<<SONAR_RELIABILITY_AFTER>>`
- [ ] `UNVERIFIED` — Issues returned / triaged: `<<SONAR_ISSUES_COUNT>>`;
      breakdown generated-fixture-noise / real-production / false-positive:
      `<<SONAR_ISSUES_TRIAGE>>`
- [ ] `UNVERIFIED` — Confirm production paths still analysed (no new exclusions):
      `grep -nE 'libc/src|libc/log|stdlib/safe' sonar-project.properties`
      → `<<PRODUCTION_SCOPE_CHECK>>`

### 2.4 Windows x86 DLL build (external/prescribed environment)

The DLL build path is prescribed by the repository agent guide as
`tools/build-win32-dll.sh --release --target <target>` (with paths supplied via
environment variables or arguments). That script is **not present in this
repository's `tools/`**, so this step must be executed in the Windows-capable
mod-base environment and recorded here as an external result.

- [ ] `UNVERIFIED` — Build command actually used: `<<WIN32_BUILD_COMMAND>>`
- [ ] `UNVERIFIED` — Target triple / arch: `<<WIN32_TARGET>>`
- [ ] `UNVERIFIED` — Build exit code: `<<WIN32_BUILD_EXIT>>`
- [ ] `UNVERIFIED` — Produced DLL path + SHA256: `<<WIN32_DLL_PATH>>` /
      `<<WIN32_DLL_SHA256>>`
- [ ] `UNVERIFIED` — Build log URL/artifact: `<<WIN32_BUILD_LOG_URL>>`

### 2.5 Live game / full-libc consumers (external/prescribed environment)

Per the repository agent guide, validate injected-DLL behaviour in the Unicorn
harness first, then in the game/Wine workflow; never claim live-game success
from a build or emulator result alone.

- [ ] `UNVERIFIED` — Unicorn harness result + trace: `<<UNICORN_RESULT>>` /
      `<<UNICORN_TRACE_URL>>`
- [ ] `UNVERIFIED` — Game/Wine workflow result + trace: `<<LIVE_GAME_RESULT>>` /
      `<<LIVE_GAME_TRACE_URL>>`
- [ ] `UNVERIFIED` — Full-libc consumer compile/link/run against installed SDK:
      `<<FULL_LIBC_CONSUMER_RESULT>>`

---

## 3. Evidence each gate must produce

A gate is "closed" only when the named artifact exists and is linked here.
Placeholders are not evidence.

### 3.1 Linux CI — job names and gate conditions

Workflow `libc-crt` (`.github/workflows/libc-crt.yml`) defines **three** jobs —
record all three, not just the two named in older docs
(`docs/GAP_ANALYSIS.md`, `docs/CRITICAL_ISSUES_RESPONSE.md` and
`docs/libc/COVERAGE.md` mention only `linux-crt` + `asan-libc-crt`):

1. `linux-crt` — `ubuntu-latest`; builds safe CRT, runs CRT/libc/stdlib/safety/
   conformance/fuzz/compat/SDK suites, including the "Libc line coverage
   (>= 80% gate)" step (`tests/libc/coverage_libc.sh`).
2. `asan-libc-crt` — `ubuntu-latest`; `tests/crt/asan/run_asan.sh`; no
   `ASAN_OPTIONS` suppressions; ASan errors must fail the build.
3. `stdlib-deep-family-integration` — `ubuntu-latest`; deep stdlib, compile-time,
   family-gap and end-to-end integration suites.

Gate condition: **all three jobs conclude `success`** on the PR #17 head SHA,
with the Checks-tab `libc-crt` conclusion also `success`. Required evidence:
run URL, run ID, head SHA, per-job conclusions, failing step/error line if any,
and the coverage-gate conclusion. Until then: `UNVERIFIED`.

### 3.2 SonarCloud quality gate — conditions, ratings, before/after

Project/branch: `haseeb-heaven_c-plus` / `agent/modern-stdlib` (branch key
`agent%2Fmodern-stdlib`), org `haseeb-heaven`.

Gate conditions to record:

- Quality gate status must be `PASS` (`<<SONAR_GATE_STATUS>>`).
- Duplication (CPD) must be **<= 3%** (the threshold in `SONAR_TRIAGE.md`; no
  threshold was changed — `sonar-project.properties` keeps the 3% limit).
- Security rating must be **A**.
- Reliability rating must be **A**.
- Production paths (`libc/src/**`, `libc/log/**`, `stdlib/safe/src/**`,
  `stdlib/safe/*.h`) remain in scope; exclusions cover only the six generated
  fixture families. No rule disabled, no threshold lowered.

Before/after table (fill `AFTER` only from the remote scan):

| Metric | Required | BEFORE — historical only | AFTER — measured post-change |
|---|---|---|---|
| Duplication (CPD) | <= 3% | **23.4%** | `<<SONAR_DUP_PCT_AFTER>>` `UNVERIFIED` |
| Security rating | A | **D** | `<<SONAR_SECURITY_AFTER>>` `UNVERIFIED` |
| Reliability rating | A | **E** | `<<SONAR_RELIABILITY_AFTER>>` `UNVERIFIED` |
| Quality gate | PASS | not locally scanned | `<<SONAR_GATE_STATUS>>` `UNVERIFIED` |

The BEFORE values (23.4% / D / E) are an **external assessment recorded in
`docs/libc/SONAR_TRIAGE.md` §1**, treated as historical input. They were **not**
re-scanned locally (no credentials) and the post-change value is **unmeasured**.
Do not present any AFTER number until the remote scan returns it.

### 3.3 Windows x86 DLL build

Required evidence: exact build command and target, exit code, produced DLL path
and SHA256, and a build log/artifact URL. Gate condition: build exits 0 and the
DLL artifact exists for the PR #17 head. ABI caveat: Windows x86 ABI is not
evidenced locally (`abi_test` NO EVIDENCE for 644/646 APIs), so the DLL build
alone does not close the ABI-evidence gap. Until the artifact is recorded:
`UNVERIFIED`.

### 3.4 Live game / full libc consumers

Required evidence: Unicorn harness result and trace, game/Wine workflow result
and trace, and a full-libc consumer compile/link/run result. Gate condition:
harness passes first, then the live-game workflow passes; a successful DLL
build or emulator run **alone is not live-game success**. Until traces are
linked: `UNVERIFIED`.

---

## 4. Rollback / response plan if a gate fails

No merge until every gate in §2 is closed with real evidence. If a gate fails:

1. **Linux CI red.** Do not merge. Identify the failing job and step from
   `<<FAILED_STEP>>`; fix or revert the offending commit on
   `agent/modern-stdlib` and re-push to trigger a new run. If the failure is
   `safe_lib_errno.h` not found, verify the tracked header
   (`third_party/safeclib/include/safe_lib_errno.h`) and the
   `scripts/build_safecrt.sh` generation fallback before re-running (see
   `docs/libc/CI_RUN128_FIX_EVIDENCE.md`). Record the new run URL; never carry
   forward a previous run's conclusion.
2. **SonarCloud gate fails.** Do not merge and do not claim the gate passes.
   - If CPD is still > 3%: confirm the non-generated production sources
     (`libc/src/**`, `libc/log/**`, `stdlib/safe/src/**`, `stdlib/safe/*.h`)
     are the drivers. Extend exclusions **only** to genuinely generated
     fixture families; never exclude production paths and never lower the 3%
     threshold. Re-run the remote scan.
   - If security/reliability are below A: fetch the issue list with a token,
     triage each item as generated-fixture noise / real production issue /
     documented false positive. Fix real production findings in code; any
     suppression must be narrow and justified. No fabricated fix, no blanket
     disable. Re-run and record the actual ratings.
3. **Windows x86 DLL build fails.** Do not merge. Revert the target-specific
   change or fix the build in the Windows-capable environment; keep ABI wrappers
   intact and do not add raw game-memory offsets to demos where an `IGI::`
   native wrapper exists. Re-run and record a fresh artifact + SHA256.
4. **Live game / full-libc consumer fails.** Do not merge. Fix the injected-DLL
   integration in the Unicorn harness first, then re-run the game/Wine
   workflow; do not assert live-game success from a build or emulator result
   alone. Re-run and link new traces.
5. **General.** Any re-run replaces the old placeholder values. Keep the
   BEFORE Sonar values labelled historical. Preserve the `UNVERIFIED` mark for
   every gate until its artifact is attached. If a gate cannot be closed in
   this environment, report it as blocked rather than inferring a pass.

---

## 5. Explicit non-claims (local results are not remote green)

- **Local clean-export builds are not remote green.** The clean-export result
  in `docs/libc/CI_RUN128_FIX_EVIDENCE.md` (`CLEAN_EXPORT_BUILD_OK`) was produced
  locally with `git archive HEAD | tar -x -C /tmp/ci128-sim` on macOS arm64. It
  verifies the checked-in header and generation fallback **locally only**; it
  is not a GitHub Actions result and does not close the Linux CI gate.
- **Local ASan/UBSan/TSan and fuzz results are not remote green.** ASan is
  locally verified only for 12 allocator scenarios with Homebrew clang 23
  (`docs/libc/ASAN_AUDIT.md`, `docs/libc/COVERAGE.md`); UBSan passes are local
  recorded results; TSan is NOT VERIFIED on this host. The Linux jobs
  `asan-libc-crt` and `linux-crt` are the authoritative cross-platform paths
  and are unverified locally. Local sanitizer passing is evidence of observed
  behaviour only, never a language guarantee or a remote gate result.
- **All other local suite numbers are recorded, not remote.** Counts in
  `docs/libc/COVERAGE.md` and `docs/STATUS.md` (libc 83/83, CRT unit 79/79,
  91.74% local libc line coverage, 600-case Rust-vs-C+ comparison, etc.) are
  local recorded results and must not be copied into remote placeholders.
- **Prior Sonar values are historical, not current.** Duplication **23.4%**,
  security **D**, reliability **E** are external-assessment inputs from
  `docs/libc/SONAR_TRIAGE.md` §1. The post-change remote quality gate is
  **unmeasured**; no new duplication number or rating is asserted here, and
  no production security/reliability finding is claimed fixed.
- **No Windows x86 DLL, live-game, or injected-DLL success is claimed.** Those
  gates are `UNVERIFIED` per §1 and can only be closed with the external
  evidence in §§2.4–2.5 and §§3.3–3.4.

---

## Appendix A — placeholder registry

| Placeholder | Gate | Filled from |
|---|---|---|
| `<<PR17_HEAD_SHA>>`, `<<LINUX_CI_RUN_URL>>`, `<<RUN_ID>>`, `<<PUSH_OR_PR>>`, `<<WORKFLOW_CONCLUSION>>`, `<<PR_CHECK_CONCLUSION>>` | Linux CI | Actions run page / PR Checks |
| `<<JOB_LINUX_CRT>>`, `<<JOB_ASAN_LIBC_CRT>>`, `<<JOB_STDLIB_DEEP>>`, `<<FAILED_STEP>>`, `<<FIRST_ERROR_LINE>>`, `<<COVERAGE_GATE_CONCLUSION>>`, `<<LINUX_LIBC_COVERAGE_PCT>>`, `<<LINUX_CI_LOG_URLS>>` | Linux CI | Per-job logs |
| `<<SONAR_GATE_STATUS>>`, `<<SONAR_SCAN_URL>>`, `<<SONAR_DUP_PCT_AFTER>>`, `<<SONAR_SECURITY_AFTER>>`, `<<SONAR_RELIABILITY_AFTER>>`, `<<SONAR_ISSUES_COUNT>>`, `<<SONAR_ISSUES_TRIAGE>>`, `<<PRODUCTION_SCOPE_CHECK>>` | SonarCloud | SonarCloud API / scan page |
| `<<WIN32_BUILD_COMMAND>>`, `<<WIN32_TARGET>>`, `<<WIN32_BUILD_EXIT>>`, `<<WIN32_DLL_PATH>>`, `<<WIN32_DLL_SHA256>>`, `<<WIN32_BUILD_LOG_URL>>` | Windows x86 DLL | Windows build environment |
| `<<UNICORN_RESULT>>`, `<<UNICORN_TRACE_URL>>`, `<<LIVE_GAME_RESULT>>`, `<<LIVE_GAME_TRACE_URL>>`, `<<FULL_LIBC_CONSUMER_RESULT>>` | Live game / consumers | Harness + game/Wine environment |
