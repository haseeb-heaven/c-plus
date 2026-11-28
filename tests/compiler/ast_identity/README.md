# AST/declaration-identity audit suite

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/ast_identity/run_suite.py
```

The suite pins the scope fence for shadowed fixed arrays, typedef arrays,
macro-expanded indices, cast aliases, function-pointer calls, conjunctive
guards, realloc through a struct field and template-shaped sources. It does
not duplicate the provenance, borrow-escape, RAII-shadowing or default-policy
suites.

`docs/compiler/ast_identity_audit.md` records the remaining token/name-only
decisions that are deliberately reported OPEN rather than claimed safe.
Evidence: `results/summary.txt` and `results/summary.json` (exact commands,
exit codes, stdout, stderr; hard timeouts, no skips).
