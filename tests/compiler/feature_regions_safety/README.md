# Region / spatial-safety / RAII feature regressions (#183, #184, #185)

Run:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/feature_regions_safety/run_suite.py
```

The suite writes its fourteen deterministic cases into `cases/` and records
exact commands, exit codes, stdout/stderr and emitted-C inspections in
`results/summary.txt` and `results/summary.json`.

* **#183** — `dtor T x = init;` lowers to the ordinary declaration plus the
  existing `/*cplus$own:...*/` marker; `guard stmt;` lowers to the existing
  `defer` machinery with a one-shot flag. Both emit direct static C calls and
  add no hidden struct field, vtable or heap cleanup record. The documented
  generic container spelling `generic Vec<T>;` is monomorphised per element
  type into `struct Vec_int` / `struct Vec_double` with exactly the three
  fields `T *data; size_t len; size_t cap;` and three `static inline` C
  helpers (`*_init`, `*_at`, `*_len`). `pos_generic_monomorph` checks the
  concrete layout and signatures on the emitted C (`inspect-generic-abi`) as
  well as at runtime. A `goto` that leaves a block with live owners now emits
  their drops at the jump site, before the branch; `pos_goto_owner_drops`
  checks the drop/jump order and that the owner's C layout is unchanged
  (`inspect-goto-drop-order`). STRICT diagnostics on `goto` are unchanged —
  this is an additive emission, not a policy change.
* **#184** — `@notnull` and `@numelts(n)` are erased from emitted C. Constant
  OOB subscripts and null arguments are rejected statically; dynamic accesses
  use the existing `cplus_check_bounds` / `cplus_check_nonnull` hooks, so the
  C parameter list stays `(T *, size_t)`.
* **#185** — `region r(bytes) { ... }` creates a stack lifetime token backed by
  one checked-heap block. `cplus_region_alloc(r, n)` is a bump take returning
  a thin `T *`; pointers escaping the region via a global/out-parameter or
  `return` are rejected with `[CPLUS-LIFETIME]`, and release is checked by the
  token's magic field (double release is a no-op).
* **Location restore** — `pos_region_line_location` checks that the synthetic
  `#line 1 "<cplus-region-prelude>"` is followed by a `#line` that restores the
  original file/line: `__LINE__` prints source line 6 and a `#warning` on source
  line 3 is reported against the `.cp` file, never against the prelude.

No case is skipped; every failure is reported.
