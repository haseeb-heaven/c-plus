# SonarCloud gate triage — `agent/modern-stdlib`

Status: **config changes proposed, gate result NOT verified locally.** See
"Verification status" below. This document records the reported findings, the
root-cause hypothesis, and exactly what was and was not changed. It does **not**
claim that the gate now passes.

## 1. Reported findings (external assessment, treated as authoritative input)

| # | Metric / rating | Reported value | Required | Delta |
|---|-----------------|----------------|----------|-------|
| 1 | Duplication (CPD) | 23.4% | <= 3% | ~20.4 pp over limit |
| 2 | Security rating | D | A | 3 grades below |
| 3 | Reliability rating | E | A | 4 grades below |

These values come from an external assessment of the branch; we did not run a
SonarCloud scan ourselves (no credentials — see §6).

## 2. Scope context

- `sonar.sources=.` — the whole repository is analysed unless excluded.
- Previous exclusions were only: `third_party/**`, `build/**`, `**/.git/**`.
- This branch adds ~800 changed files and ~400k lines. The overwhelming
  majority are **generated / adversarial test fixtures**, not production code:
  - `tests/crt/adversarial/test_*50*.c` (alloc50/mem50/io50/str50/raii50/...)
  - `tests/libc/test_corpus_*.c` (10 corpus drivers, one repeated harness)
  - `tests/scenarios/scenario_*.c` (some files 100k+ lines/cases; 136–171 KB each)
  - `scratch/bugs*.cp` (~50 catalog files, ~500k lines total)
  - `tests/compile_fail/bugcat*.c` / `.err` (47 generated negative tests + expected diagnostics)

- Production sources are a small, separate set and must remain fully analysed:
  `libc/src/**`, `libc/log/**`, `stdlib/safe/src/**`, `stdlib/safe/*.h`.

## 3. Root-cause hypothesis

| Finding | Hypothesis |
|---------|------------|
| Duplication 23.4% | Generated fixtures repeat the same harness/assertion templates thousands of times. CPD counts these across `.c`/`.cp` fixtures. The previous exclusion scope let all of `tests/` and `scratch/` into analysis, so duplication is dominated by generated data rather than by `libc/src` or `stdlib/safe`. |
| Security rating D | Issue list not available locally. Likely dominated by findings on generated adversarial fixtures (they deliberately exercise null/UAF/overflow/integer/format bug classes), which read as "vulnerabilities" to analyzers but are intentional test inputs. **This is a hypothesis, not a confirmed cause.** Production findings, if any, have not been triaged. |
| Reliability rating E | Same uncertainty. Generated fixtures intentionally contain error paths, constraint catches and `compile_fail` cases; analyzers may flag these as bugs. Production findings, if any, have not been triaged. |

We cannot confirm the security/reliability root causes without the actual issue
list (see §6). No security or reliability fix is invented here.

## 4. Configuration changes made

Only `sonar-project.properties` was changed.

### 4.1 Exclusions chosen

We chose **full removal from analysis via `sonar.exclusions`** (not
`sonar.coverage.exclusions`) for the generated families, so they are not
analysed at all — no issues, no duplication, no coverage, no security or
reliability findings. This is the "exclude outright" option recommended by the
task.

Added to `sonar.exclusions` (each with an inline comment in the file):

```
tests/crt/adversarial/test_*50*.c
tests/libc/test_corpus_*.c
tests/scenarios/scenario_*.c
scratch/bugs*.cp
tests/compile_fail/bugcat*.c
tests/compile_fail/bugcat*.err
```

Existing exclusions kept unchanged:

```
third_party/**
build/**
**/.git/**
```

`sonar.cpd.exclusions` was added as a duplication-specific safety net containing
the **same generated-only patterns**. Because the fixtures are already removed
from analysis, this is redundant by design: it only matters if the analysis
scope is widened later. No production path appears in it.

### 4.2 Before / after scope

**Before** — analysed:
- everything under `.` except `third_party/**`, `build/**`, `**/.git/**`
- => included all generated fixtures in `tests/**` and `scratch/**`
- => duplication measured against production + generated fixtures (23.4%)

**After** — analysed:
- production: `libc/src/**`, `libc/log/**`, `stdlib/safe/src/**`, `stdlib/safe/*.h`,
  plus all other non-generated sources, tests, docs, scripts, etc.
- excluded from all analysis: the 6 generated families listed in §4.1, plus the
  3 pre-existing exclusions
- => duplication measured against production/hand-written code only
- => expected: generated-fixture-driven duplication removed from the metric
  (magnitude unverified — see §6)

## 5. What is NOT changed

- **No duplication threshold change.** The 3% limit stays 3%.
- **No rule is disabled or relaxed.** No `sonar.issue.ignore.*`, no `sonar.exclusions`
  of a production path, no narrowing of rule sets.
- **No production source excluded.** `libc/src`, `libc/log`, `stdlib/safe/src`,
  `stdlib/safe/*.h` are explicitly kept in analysis (verified by grep — see §6).
- **No production code modified.** This change touches only configuration and
  this documentation file.
- **No security or reliability finding "fixed" or suppressed in production.**
  We do not have the issue list (§6), so no fix is fabricated.
- **No build/compile change.** `sonar.cfamily.compile-commands` remains
  `build/compile_commands.json`; `sonar.sourceEncoding` remains UTF-8.

## 6. Verification status (honest)

- **Local verification of the SonarCloud gate is impossible without credentials.**
  We cannot run the remote scanner or query the quality gate from this
  environment. The remote gate **must be re-run after pushing this branch**;
  only then can we read the new duplication percentage and ratings.
- **We do not claim the gate now passes.** Excluding generated fixtures is
  expected to remove the dominant source of duplication, but the resulting
  number is unmeasured and the security/reliability ratings are untriaged.
- No production-path exclusion was introduced. Check with:
  ```sh
  grep -nE 'libc/src|libc/log|stdlib/safe' sonar-project.properties
  # Expect hits only in comments that say these paths are NOT excluded.
  ```
  Properties syntax was validated by reading back the file (`cat
  sonar-project.properties`) and confirming Java-properties line continuations
  (`\` at end of line) and `#` comments are well formed.

## 7. Next steps (blocked on credentials)

1. Fetch the SonarCloud findings via the SonarCloud API (requires a token with
   the right permissions). Example (token supplied at runtime, never committed):
   ```sh
   # Issues, paginated
   curl -sS -u "$SONAR_TOKEN:" \
     "https://sonarcloud.io/api/issues/search?componentKeys=haseeb-heaven_c-plus&ps=500&p=1"
   # Quality gate status of the branch
   curl -sS -u "$SONAR_TOKEN:" \
     "https://sonarcloud.io/api/qualitygates/project_status?projectKey=haseeb-heaven_c-plus&branch=agent%2Fmodern-stdlib"
   ```
2. Triage each returned issue into: (a) generated-fixture noise (already
   excluded), (b) real production issue in `libc/src`, `libc/log`, `stdlib/safe`
   — fix in code, or (c) false positive requiring a narrow, documented
   suppression with justification.
3. Re-run the remote scan/gate after push and record the actual duplication
   percentage and security/reliability ratings here.
