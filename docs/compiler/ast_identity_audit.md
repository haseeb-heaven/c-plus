# AST/declaration-identity audit of safety-critical rewriter decisions

**Scope:** `compiler/Sema/CPlusRewriter.cpp`, `StaticBoundsCheck.cpp`,
`RaiiLowering.cpp`, `AstSafetyCheck.cpp`. **Mode:** read-only inventory plus
targeted AST conversions; no check was weakened to make a test pass. **C ABI:**
unchanged — all work is compile-time analysis or source lowering.
Reconciled 2026-11-25 against the current suite summaries; this document edit
changed no compiler source and ran no suite.

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
- **Fixed-array shape facts are scope-fenced** (`StaticBoundsCheck.cpp`:
  `ArrayShape::scopeEnd`, `enclosingScopeEnd`, checked in `findShape`). A shape
  declared in an inner block is ignored once the token position is past that
  block's closing brace, so a dead inner `int a[4]` no longer rejects an access
  to an outer `int a[8]`. This is a **token-scope fence, not declaration
  identity**: two same-named objects that are both live still share a spelling.
  Evidence: `tests/compiler/ast_identity/` 9/9 and the
  `scope_outer_after_inner` / `scope_inner_shadow_safe` cases in
  `tests/compiler/semantic_adversarial/`.

## Remaining token/name-based decisions (OPEN, not claimed safe)

The following are still implemented in the legacy token passes. They are
listed with their concrete defeat so the gap is measurable. They are **not**
marked supported by STRICT; where an AST rule above covers the same source
shape, the AST rule is authoritative.

For `AstSafetyCheck.cpp`, heap-provenance facts that used to be only name-keyed
(`heapAllocs_`) now have a parallel `identityAllocs_` view keyed by canonical
`VarDecl*`/`FieldDecl*` (`identityKeyForDecl`, `identityKey`, `namedVars_`);
`heapStateOf` prefers the name-keyed entry only when it still belongs to the
same canonical declaration. Those facts are therefore **not** listed as fully
OPEN here. The name-keyed decisions that remain in the token passes are below.

### `StaticBoundsCheck.cpp`

- Shape lookup (`findShape`/`findExtent`, `localArrays_`) is still name-keyed:
  it picks the most recent earlier same-spelling declaration. It is now
  **scope-fenced** (`scopeEnd` / `enclosingScopeEnd`, see "Converted in this
  change"), so a shape from an already-closed inner block is skipped, but the
  fence is positional, not declaration identity. **KNOWN_OPEN:** two same-named
  objects that are both live (for example a parameter shadowing a global:
  `scope_param_shadows_global` in the adversarial suite) can still bind to the
  wrong extent, and the byte/element distinction is not modelled here.
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
throw-in-try rejection (evidence listed in the table above). Multi-declarator
owning declarations are now rejected in STRICT with `declare one owning
resource per statement` (`RaiiLowering.cpp`, 4617c7c); control-flow cleanup and
destructor order are pinned by `tests/compiler/raii_cleanup/` 21/21. The
token-scan limits for non-local exits above remain KNOWN_OPEN.

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
stdout and stderr are recorded in each suite's `results/summary.{txt,json}`.
Current counts (2026-11-25) are fixture evidence, not a completeness claim:

| Suite | Current result | Scope pinned |
| --- | --- | --- |
| `tests/compiler/pointer_provenance/` | 14/14 PASS | declaration-identity provenance: struct/union fields, field arithmetic, conditional aliases, integer round-trips, identity helpers |
| `tests/compiler/ast_identity/` | 9/9 PASS | scope fence, typedef arrays, macro indices, cast aliases, function-pointer calls, conjunctive guards, realloc via a field, template shapes |
| `tests/compiler/data_race/` | 9/9 PASS | helper-global races and lock discipline in `AstSafetyCheck.cpp` |
| `tests/compiler/raii_cleanup/` | 21/21 PASS | multi-declarator owner rejection; goto/setjmp/longjmp limits; destructor-counter order |

Remaining token-only decisions are reported as OPEN above, not presented as
compiler guarantees. The adversarial suite now reports **0 FAILs** in 58
cases (`tests/compiler/semantic_adversarial/results/summary.txt`); see
`docs/compiler/semantic_safety_inventory.md` §3 for the measured evidence and
the remaining KNOWN_OPEN token decisions listed above.
