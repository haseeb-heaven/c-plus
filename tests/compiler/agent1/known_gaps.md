# Known gaps (documented, not hidden)

Sources: docs/LIMITATIONS.md, SAFE_SUBSET.md, CRITICAL_ISSUES.md,
tests/known_gaps/README.md, and docs/RUST_COMPARISON.md.

## Safety-model gaps
- No full Rust-style borrow checker/lifetime annotations; borrows tracked only
  for simple local `T *p = &owner`.
- No general data-race verification; only same-TU global-write heuristics.
- Raw malloc/free UAF not detected in default builds (ASan opt-in needed).
- No leak detection for raw/borrowed pointers.
- No cross-TU ownership ABI checking (single-TU compiler).
- Fields/containers with untracked element lifetimes unsupported.
- General arbitrary pointer provenance/casts outside the guarantee.
- goto/longjmp with no live resource only warn; exit() bypasses cleanup.
- No `unsafe` boundary marker; trusted interfaces are JSON contracts,
  cplus_check_live, explicit --sanitize.

## Arithmetic gaps
- Runtime-variable signed overflow is accepted (no native check inserted).
- Nested arithmetic helpers are not inferred beyond direct `return a OP b`.
- `if (d != 0)` style guards are not yet recognized (bare `if(d)` is).
- Division by runtime unknown is rejected in STRICT (fail-closed).

## Integration findings (2026-11-23)

- **Verified race diagnostic false positive**: `tests/core/positive/thread_guard_ok.cp`
  emits `[CPLUS-FFI] multiple threads mutate shared global 'shared' without
  synchronization` although `Guard_lock` protects the write. The token layer
  suppresses it (substring `lock`/`guard`), the AST layer does not. Fix:
  recognize same-TU lock helper functions by body, not callee name.
- **Raw UAF escapes**: 4 of 12 evidence cases (callee parameter, `void*` cast,
  helper-returned alias, runtime array index) pass `c+ check --strict` and are
  detected only by opt-in ASan. See `tests/metrics/uaf_evidence_run.py`.
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

## CI gaps
- Linux x86-64 compiler CI passes. macOS arm64 job cancelled with zero steps
  (retired `macos-14` label per upstream changelog) - fix is to use macos-15.
- SonarCloud remains blocking: reliability/security E and ~25% duplication on
  new code, dominated by intentional unsafe test fixtures.
