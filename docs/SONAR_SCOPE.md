# SonarCloud analysis scope

## What changed

Two configuration files now describe the same analysis scope:

| File | Analysis method | Notes |
| --- | --- | --- |
| `.sonarcloud.properties` | SonarQube Cloud **automatic analysis** | New file. Read from the repository's **default branch**. No wildcards allowed. Does **not** carry `sonar.projectKey`/`sonar.organization`. |
| `sonar-project.properties` | **CI-based analysis** (SonarScanner) | Updated to match. Wildcards are permitted here; the fixture paths are kept identical to avoid drift. |

Neither change modifies compiler source or test fixtures. Both files only tell
SonarCloud which paths count toward the quality gate.

## Why the first fix had no effect

SonarCloud automatic analysis **ignores `sonar-project.properties` entirely**.
The existing file therefore never influenced the failing gate. Automatic
analysis reads `.sonarcloud.properties` from the default branch, and it accepts
only a restricted set of keys (source/test paths, exclusions, encoding, C++
standard override, Python version). Wildcard patterns are explicitly not
supported in that file.

Source: <https://docs.sonarsource.com/sonarqube-cloud/analyzing-source-code/automatic-analysis/>

## Root cause of the failing gate (as reported by the CI audit)

- **Reliability E / Security E** — 102 blocker bugs and 2 blocker vulnerabilities
  raised against intentional unsafe test fixtures (`tests/safety`,
  `tests/metrics` bug corpora, negative/breaker/compile-fail corpora). These
  files exist *to be* unsafe, so their defects are test inputs, not production
  defects.
- **~25% duplication** — the generator scripts in `tests/metrics/`
  (`*_100.py`, `_260.py`, `_350.py`, …) emit near-identical scenario bodies.
  Treating generated fixture code as duplicated production code is a
  measurement artefact.

## Exact scope and rationale

`sonar.sources=compiler,runtime,tools` — production code only is the baseline
for issue analysis.

`sonar.tests=tests,demos,benchmarks,examples` — test trees remain identified
for test-code metrics.

`sonar.exclusions` and `sonar.test.exclusions` both list these directories
explicitly (no wildcards):

```
tests/safety
tests/metrics
tests/breaker
tests/cases
tests/compile_fail
tests/runtime_fail
tests/known_gaps
tests/legacy
tests/native
tests/core/quarantine
tests/stress
```

Rationale: these are the negative/adversarial corpora, bug corpora
(`bugs52`, `bugs52b`), fuzz/stress fixtures and quarantine cases. Their C/C++
files intentionally contain use-after-free, overflow, invalid free, null
dereference, unchecked libc contracts and similar defects. `sonar.exclusions`
alone is not sufficient for files under `sonar.tests`: Sonar classifies them as
test code, which is excluded only by `sonar.test.exclusions` (singular `test`).
Both keys are listed because they are supported by automatic analysis and the
classification is identical in both files.

`sonar.cpd.exclusions=tests,demos,benchmarks,examples` — duplication detection
is disabled for all non-production trees. This removes the `tests/metrics`
generator duplication (the ~25% figure) as well as duplicated scenarios in the
other test corpora. Listing the parent directories covers every nested
generator without wildcards.

Known build-output directories (`build`, `build-verify-*`) and `scratch/` are
outside the declared source/test scope, so they are not analyzed at all; CI mode
additionally keeps its `build*/**` and `__pycache__` exclusions.

## This is a scope correction, not a quality fix

Excluding fixtures **does not** make the compiler safer and **does not** fix any
finding raised against production code. In particular, the `compiler/Sema` and
`compiler/Driver` style/security findings described in `docs/CRITICAL_ISSUES.md`
remain open and still require real work. The gate should be re-read after the
scope change lands, and any remaining production-source issues must be fixed in
code, not excluded.

The fixtures themselves are untouched: they stay in the repository and in the
compiler/safety test suites (`tests/core/run.sh`, `tests/metrics/check_52.py`,
etc.). Only their contribution to SonarCloud metrics changes.

## Known limitation / rollout

- `.sonarcloud.properties` is read **only from the default branch**. It has no
  effect on automatic analysis of a pull request until this file is merged to
  the default branch; PRs analyzed before that merge keep the old scope.
- Automatic analysis and CI-based analysis must not both run for the same
  project. If CI-based analysis is later enabled, turn off automatic analysis
  in the SonarCloud UI; `sonar-project.properties` then becomes the active
  configuration and already carries the same scope.
- The property set is scanner-version dependent. If a future SonarCloud change
  stops honouring one of these keys, re-check the "Automatic analysis" and
  "Analysis parameters" documentation before assuming the scope still applies.

## Maintenance

When adding a new unsafe/adversarial fixture directory, add its explicit path
to `sonar.exclusions` and `sonar.test.exclusions` in **both** configuration
files. Do not use wildcards in `.sonarcloud.properties`.
