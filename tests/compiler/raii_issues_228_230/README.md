# RAII loop-stack, by-value and return-lowering regressions (issues #228, #229, #230)

Deterministic regressions for three `compiler/Sema/RaiiLowering.cpp` bugs:

- **#228** — a single `doAwaitingWhile_` flag let an inner `while`/`do` steal
  a `do`'s trailing `while`, and braces in a loop header could retire the loop
  frame before its body. Both made `break`/`continue` skip or duplicate drops.
- **#229** — by-value resource parameters were dropped only at the function's
  closing brace (early returns leaked), and `passedByValueFrom()` suppressed a
  caller's drop across unrelated functions and untaken branches.
- **#230** — `insertReturnCleanup()` replaced the whole `return expr;` span,
  silently discarding edits emitted inside the expression; `__typeof__(expr)`
  also broke array returns.

Every case builds with `c+ build --default --sanitize=none`, runs the produced
binary and checks exact stdout. Cases with a `text` assertion also inspect the
lowered C from `--emit-c`, because an idempotent destructor can hide a lost
zeroing guard at runtime.

## Run

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/raii_issues_228_230/run_suite.py
```

`CPLUS_BIN` defaults to `<repo>/build/c+`. Generated sources live in `cases/`;
verbatim command/output evidence is written to `results/`. No case is skipped.
