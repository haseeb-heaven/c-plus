# Feature: SafeCC-style CFG dataflow verification (issue #180)

Regression suite for the additive compile-time dataflow pass in
`compiler/Sema/StaticBoundsCheck.cpp` (`Checker::checkCfgDataflow`).

The pass is a structured CFG over the C+ token stream:

- `if` / `else` fork the must-fact state and join at the merge point;
- `while` / `for` close a back-edge that is iterated to a fixed point;
- unknown shapes, calls, `switch`, `goto` and address-taking kill facts rather
  than being guessed.

Two findings are provable and therefore rejected:

1. `[CPLUS-SAFECC] definite null dereference of '<name>'` — a pointer is
   `NULL` on *every* path reaching a `*p`, `p->f` or `p[i]` access.
2. `[CPLUS-SAFECC] definite memory leak: allocation held by '<name>'` — a
   raw `malloc`/`calloc` result is still live on *every* function exit path.

Both are compile-time only: no runtime check, pointer representation, struct
layout or C ABI symbol is changed.

## Running

```sh
CPLUS_BIN=/path/to/c+ python3 tests/compiler/feature_safecc/run_suite.py
```

`neg_*` cases must be rejected with the asserted `[CPLUS-SAFECC]` fragment;
there is deliberately no fallback to the older `[CPLUS-LIFETIME]` null-deref
diagnostic, so a case that only trips the lifetime pass fails the suite.
`pos_*` cases must compile cleanly. The suite uses `c+ check --sanitize=none`
only — no build, network, game assets or live injection.

Coverage notes:

- `neg_null_deref_arrow.c` and `neg_null_deref_arrow_join.c` exercise the `->`
  dereference path in `Checker::analyzeFlowRange` (`StaticBoundsCheck.cpp`).
  The join variant proves the fact via the CFG merge, which the lifetime pass
  does not model.
- `pos_sizeof_star_no_eval.c`, `pos_sizeof_subscript_no_eval.c` and
  `pos_sizeof_arrow_no_eval.c` exercise `Checker::flowInsideSizeof` for
  `sizeof *p`, `sizeof(p[0])` and `sizeof(p->x)` respectively. Each makes `p`
  must-NULL at the `sizeof` so the pass would report a defect if it forgot that
  the operand is unevaluated.
