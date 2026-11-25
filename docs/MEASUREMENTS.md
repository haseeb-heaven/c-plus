# C+ safety measurements

Reproduce with `python3 tests/metrics/measure.py`,
`python3 tests/core/redteam_100.py` and
`python3 tests/core/brutal_memory_fuzz.py`. Machine-readable results are
written to `tests/out/metrics/report.json`.

Parallel-agent verification (one agent auditing, one authoring the brutal
fuzzer, one running the fast suites) produced: all fast suites green, the
brutal fuzzer's 82 initial mismatches reduced to 0 after fixes, and 15,720
mass-fuzz (3 seeds) plus 1,200 cross-TU programs passing. See
`docs/SAFETY_COVERAGE.md` for the per-suite breakdown.

## Axes the compiler is judged by

| metric | DEFAULT | STRICT |
| --- | --- | --- |
| safe C programs accepted (curated real C + repo positives) | **90/90 (100%)** | **90/90 (100%)** |
| false positives (safe programs rejected) | **0** | **0** |
| genuine memory bugs detected (tests/safety corpus) | 80/107 (74.8%) | 97/107 (90.7%) |

The 51 curated programs in `tests/metrics/safe_corpus/` are ordinary ISO C with
no annotations, no renames and no new syntax; all 51 compile cleanly under
`clang -Wall -Wextra -Werror`. The remaining 39 are the repository's positive C
fixtures (excluding one documented borderline branch-UAF case).

## Adversarial red team

`tests/core/redteam_100.py` runs **110 hand-crafted scenarios** in both modes:
65 must-reject memory errors, 8 strict-only unverified operations and 37
must-accept safe programs. Result: **110/110 with 0 mismatches**. The suite
covers alias UAF, branch/loop/interprocedural UAF, realloc failure paths,
destructor invalidation, invalid frees, bounds and libc overflows, nullability,
uninitialized state, provenance, resource moves, threads and FFI.

A second, larger adversarial suite, `tests/core/brutal_memory_fuzz.py`,
generates **688 seeded programs** across 8 categories (UAF, double free,
invalid free, overflow, null/uninitialized, provenance, ownership, threads)
with 486 must-reject, 110 strict-only and 92 must-accept cases. Result:
**688/688 with 0 mismatches**. It found and we fixed holes in constant-sized
libc copies, nested struct-field frees, cross-thread frees of shared globals,
stack-array borrows passed to threads, and a single-worker thread-race false
positive.

## Notable gaps (honest)

27 of the 107 bug fixtures are missed in DEFAULT and 10 in STRICT; these are
mostly outside the declared sound subset: wrong `malloc` byte count with a
compatible pointer type, deliberately conflicting aliases that the project
now accepts as valid C, branch-conditional UAF (warn in DEFAULT / reject in
STRICT), format-string ABI mismatches, and double release after an unverified
`realloc`. `longjmp` cleanup is no longer a blanket gap: STRICT rejects jumps
that bypass cleanup of a live allocation, while DEFAULT still warns. Misses
are retained as regression fixtures and documented rather than claimed as
detected.

These figures measure the declared subset, not Rust-equivalent safety for
arbitrary C. They are local macOS arm64 measurements, not CI results; no Linux
CI claim is made. The paired Rust comparison (`docs/RUST_COMPARISON.md`)
covers only the overlapping subset (15/20 compile-time rejections for each
language) and makes no parity claim.
