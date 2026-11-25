# Response to external PR #17 assessment (Agent 2)

Evidence-based status as of local commit `ca00f1b`. **No merge claimed.**

## Stale metadata

The assessment quotes PR metadata (88 commits, 843 files, 406,546 additions)
that reflects the *pushed* branch. Locally the branch has **23 unpushed commits**
and ~1,179 changed files / ~486k additions. Nothing has been pushed or merged.

## Blocker-by-blocker

| Assessment blocker | Actual state | Evidence |
|---|---|---|
| Linux CI fails: `safe_lib_errno.h` not found | Fixed on this branch. Header is checked in **and** `scripts/build_safecrt.sh` generates it into `$OUT` with `-I$OUT` when absent. | `third_party/safeclib/include/safe_lib_errno.h`, `scripts/build_safecrt.sh:44-46`, `sh scripts/build_safecrt.sh` passes locally |
| SonarCloud D/E, duplication 23.4% | Cannot verify without credentials. Generated fixtures excluded from analysis; production paths and thresholds untouched. Remote gate must be re-run. | `sonar-project.properties`, `docs/libc/SONAR_TRIAGE.md` |
| Ordinary C spellings not demonstrated by installed SDK | Fixed: `cplus_libc_safe.h` is installed and an **external CMake `find_package` consumer** passes with unchanged `malloc/memset/memcpy/strcpy`. | `tests/sdk/run_sdk_consumer.sh` → `sdk consumer: ok`, `docs/libc/SDK_CONSUMER.md` |
| CMake package config not installed | Already installed and now verified by a clean-prefix consumer. | `lib/cmake/cplus_hardened_libc/cplus_hardened_libcConfig.cmake` in install prefix; `tests/sdk/` |
| Contract "realloc alias invalidation planned" | Implemented generation-based ABA guards in both allocators; stale contracts corrected. | `libc/src/stdlib/alloc.c`, `stdlib/safe/src/safe_alloc.c`, `docs/libc/CONTRACT_STATUS.md` |
| 11 commits behind develop | Confirmed docs-only (`README.md`, `docs/ROADMAP.md`); no functional merge required. | `git diff --stat HEAD...origin/develop` |

## What was added after the assessment snapshot

- ASan audit: both allocators delegate each block to its own host `malloc`
  block with a separate metadata table — **no arena/suballocation, so no
  `asan_interface.h` poisoning**. 12 API-level ASan scenarios verified with
  Homebrew clang 23 + explicit SDK; OOB/UAF detected, valid operations clean.
- 384 runtime scenarios + ~81,000 generated cases across both libraries;
  52/52 scenario suites pass in plain and UBSan modes.
- 15 user-supplied crash programs reproduced as fixtures with safe variants;
  sanitizer baselines recorded, caller-only vs library-enforced fixes separated.

## Honest remaining limitations

- SonarCloud gate and Linux CI result cannot be confirmed locally; they must be
  re-run on the remote after push.
- Compiler-side runtime bounds instrumentation for runtime-indexed function
  pointer arrays is missing; C+ currently relies on Clang UBSan there. Handed
  to Agent 1 (`docs/libc/DISPATCH_BOUNDS_HANDOFF.md`).
- Cross-allocator frees, arbitrary alias/lifetime safety, untrusted format
  strings, and raw pointer provenance remain caller/compiler responsibilities.

## Verdict

Substantial, tested implementation; **not** a finished 100% hardened libc
release. This response closes the concrete, verifiable blockers; remote gates
and Agent 1 compiler work remain before merge readiness can be asserted.
