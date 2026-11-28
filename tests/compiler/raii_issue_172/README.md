# RAII qualified-borrow regression (issue #172)

`RaiiLowering::tryBorrowDeclaration()` previously recognised only the exact
token shape `T *p = &owner;`.  Any qualifier or type specifier made it return
`false`, so `const T *p = &owner;`, `struct T *p = &owner;`,
`T * const p = &owner;` and `volatile T *p = &owner;` were never entered into
`scopes_.back().borrows`.  A later `cplus_move(owner)` or escape through `p`
was therefore not diagnosed.

## Run

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/raii_issue_172/run_suite.py
```

`CPLUS_BIN` defaults to `<repo>/build/c+`.  Cases are generated into `cases/`
and verbatim command/output evidence is written to `results/`.

## Cases

- `neg_*`: each qualified/multi-token pointer spelling MUST be rejected by
  `c+ check --strict` with `borrow 'p' ... used after that resource was moved`,
  and `c+ build` MUST also reject it.
- `pos_*`: valid qualified borrows MUST build and run with exact stdout;
  `pos_non_pointer_const_copy` is a false-positive guard proving a qualified
  non-pointer declaration is not entered into the borrow map.
