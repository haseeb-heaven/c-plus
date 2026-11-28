# Known gaps (documented, not hidden)

Sources: docs/LIMITATIONS.md, SAFE_SUBSET.md, CRITICAL_ISSUES.md,
tests/known_gaps/README.md, and docs/RUST_COMPARISON.md.

## Safety-model gaps
- No full Rust-style borrow checker/lifetime annotations. Local scalar
  borrows, struct/union field provenance, conditional aliases, integer
  round-trips, identity helpers and `free(*pp)` / `free(pp[0])` pointee
  releases are tracked; arrays-of-pointers with runtime indices and general
  container element lifetimes remain unsupported.
- No general data-race verification. Same-TU global writes, transitive helper
  writes, shared heap arguments, mutex lock/unlock discipline and opposite
  lock nesting orders are diagnosed in STRICT; race-freedom is not claimed.
- Raw malloc/free UAF is detected for the documented single-TU shapes in
  STRICT (including indirect helpers and double-pointer indirection). General
  temporal safety across address reuse, threads and multiple TUs is not
  claimed; ASan remains a separate, host-dependent runtime check.
- No leak detection for raw/borrowed pointers.
- No cross-TU ownership ABI checking (single-TU compiler).
- Fields/containers with untracked element lifetimes unsupported.
- General arbitrary pointer provenance/casts outside the guarantee.
- goto/longjmp with no live resource only warn; exit() bypasses cleanup.
- No `unsafe` boundary marker; trusted interfaces are JSON contracts,
  cplus_check_live, explicit --sanitize.

## Arithmetic gaps
- Runtime-variable signed add/sub/mul, division/remainder by zero, `INT_MIN/-1`,
  invalid shift counts, value-changing narrowing conversions, and
  out-of-bounds pointer formation now have **native** guards in generated C
  (no UBSan dependency). Remaining blind spots: `__int128` widths, unsigned
  widths >32 bits, macro-generated operations, and header-location operations.
- Nested arithmetic helpers are not inferred beyond direct `return a OP b`.
- `if (d != 0)` style guards are not yet recognized (bare `if(d)` is).
- Division by runtime unknown is rejected in STRICT (fail-closed); DEFAULT
  emits the native runtime guard.
- Sign-change-only conversions (`(unsigned)-1`) are intentionally not checked.

## Integration findings (2026-11-23)

- **Verified race diagnostic false positive**: `tests/core/positive/thread_guard_ok.cp`
  emits `[CPLUS-FFI] multiple threads mutate shared global 'shared' without
  synchronization` although `Guard_lock` protects the write. The token layer
  suppresses it (substring `lock`/`guard`), the AST layer does not. Fix:
  recognize same-TU lock helper functions by body, not callee name.
- **Raw UAF escapes closed (single-TU, direct callees)**: all 12 evidence cases
  are now statically rejected in STRICT (10 unsafe + 2 safe controls accepted
  only when genuinely safe). The previously escaping shapes — callee
  parameter, `void*` cast, helper-returned alias, runtime array index — are
  rejected by cast-transparent alias groups, `returnedParam_` alias transfer,
  a released-pointer call-boundary rule, and a conservative runtime-index
  element rule. General temporal safety across indirect calls, integer casts,
  heap address reuse, threads and multiple TUs is still not claimed.
- **Element lifetimes**: constant-index field/array lvalues are already
  tracked, but runtime-indexed elements, scope-sensitive escape and
  indirect-call ownership are not; a sound minimal rule closes none of the
  25 `bugs51_97` gaps as written.
- **Cross-TU ownership**: unsupported by construction — one
  `buildASTFromCodeWithArgs` buffer per TU, per-TU diagnostics discarded. A
  manifest design exists but is not implemented.
- **Pre-existing robustness defect**: Safe-mode `c+ check` hangs on an
  unbalanced brace (reproduced with the older baseline compiler). Legacy mode
  rejects it. Root cause is in the token checker's bracket matching, not the
  new arithmetic instrumentation.

## Test-infrastructure gaps
- 11 top-level safety fixtures + 100 subdirectory sources are not asserted by
  an active runner (see test_inventory.md).
- 74 metrics scripts are not wired into an aggregator.

## Verified status (2026-11-26, agent/core-safety @defae65)

- Agent 1 runner: **198 PASS, 0 FAIL, 3 SKIP** (201 cases).
- Repo tests: **149 passed, 0 failed**; `ctest` **2/2**; semantic-adversarial
  **58/58**; pointer-provenance **14/14**; AST identity **9/9**; data-race
  **9/9**; RAII cleanup **21/21**; RAII shadowing **11/11**; Proof 1 UAF
  **4/4**; ABI gate **5/5**; abi_ownership **42/42**.
- The former 15 `compile_fail` diagnostic failures are fixed: expected
  constant-bounds/VLA messages are restored and asserted in both `check`
  and `build`; every `.err` fixture matches.
- The former 17/19 PR33 merge conflicts with `agent/modern-stdlib` were
  resolved in `99d31c7`; the combined tree passes ABI fingerprint match,
  libc 109/0, CRT all checks, coverage 92.51%/82.10%. The gate script now
  reports NOT_TESTED because the stdlib branch is contained in core-safety.
- CI: Linux x86-64 and macOS arm64 compiler jobs pass at PR head;
  SonarCloud still FAILS and PR #16 has 0 reviews. No automatic merge.


## CI gaps
- Linux x86-64 compiler CI passes. macOS arm64 job cancelled with zero steps
  (retired `macos-14` label per upstream changelog) - fix is to use macos-15.
- SonarCloud remains blocking: reliability/security E and ~25% duplication on
  new code, dominated by intentional unsafe test fixtures.
