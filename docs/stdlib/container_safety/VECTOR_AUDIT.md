# Task 2 Phase 4 — `stdlib/vector.hp` implementation audit

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **File ownership honoured:** only this document was created/modified.
  `stdlib/vector.hp` was **not** changed. No test, harness, compiler, runtime,
  expectation fixture or git index entry was touched. No `git add` / commit.
- **Verdict:** **no defect reproduced in `stdlib/vector.hp`**; no header fix was
  warranted or applied. Every requested scenario below was exercised in
  default (Safe/UBSan), `--strict --sanitize=none` and
  `--strict --sanitize=undefined` builds, with timeouts on both build and run.

## 1. Scope and evidence vocabulary

Header under test: `stdlib/vector.hp` (308 lines; `slice` at `:29-86`,
`vector` at `:88-306`). Dependency read for context: `stdlib/error.hp`
(checked allocator `stdlib_alloc`/`stdlib_release`, fault injection, telemetry).
Baseline documents read: `docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md`,
`docs/stdlib/DEEP_VECTOR_TASK4_CONTRACT.md`, and
`tests/stdlib/agent2_audit/module_audits/vector.hp.md`.

- **[EXECUTED]** — command run in this audit with recorded rc/stdout/stderr.
- **[OBSERVED]** — behaviour asserted by a passing self-checking harness.
- **[N/A]** — requested area does not apply to this type.

A "defect" means a reproduced deviation from the header's own documented
contract (`vector.hp:1-20,109-112,189-190,212,231-232,261-262,288-295`).
Conservative but documented behaviour (e.g. the half-limit growth refusal) and
caller lifetime rules explicitly declared undetectable are not defects.

## 2. Harnesses executed and exact results

All commands run from the repository root with the checked-in `build/c+`
driver. Compiler/run invocations were bounded with `timeout` (60 s build,
30 s run). Flags are always separate argv entries.

### 2.1 Deep regression harnesses — [EXECUTED]

```sh
sh tests/stdlib/run_deep_tests.sh deep_error_vector deep_vector_task4
```

```
deep stdlib harness: build<=60s, run<=30s, kill-after=5s, compiler=.../build/c+
pattern(s): deep_error_vector deep_vector_task4
ok       deep_error_vector (default): DEEP_ERROR_VECTOR 1 F
ok       deep_error_vector (strict-none): DEEP_ERROR_VECTOR 1 F
ok       deep_error_vector (ubsan): DEEP_ERROR_VECTOR 1 F
ok       deep_vector_task4 (default): VECTOR_TASK4 16 0
ok       deep_vector_task4 (strict-none): VECTOR_TASK4 16 0
ok       deep_vector_task4 (ubsan): VECTOR_TASK4 16 0

MODE           PASSED   FAILED  SKIPPED  BUILD-FAIL  RUN-FAIL  OUT-FAIL
default             2        0        0           0         0         0
strict-none         2        0        0           0         0         0
ubsan               2        0        0           0         0         0
deep stdlib tests: 6 passed, 0 failed, 0 skipped (2 file(s) x 3 modes)
```

Byte-exact stdout was unchanged in all three modes (files under
`build/stdlib-deep/`): `deep_error_vector_default.out` ends
`DEEP_ERROR_VECTOR 1 F` with all 12 records `1`;
`deep_vector_task4_strict_none.out` ends `VECTOR_TASK4 16 0` with all 16
`T4_*` records `1`. stderr was empty for both tests in all modes.

**Concurrency caveat (recorded honestly).** The full 6/6 matrix above was
executed successfully earlier in this session. A final re-run after this
document was authored failed the default/UBSan *builds* (strict-none still
passed 2/2) with
`cplus_runtime.c:129:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'`
while the driver compiles the shared, instrumented runtime from
`c-plus-core/runtime/cplus_runtime.c` against
`c-plus-stdlib/runtime/include/cplus/cplus.h`. The sibling-owned core runtime
had been edited concurrently to reference `CPLUS_EXIT_ARITHMETIC`, which at
that moment existed only in `c-plus-core/runtime/include/cplus/cplus.h`
(`CPLUS_EXIT_ARITHMETIC = 110`). This is an out-of-scope, in-progress compiler/
runtime working-tree state, **not** a `vector.hp` defect and not something this
task may fix (compiler/runtime edits are forbidden). No vector code changed
between the passing matrix and this transient build failure.

### 2.2 `f100_vec_*` fuzz harnesses — [EXECUTED]

Built and run directly with the flags documented in `tests/fuzz100/README.md`,
because the `run.sh` wrapper's native runner is broken in this checkout
(Section 5). Three modes per case: default (Safe/UBSan), `--sanitize=none`,
`--sanitize=undefined`. Build rc = 0, run rc = 0, stderr = 0 bytes in every
combination. Exact final stdout:

| Harness | Ops | Final records (identical in all 3 modes) |
| --- | --- | --- |
| `f100_vec_f070_capacity.cp` | 2500 | `F-070 overflow.probes 12` / `f070_digest 0cfbfacab50c0a12` / `F-070 ok 2500 ops` |
| `f100_vec_f071_erase.cp` | 2500 | `f071_digest 67f6cdf44ffbcd7a` / `F-071 ok 2500 ops` |
| `f100_vec_f072_stale.cp` | 2400 | `F-072 stale.hits 1268` / `f072_digest 48c2b79270f00633` / `F-072 ok 2400 ops` |
| `f100_vec_f073_faults.cp` | 2465 | `F-073 faults 65` / `f073_digest 90eca56c75396c47` / `F-073 ok 2465 ops` |

Representative commands:

```sh
timeout 60 ./build/c+ build tests/stdlib/fuzz/f100_vec_f070_capacity.cp \
  --sanitize=none -o <tmp>/f100_vec_f070_capacity_sanitize_none.bin
timeout 30 <tmp>/f100_vec_f070_capacity_sanitize_none.bin < /dev/null
```

These harnesses check the vector's exact capacity against a shadow model
(`f100_cont_engine.hp:272-285`), positional insert/erase against a shadow
array (`:437-456,928-969`), stale-slice refusal (`:656-686,970-980`) and
injected allocation-failure atomicity (`:549-653`), in 2400–2500 seeded
operations each.

### 2.3 Independent Phase-4 probe — [EXECUTED]

A self-checking probe was written to the session temp directory (outside the
repository, so no ownership rule was breached) to cover the requested areas
not fully pinned by the committed harnesses. It was built and run in three
modes with 60 s / 30 s timeouts:

```sh
timeout 60 ./build/c+ build <tmp>/vec_phase4_probe.cp -o <tmp>/probe.bin
timeout 60 ./build/c+ build <tmp>/vec_phase4_probe.cp --strict --sanitize=none -o <tmp>/probe_strict_none.bin
timeout 60 ./build/c+ build <tmp>/vec_phase4_probe.cp --strict --sanitize=undefined -o <tmp>/probe_ubsan.bin
timeout 30 <bin> < /dev/null
```

All three builds returned 0, all three runs returned 0 with empty stderr and
byte-identical stdout ending:

```
TELEMETRY 45 45
J_ALLOC_FREE_BALANCE 1 A 45 F 45
VEC4P 10 0
```

The 10 records are `A_EMPTY_SINGLE`, `B_GROW_SHRINK_EMPTY_COPY`,
`C_OVERFLOW_SHAPES`, `D_GROWTH_OOM_ATOMIC`, `E_REMOVAL_POSITIONS`,
`F_REPEATED_INSERT_ERASE`, `G_SLICE_INVALIDATION`, `H_LARGE_VOLUME`,
`I_PARTIAL_INIT_DOUBLE_DROP`, `J_ALLOC_FREE_BALANCE` — all `1`.

## 3. Scenario-by-scenario findings

| # | Requested area | Evidence | Result |
| --- | --- | --- | --- |
| 1 | empty / single element | Probe A; `deep_error_vector` V2/V5; `f100_vec_f071` | view on empty is `size=0, valid=0, stale=0`; `at_checked`/`pop_back`/`erase` refuse; single push gives `size=1, cap=4`; slice reads value. No defect. |
| 2 | growth / shrinkage | Probe B/F/H; `deep_error_vector` V1; `f100_vec_f070` | `0→4→8→16` at pushes 1/5/9; exact `reserve(23)`/`reserve(24)`; `shrink_to_fit` to `used`; 1500 alternating insert/erase and a 20000-element run keep a shadow model exact. No defect. |
| 3 | capacity overflow | Probe C; `deep_error_vector` V6; `deep_vector_task4` `T4_RESERVE_EXACT_AND_OVERFLOW`/`T4_PUSH_GROWTH_OVERFLOW` | `reserve(SIZE_MAX)` and `reserve(LIMIT/sizeof(int)+1)` return `-1` with state unchanged; full buffer beyond the half-limit refuses `-1`; 12 overflow probes in F-070. No defect. |
| 4 | allocation failure during growth | Probe D; `deep_error_vector` V4; `f100_vec_f073` (65 injected faults) | `fail_at(1)` on push growth, insert growth, `reserve`, `copy_from` and `shrink_to_fit` each returns `-1` with size, capacity, contents **and generation** unchanged; retry succeeds and the old slice goes stale. No defect. |
| 5 | removal at begin / middle / end | Probe E; `deep_vector_task4` `T4_ERASE_BOUNDARIES`; `f100_vec_f071` | `erase(0)`, middle `erase` (incl. `out == NULL`), last-element `erase`, `pop_back`, repeated pops to empty and refusal on empty all preserve order and bump generation exactly once. No defect. |
| 6 | repeated insert / remove | Probe F (1500 ops, shadow array); `f100_vec_f071` (2500 ops) | Every positional insert/erase returns the shadow value, preserves order, and keeps `capacity() >= size()`. No defect. |
| 7 | slice invalidation (generation counter) | Probe G (all 8 shape-changing ops + non-reallocating push); `deep_error_vector` V3; `f100_vec_f072` (1268 stale hits) | Fresh slice/sub-slice stays readable across a non-reallocating `push_back`; `reserve`, `pop_back`, `insert`, `erase`, `clear`, `copy_from`, `shrink_to_fit`, `drop` all make parent and sub-slice `stale()==1 && valid()==0` with `at_checked()==-1`; refused mutations leave slices fresh. No defect. |
| 8 | hash collisions | — | **[N/A]** `vector.hp` has no hashing or keyed lookup. (Open addressing lives in `collections.hp`, a different module.) |
| 9 | large volumes | Probe H (20000 pushes, sampled + full verification, reserve to 40000, shrink, 5000 front erases) | All values intact; `capacity()` exact at each stage; first/last element after bulk erase correct; telemetry balanced. No defect. |
| 10 | destruction after partial init | Probe I (4 elements land, 5th growth fails, scope exit) and probe A/D scopes | Failed growth leaves a valid 4-element buffer; compiler-inserted `drop` releases it; `alloc/free` ends 45/45. No defect in `vector.hp`. |
| 11 | double destruction | Probe I (explicit `drop()` twice, then re-push, then scope exit); `deep_vector_task4` `T4_CLEAR_DROP_REUSE`; `f100_cont_engine.hp:996-1004` | `drop()` is idempotent: second call inert, vector reusable afterwards, no double release or C+ ownership fatal. No defect. |
| 12 | failure atomicity | Probe D; `deep_vector_task4` `T4_INSERT_BOUNDARIES`/`T4_COPY_FROM`/`T4_SHRINK_FAILED_ATOMIC`; `f100_vec_f073` | All five allocating mutations are failure-atomic, including the generation counter, which is the subtlest observable. No defect. |

## 4. Confirmed defects and changes

**Confirmed defects in `stdlib/vector.hp`: 0. Header changes: 0.**

No behaviour was added, relaxed or rewritten to make a test pass. The five
initial failures in the independent probe were all **probe expectation
mistakes**, corrected in the ephemeral probe only (the committed harnesses were
never edited):

1. `B` copied from a vector that had just been filled, not from an empty one —
   fixed by adding a genuinely empty source.
2. `E` expected values `[1,2,3]` after erasing the value `3`; correct state was
   `[1,2,4]`.
3. `G` asserted a slice stays fresh across a `push_back` that actually
   reallocated (buffer was full) — fixed by reserving spare capacity first;
   later an inverted `!stale()` assertion and a no-op `shrink_to_fit`
   (`space == used`) were likewise corrected.
4. `I` injected a failure on a `push_back` with spare capacity (no allocation
   happens, so the push succeeds) — fixed by filling to capacity first.
5. The final alloc/free imbalance was traced to compiler RAII lowering
   (Section 5), not to a vector leak; after an explicit compensating drop the
   probe reported `45 45` in all three modes.

These are the same class of test-side mistakes already documented in
`DEEP_VECTOR_TASK4_CONTRACT.md:157-165`.

## 5. Out-of-scope observations (not vector defects, not fixed)

The probe surfaced two compiler-side RAII lowering behaviours. They are
recorded for completeness because they affect how a caller must write tests;
neither was modified here (compiler files are outside this task's ownership)
and neither changes the verdict on `vector.hp`.

- **Repeated owned-local name in sibling scopes.** `closeScope`/
  `dropsFor` (`compiler/Sema/RaiiLowering.cpp:386-405,518-538`) combined with
  `tryResourceDeclaration`'s `owned_.count(name)` guard (`:650-652`) means a
  second `vector v = vector();` in a later sibling scope is not re-registered,
  so its scope-exit `vector_drop` is omitted. Evidence: emitting the lowered C
  of the first probe revision showed `vector_drop(&z); vector_drop(&w);` at
  scope B's close but no `vector_drop(&v)`; the cumulative telemetry gap
  disappeared when each scope used a unique variable name. This is a compiler
  lowering issue, not a `vector::drop` leak.
- **Drop injected into macro bodies.** With a uniquely named owner, the
  lowering inserted `vector_drop(&g_src); vector_drop(&g_v);` inside a
  `#define CHECK_STALE() do { ... } while (0)` body (before its closing `}`),
  corrupting the macro. Replacing the macro with a brace-free expression
  restored a clean build. Again a lowering issue; `vector.hp` is untouched.
- **`f100_vec_*` strict-mode build.** `--strict --sanitize=none` (and
  `--strict --sanitize=undefined`) reject the shared fuzz engine, not vector.hp:
  five `[CPLUS-ARITH] remainder by zero cannot be excluded` errors at
  `tests/stdlib/fuzz/f100_cont_engine.hp:347,445,842,880,937`. Non-strict
  builds with `--sanitize=none` / `--sanitize=undefined` succeed and all four
  vector cases pass (Section 2.2); the deep harnesses cover strict mode.
- **`fuzz100` native runner.** `sh tests/fuzz100/run.sh --config F070,F071,F072,F073`
  exited 134 with `C+ error: index 64 is outside the bounds of 'tmp' (length 64)`;
  this is in the native runner's argv handling. Direct driver invocations were
  used for verification.
- **Concurrent core-runtime edit (see Section 2.1 caveat).** A later re-run of
  the deep harnesses failed default/UBSan builds on a missing
  `CPLUS_EXIT_ARITHMETIC` enum in `c-plus-stdlib/runtime/include/cplus/cplus.h`
  while `c-plus-core/runtime/cplus_runtime.c` (sibling-owned, modified) uses it.
  Strict-none continued to pass. Not fixed here; no vector code changed.

## 6. Residual risk (documented, not testable as recoverable failure)

- A `slice` outliving its owning `vector` dereferences a dangling
  `generation_ref`; the header explicitly declares this undetectable
  (`vector.hp:17-19`). All stale-slice checks in this audit kept the owner
  alive and never dereferenced the stale pointer.
- `generation` is an unchecked `size_t` counter (`vector.hp:130,185,208,222,
  228,257,273,284,303`); wraparound after 2^64 shape changes would alias a
  stale snapshot. Not reachable in any bounded harness.
- Concurrent mutation of one vector is not documented as safe.
- ASan is unavailable on this host; no address-safety claim is made. UBSan and
  the checked allocator (which detects double/invalid release) were exercised.

## 7. Summary counts

| Item | Value |
| --- | --- |
| Requested scenario areas investigated | 12 (11 applicable + 1 N/A) |
| Committed harnesses run | `deep_error_vector`, `deep_vector_task4` (x3 modes), 4 x `f100_vec_*` (x3 modes) |
| Independent probe checks | 10, all passing in 3 modes |
| Seeded fuzz operations | 2500 + 2500 + 2400 + 2465 = 9865 |
| Confirmed `vector.hp` defects | **0** |
| `vector.hp` edits | **0** |
| Files created/modified by this task | 1 (this document) |
| Harness runs with sanitizer diagnostic or runtime fatal | 0 |
