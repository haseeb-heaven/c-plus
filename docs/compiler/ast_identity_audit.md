# AST/declaration-identity audit of safety-critical rewriter decisions

**Scope:** `compiler/Sema/CPlusRewriter.cpp`, `StaticBoundsCheck.cpp`,
`RaiiLowering.cpp`, `AstSafetyCheck.cpp`. **Mode:** read-only inventory plus
targeted AST conversions; no check was weakened to make a test pass. **C ABI:**
unchanged — all work is compile-time analysis or source lowering.

## What counts as a defect here

A safety-critical decision is unsound when it is keyed by an identifier
*spelling*, a raw token position, or a single hard-coded expression shape:
shadowing can bind the fact to the wrong object, a cast or macro can hide the
shape, and two scopes can share one fact. A decision that only recognizes a
narrow lexical construct (for example a literal `0` allocation size) is not
automatically a defect if it is deliberately conservative and covered by a
fixture.

## Already converted to declaration identity (do not re-audit)

| Area | Identity used | Evidence |
| --- | --- | --- |
| Borrow escape on `return` (issue #26) | `VarDecl*` facts, full AST walk through casts/conditionals/helpers | `tests/compiler/borrow_escape/` 25 cases |
| Pointer provenance through struct/union fields, nested fields, field arithmetic, conditional aliases, integer round-trips and identity helpers | canonical `VarDecl*`/`FieldDecl*` pairs; `returnedParam_` summaries; no name key for the new facts | `tests/compiler/pointer_provenance/` 14 cases |
| RAII `resource struct` shadow tracking | `OwnedVar`/`DeclId` identity | `tests/raii_shadowing/run.sh` |
| Definite initialization of fields/elements | `OwnershipAnalysis::SubKey{base, field, index}` with `FieldDecl*` | `tests/compiler/semantic_adversarial/` and `tests/core/` |
| One-past-end and heap interior | `VarDecl*`-keyed sets | `tests/core/`, `tests/safety/` |
| Frame escape, double-move, borrow-after-move | dedicated negative suites | `tests/compile_fail/`, `tests/safety/`, `tests/compiler/red_blue_round/` |

Those checks are skipped by this audit.

## Converted in this change

- **Provenance facts now use canonical declaration pointers**, never names
  (`AstSafetyCheck.cpp`: `identityKey`, `identityKeyForDecl`,
  `identityAllocs_`). A pointer stored into `s.p`, `o.i.p` or a union pointer
  member carries the source allocation's extent under the exact
  `(base VarDecl*, FieldDecl*)` key. Shadowed bases with the same spelling no
  longer share a fact.
- **GNU statement expressions are peeled structurally**
  (`stripStmtExpr`), so `cplus_new_array(T,N)` is recognized by the AST pass as
  the underlying `cplus_alloc_array(N,sizeof(T))` call instead of by macro
  spelling. Explicit pointer casts in allocator initializers are peeled too —
  `int *p = (int *)malloc(16)` remains tracked.
- **Conditional aliases inherit an extent only from matching arms**, using
  AST arm resolution through casts and declaration references; divergent arms
  stay unknown and fail closed in STRICT.
- **Identity-returning helpers reuse `returnedParam_`**, the existing
  interprocedural summary, so `(T *)(void *)p` return wrappers do not launder
  provenance. No new name-based rule was added.
- **Integer round-trips carry the source extent** only when the integer is
  provably derived from a tracked pointer; an arbitrary integer still fails
  the existing `pointer created from integer value has unverifiable
  provenance` gate.
- **Union type punning records a `provenanceLost` fact** for pointer members of
  the same union; STRICT rejects a later subscript or dereference.

## Remaining token/name-based decisions (OPEN, not claimed safe)

The following are still implemented in the legacy token passes. They are
listed with their concrete defeat so the gap is measurable. They are **not**
marked supported by STRICT; where an AST rule above covers the same source
shape, the AST rule is authoritative.

### `StaticBoundsCheck.cpp`

- Shape lookup (`findShape`/`findExtent`, `localArrays_`) picks the most recent
  earlier same-spelling declaration and has no scope: shadowed arrays can be
  bound to the wrong extent.
- Struct shape/type maps (`varStruct_`, `fieldFacts_`) are global by spelling;
  same-named locals of different struct tags contaminate each other.
- Constant maps (`localConstAt`, `phiConstAt`) and volatile-name suppression
  are file-wide by spelling, with no CFG joins; `if/else` assignments are
  folded in source order rather than by reaching definition.
- Guard recognition (`hasUpperGuard`, `hasNonNegativeGuard`,
  `collectRecursiveGuardBounds`) is a backward token scan for one literal
  spelling; `&&`, `||`, `else`, helper calls and negation are not parsed.
  Conjunctive guards can be both false positives and false negatives.
- Call-site resolution (function names followed by `(`, `.`/`->` skipped) does
  not resolve function pointers; `template<...>` declarations are not
  registered at all. The C+ front end is C-like; template sources are
  currently outside the verified subset.
- `checkDirectOnePastDeref` only matches `*(name + N)` literally; commuted or
  cast forms are left to the AST pass.
- `checkRawRealloc` recovers the LHS as one token before `=`, so
  `s->p = realloc(s->p, n)` is not diagnosed by that rule.
- `checkMacroStatements` skips lines containing string/char literals or `if`;
  macro-expanded operations are not resolved by the token pass.
- `checkPointerLeaks` and `checkNegativeComparison` are present but not called
  from `run()`; they are not coverage evidence.

Lexical-only rules that are deliberately narrow and covered by fixtures are
**SKIPPED** (zero allocation, NaN comparison, VLA `sizeof`, empty loop,
`fflush(stdin)`, ctype-on-char, fork-in-threads, and the direct identifier
rejections). See the individual `compile_fail/classic_bug*` fixtures.

### `RaiiLowering.cpp`

- Marker comments (`/*cplus$own:...*/`, `/*cplus$mem:...*/`) are trusted without
  binding to a declaration; the drop function is guessed from a `_drop`
  suffix.
- `findDroppedOwner` uses the last bare `&identifier` and ignores nested
  address forms; an identifier ending in `_drop` is treated as a destructor
  even when it is an ordinary function.
- Resource declarations are recognized by exact token spelling; `typedef`
  aliases, `static` and `struct R` forms are not lowered.
- `checkReturnValue` transfers ownership only for a bare `return x;`; casts
  and parenthesized returns are handled by the AST borrow check only when the
  value is a borrow of a stack owner, not for resource handles.
- `goto`/`longjmp`/`setjmp`/noreturn/termination handling uses label-name and
  ±32-token scans, and function-pointer aliases of terminators are not
  resolved.
- Plain-C name-keyed file state in `analyzeFunction` (`live`, `fileAliases`,
  `closedFiles`) has no scope identity; same-named locals in nested scopes can
  contaminate each other.

Already-proven RAII paths are skipped: identity-based resource shadowing,
conditional defer, explicit drop with a single cast, early-return ordering,
basic move/double-move/borrow-after-move, same-TU `setjmp` with owner, and
throw-in-try rejection (evidence listed in the table above).

### `CPlusRewriter.cpp`

- Class receiver, array length, dynamic-pointer and arithmetic-site maps are
  keyed by bare name; shadowed receivers/arrays/types can bind to the wrong
  class, length or guard. `self`/`this` replacement is textual inside methods.
- Default arguments are keyed by function name only, so a function pointer
  with the same spelling can receive a rewritten default argument.
- `throw f(...)` discards the call operand by a simple token heuristic.
- Exception lowering scans raw text and does not parse preprocessor lines, so
  a `try` inside a macro replacement can be rewritten incorrectly.

Skip list: fixed/runtime `.len` basics, simple alias length propagation,
checked-arithmetic emission internals and side-table fallback are covered by
`tests/cases/`, `tests/core/` and `tests/metrics/`.

## Verification policy

Every AST conversion in this change is backed by a negative fixture (must be
rejected in STRICT with the asserted diagnostic substring) and a positive
fixture (must build and run with exact stdout). Exact commands, exit codes,
stdout and stderr are recorded in
`tests/compiler/pointer_provenance/results/summary.txt`. Remaining token-only
decisions are reported as OPEN here rather than presented as compiler
guarantees.
