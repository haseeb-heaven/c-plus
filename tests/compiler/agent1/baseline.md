# Baseline audit (2026-11-23)

Branch `agent/core-safety`, HEAD `4ce140e` (plus local uncommitted audit docs).

## Commands and actual results

```
./tests/run_tests.sh                 -> C+ tests: 109 passed, 0 failed
sh ./tests/core/run.sh               -> core safety: 152 passed, 0 failed
ctest --test-dir build --output-on-failure -> 2/2 passed (cplus_tests, cplus_demos)
./tests/out/native_tests             -> native C+ safety suite: 44/44 passed
./tests/out/strict40_runner          -> 40 tests, 27 rejected, 27 build-rejected, 0 mismatches
python3 tests/metrics/measure.py     -> safe 90/90 DEFAULT, 90/90 STRICT; FP 0/0;
                                        detection 80/107 DEFAULT (74.8%), 97/107 STRICT (90.7%)
```

Raw output: `tests/compiler/agent1/results/baseline_raw.txt`.

## Baseline conclusion

All currently-wired suites pass with zero failures and zero false positives.
Known defects addressed before this audit: array decay bounds propagation,
arithmetic overflow/divide/shift, alias UAF/double-free, RAII cleanup paths.
Remaining gaps are documented in `known_gaps.md` and are not hidden by this
baseline (notably: unknown-length pointer operations in some contexts,
general races, cross-TU ownership, raw malloc/free UAF without ASan).
