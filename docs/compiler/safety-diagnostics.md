# C+ safety diagnostic reference

Diagnostic families (stable prefixes): `CPLUS-ARITH`, `CPLUS-BOUNDS`,
`CPLUS-SAFE-BOUNDS`, `CPLUS-LIFETIME`, `CPLUS-SAFE-LIFETIME`, `CPLUS-OWNER`,
`CPLUS-FFI`, `CPLUS-REALLOC`, `CPLUS-BORROW`, `CPLUS-RAII`, `CPLUS-ALIAS`,
`CPLUS-LIBC`.

Emission sites: `compiler/Sema/StaticBoundsCheck.cpp` (`categoryOf`) and
`compiler/Sema/AstSafetyCheck.cpp` (`diagnosticCategory`), plus CFG
definite-initialization in `compiler/Analysis/OwnershipAnalysis.cpp`.

- **Errors** (both modes): bounds, UAF/double-free, invalid free, null
  dereference, definite-init, invalid API contracts.
- **WARN in DEFAULT / ERROR in STRICT**: format strings, `%n`, unbounded
  scanf `%s`, pointer provenance, potential UAF on a branch path, unverifiable
  pointer extents, arithmetic overflow/divide-by-zero/shift range.
- **Warnings only** (both modes): plain `setjmp` with no live resource,
  plain `goto` with no live resource.

Tag shape inconsistency (documented, not silently renamed): most AST bounds
diagnostics emit both `[CPLUS-BOUNDS] [CPLUS-SAFE-BOUNDS]`; the constant-index
diagnostic emits only `[CPLUS-SAFE-BOUNDS]`, while the token checker emits only
`[CPLUS-BOUNDS]`. Lifetime has a similar doubled-vs-single split.

Correction from the tag-inventory audit: this is **not** blocked by `.err`
fixtures. All 64 `tests/compile_fail/*.err` files pin message *substrings*
only, and both runners (`tests/run_tests.sh`, `tests/compiler/agent1/run_all.sh`)
use `grep -q`/`strstr`, so no fixture contains a literal `[CPLUS-*]` tag. The
actual constraint is documentation that asserts the emitted shapes verbatim
(this file, `README.md:112`, and the diagnostic inventory). Changing emission
therefore requires coordinated documentation edits, not fixture edits.

A full per-message inventory with exact strings, source lines, levels and test
evidence is in `tests/compiler/agent1/diagnostic_inventory.md`.
