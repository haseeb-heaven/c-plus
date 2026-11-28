# RAII lowering and ownership cleanup audit

**Claim boundary.** STRICT rejects the unsupported non-local exits listed
below; a clean check is not a proof that destructors run after an unmodelled
non-local jump. Plain C semantics and the native C ABI are unchanged.

## Already verified — skipped

- Declaration-identity shadowing (`OwnedVar`/`DeclId` in
  `RaiiLowering.cpp`): `tests/raii_shadowing/run.sh` — 11 cases, 0 failed
  (basic, deep, branch, loop, return, deep-break, move, partial, struct,
  cross-TU, non-owning control).
- Loop/switch/branch cleanup and destruction order: `tests/core/run.sh`
  (`loop_cleanup`, `raii_switch_continue`, `nested_raii` seq==34251,
  `raii_early_return`) and `tests/stress/cplus_stress_10/`.
- Return/break/continue ordering: `repro_shadow_return`,
  `repro_shadow_break_deep`, breaker `02_defer_return_order`.
- Direct same-TU `setjmp`/`longjmp` with a live owner:
  `tests/safety/longjmp_owner_reject.cp`.
- throw/try/catch and throwing helpers: `tests/compiler/red_blue_round/`
  (`raii__01`, `raii__05`) and `tests/compiler/exceptions/` (100 fuzz cases).
- Use-after-move, double drop and borrow-after-move: `tests/compile_fail/`,
  `tests/safety/`, `tests/core/positive/`.
- Documented limits: `docs/compiler/ast_identity_audit.md`,
  `docs/compiler/red_blue_round/`.

## Fixed in this change

`RaiiLowering::tryResourceDeclaration()` stopped scanning a declaration at
the first `=` token, so a second declarator after the initializer
(`R a={1}, b={2};`) was never seen: neither owner was registered and no
destructor was emitted. The scan now records the first initializer but keeps
looking for a top-level comma, so STRICT rejects the statement with
`declare one owning resource per statement`. Each owning declaration must be
a separate statement, which also makes destruction order unambiguous.

## New evidence

`tests/compiler/raii_cleanup/run_suite.py` — 21 cases:

- 5 negatives: multi-declarator owner, `goto` into and out of an owning
  scope, `x = setjmp(...)` with a live owner, and `longjmp` from a callback;
- 16 positives with exact destructor-counter stdout: nested shadow order
  (`D2 END D1`), loop cleanup, switch/break, return order, move transfer,
  continue cleanup, explicit two-owner helper drop, separate declarations,
  and generated chains/branches of 1–16 owners.

Exact commands, exit codes, stdout and stderr:
`tests/compiler/raii_cleanup/results/summary.{txt,json}`.

## Still OPEN (not claimed)

- Marker comments (`/*cplus$own:...*/`) are trusted without binding to a
  declaration; the drop function is guessed from a `_drop` suffix.
- `findDroppedOwner` recognises the last bare `&identifier`; nested address
  forms are not bound through the AST.
- `typedef`/`static`/`struct R` resource declaration forms are not lowered.
- `checkReturnValue` transfers ownership only for a bare `return x;`.
- `goto`/`longjmp`/`setjmp` diagnostics still use token scans; a non-local
  exit hidden behind a function pointer alias or another TU may escape.
  STRICT rejects the forms with tests; absence of a diagnostic is not a
  general proof.
- Managed allocation leaked by `throw` remains documented KNOWN_OPEN in the
  red/blue suite.
