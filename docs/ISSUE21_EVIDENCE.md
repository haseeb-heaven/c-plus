# Issue #21 — Agent 1 current-head evidence (2026-11-23)

Branch `agent/core-safety`, PR #16 (open, not merged). Base updated by merging
current `develop` (`6c29dd8`); `.gitignore` conflict resolved by keeping both
the `*.plist` and `__pycache__/` rules.

## Commands and results (macOS arm64, Apple clang 16 backend by default)

```
cmake -S . -B build-verify-1123 -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
cmake --build build-verify-1123 --parallel          # rc=0, 0 errors
./tests/run_tests.sh                                # 109 passed, 0 failed
./tests/core/run.sh                                 # 157 passed, 0 failed
ctest --test-dir build-verify-1123 --output-on-failure  # 2/2 passed
python3 tests/core/p0_correctness_suite.py          # 11/11
python3 tests/core/fnptr_bounds_suite.py            # 4/4
python3 tests/core/rust_compare_suite.py            # 20/20 (C+ 15/20, Rust 15/20)
python3 tests/core/runtime_crashes_suite.py         # 15/15
python3 tests/core/sanitizer_suite.py               # 33/33
python3 tests/core/lib_contracts_suite.py           # 46/46
python3 tests/core/bugs51_97_suite.py               # 207/207 (188 scenarios, 28 documented gaps)
python3 tests/core/redteam_100.py                   # 110 scenarios, 0 mismatches
CLANG_BIN=/usr/bin/clang python3 tests/metrics/measure.py
```

`measure.py`: safe-C acceptance 90/90 in DEFAULT and STRICT, 0 false positives,
bug detection 80/107 DEFAULT (74.8%) and 97/107 STRICT (90.7%).

## Issue #21 task status

1. **Branch rebase/conflicts:** done — merged `origin/develop`, resolved
   `.gitignore`, pushed; PR #16 base now current.
2. **P0 correctness:** new isolated `tests/core/p0_correctness_suite.py` (11
   cases) covers null-out-parameter (address-of to a non-output function does
   not prove initialization), safe aliases, alias UAF/double-free, realloc
   failure/success (including stale aliases after `if(!t) return;`), and
   cleanup paths. Also fixed a real name-collision false positive: two
   `request` variables in nested scopes of `main` were conflated by
   `stringPtrs_` (now keyed by `VarDecl*`), which had broken the `E_networking`
   demo.
3. **setjmp/longjmp policy:** STRICT now rejects a `longjmp` that bypasses
   cleanup of a live allocation even when an unreachable `free` follows it
   (`classic_bug34_longjmp.c`); the safe `if(setjmp==0) longjmp; else free(p);`
   resume idiom and plain no-resource `goto`/`longjmp` remain accepted with
   warnings. Forward `goto` that jumps over a `free` of a live allocation is
   now a STRICT error. Compatibility mode keeps warnings. This is a policy
   fix, not a test relocation.
4. **Fail-closed:** proven violations and unsupported contract fields are
   rejected; unverifiable pointers with unknown bounds/lifetime, field and
   container element lifetimes, general races, and cross-TU ownership are
   **explicitly unsupported** rather than claimed safe — rejecting them would
   reject ordinary C, contrary to the product direction. No Rust-equivalence
   claim.
5. **CI:** all local suites and clean CTest pass on macOS arm64. Remote CI:
   GitGuardian passes; SonarCloud fails on PR #16; the only configured GitHub
   Actions workflow is `libc-crt` (Agent 2) and it is failing on their branch.
   **No macOS/Linux compiler CI workflow exists yet** — this is an open gap,
   not a green result.
6. **Docs:** `docs/SAFE_SUBSET.md` updated for C+ inserted bounds checks,
   configurable sanitizers, the new setjmp/goto policy and the fail-closed
   status; `docs/RUST_COMPARISON.md` records the paired C+/Rust evidence.

Remaining known gaps are listed in `docs/LIMITATIONS.md` and the 28 documented
gaps in `tests/out/core/bugs51_97/report.txt`. PR remains open; no auto-merge.
