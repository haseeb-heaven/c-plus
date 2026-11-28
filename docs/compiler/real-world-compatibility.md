# Real-world C compatibility (measured)

This document records measured compatibility for unmodified, externally sourced
C code. Raw evidence is in the result files named below; no placeholder tables
are kept here.

## Toolchain snapshot

```
host        macOS 27.2.1 arm64
C+          C+ 0.1.0 (./build/c+ --version)
reference   /usr/bin/clang 16.0.0; /opt/homebrew/bin/gcc-14 14.2.0
modes       --legacy-c, --default, --strict; all with --sanitize=none
```

## Measured results

| Suite | Result | Evidence |
| --- | --- | --- |
| Downloaded single-file C programs | 6/6 projects: stdout+exit match clang; source byte-identical after `#line` | `tests/compiler/real_world_c/results/summary.txt` |
| 12 pinned projects × 5 standards × 3 modes | 175 PASS, 5 STRICT safe-mode rejections, 0 hard failures; legacy source preservation 60/60 | `tests/compiler/real_world_c/wave3/results/summary.txt` |
| 10 pinned apps/games × 12 configs | 53 PASS, 61 documented SAFE_MODE_RULE, 6 reference-rejected, 0 DEFECT, 0 mismatches | `tests/compiler/real_apps/summary.txt` |
| Source preservation fixtures | 4/4 byte-identical after one `#line`; symbols match clang | `tests/compiler/source_preservation/results/summary.txt` |
| ABI checks | 33/33 (pointer sizes, offsets, callbacks, variadics, symbols, static libs) | `tests/compiler/abi_safety/results/summary.txt` |
| Cross-compiler object link | 12/12 steps; c+ and clang objects link both directions | `tests/compiler/abi_mixed/results/summary.txt` |
| C standards | c89/gnu89/c99/c11/c17/c2x pass (48/0/0); c23 unsupported by the default backend (8) | `tests/compiler/c_standards/summary.txt` |
| Vulnerable real C programs (web-pinned) | 40 cases: 32 caught statically, 0 runtime traps, 1 missed dynamic OOB, 7 not runnable | `tests/compiler/vulnerable_apps/results/summary.txt` |

## Honest limitations

- STRICT intentionally rejects some valid C (`SAFE_MODE_RULE`), e.g. pointer
  parameters with unverifiable extents; those cases are recorded, not hidden.
- The latest real-apps run has 61 `SAFE_MODE_RULE` and 53 `PASS` (previously 46
  and 68). The increase is a policy outcome, not an ABI failure: warnings are
  errors by default in the safe modes (`--default`/`--strict`, issue #34), so
  warning-only diagnostics that `--legacy-c` accepts become hard rejections
  and are recorded as documented `SAFE_MODE_RULE` cases. All 38 LEGACY configs
  still match clang/gcc with exact symbols and byte-identical source, and
  `failed=0`/`mismatches=0` are unchanged. This is not a claim that every
  configuration compiles or that all C is proven safe.
- The default backend has no `-std=c23`; clang-23/GCC-14 can select it.
- `CAUGHT_RUNTIME = 0` in the vulnerable-app corpus is a measured fact; the
  single dynamic-OOB miss is documented in that suite's README.
- Compatibility here is behavioural, not a conformance or safety proof.

Reproduce the real-apps row with
`CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_apps/run_suite.py` from the
repository root; other suites' commands are recorded in their READMEs.
