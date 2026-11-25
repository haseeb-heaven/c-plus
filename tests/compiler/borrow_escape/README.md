# Borrow-escape regression suite (issue #26)

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/borrow_escape/run_suite.py
```

Negative fixtures assert a compile-time rejection and diagnostic substring from
both `c+ check` and `c+ build`; positive fixtures assert a successful build and
byte-exact stdout. Modes include DEFAULT, STRICT implicitly via the default
mode setting, RELEASE, DEBUG, `--sanitize=undefined`, and a LEGACY compatibility
control. Hard timeouts; no `|| true`, skips or suppressed failures.

The imported defect was that `RaiiLowering::checkReturnValue()` recognized only
a bare `return b;`. Casts, qualifier conversions, `void *` conversions,
typedefs, parentheses, conditional/comma expressions and pointer-returning
helpers all bypassed it. The fix records borrow -> stack-owner facts by
`VarDecl` identity in `AstSafetyCheck` and walks the full return expression with
Clang AST nodes; the token-level check was removed to avoid duplicate
diagnostics.

Evidence: `results/summary.txt`, `results/summary.json` (exact commands, rc,
stdout, stderr per case). Fixtures are regenerated into `cases/`.
