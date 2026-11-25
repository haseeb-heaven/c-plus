# Semantic safety inventory: token rewriting vs semantic analysis (v2)

Version 2, 2026-11-25 (reconciled to current measured evidence). Scope: issue
#29 compiler portion, branch `agent/core-safety` at 4617c7c.
Evidence binary: `build/c+` (`C+ 0.1.0`, sha256 prefix `1ea8c30d2d291c5035a00aa`,
mtime 2026-11-25T14:01:29+0530 > all source files below, so it includes the
working-tree edits). The generated suite in §3 names this same binary path.
Source read: `compiler/Sema/StaticBoundsCheck.cpp` (5319 lines) and
`compiler/Sema/CPlusRewriter.cpp` (5236 lines), plus
`compiler/Sema/AstSafetyCheck.cpp` for the declaration-identity view.
No compiler source was changed by this inventory.

Notation for "needs": **S** = identifier spelling / token adjacency only;
**P** = source-position-keyed name facts (same token model, with invalidation);
**D** = declaration identity + scope; **T** = types (incl. byte vs element size);
**E** = parsed expression structure (casts, macro expansion, ternary, comma);
**C** = CFG / loop structure; **F** = interprocedural data flow.
"Token-only" means S/P: it can be fooled by spelling, shadowing or expression shape.
The tables name every safety-critical check; related checks are grouped by row.

## 1a. `StaticBoundsCheck.cpp` — inventory (all checks run on lexer tokens)

| Check(s) | What it decides | Basis | Semantic facts actually needed |
| --- | --- | --- | --- |
| `collectLocalArrays`, `findShape`, `findExtent`, `checkSubscriptChain`, `checkFoldedIndexSubscript`, `checkDirectOnePastDeref` | literal/interval index OOB in fixed arrays and multidim rows | S/P: name + preceding type word, literal dims, position order | D (shadowing), E (paren/cast/comma), T (element size) |
| `indexExprValue`, `evalIndex`, `novelIndexValue`, `stripCast`, `checkConstantVlaSubscript` | folds `+ - ?: ()`, casts and local/return constants into an index value | S: token arithmetic, no parsed AST | E (real ternary semantics), D (which constant), T (cast width) |
| `collectLocalConstants`, `collectPhiConstants`, `collectConstantVlaExtents`, `checkPhiIndexSubscript`, `checkTernaryIndexSubscript` | constant/phi index facts; rejects an OOB ternary branch | S/P: name-keyed values, `max(a,b)` for unknown condition | D, E (short-circuit/constant condition), F (which branch is live) |
| `collectArrayPointers`, `collectByteCastAliases`, `checkTrackedArrayPointer`, `checkPointerArithmetic`, `checkUnrelatedPointerCompare` | alias offset, one-past deref, byte-cast alias extent, pointer arithmetic/compare | S/P: `p = a + k` text, name lookup, number tokens | T (byte vs element), E (casts/macro), F (reassignment), D |
| `collectPointerToPointerParams`, `checkPointerToPointerSubscript`, `collectStructShapes`, `collectStructTypedVars`, `collectFieldInitializers`, `propagateFieldFactsToParams`, `checkStructFieldSubscript` | `(*pp)[k]` and `s.p[k]` extents/index | S/P: struct text, field-name string, assignment adjacency | D (object identity), T (field type), F (field store) |
| `collectParamConstBounds`, `collectParamArrayExtents`, `collectCallGraph`, `collectRecursiveCallArgs`, `collectRecursiveGuardBounds`, `hasUpperGuard`, `checkParamIndexSubscript`, `checkRecursiveArraySubscript`, `checkRecursivePointerParamSubscript`, `checkOutParamHeapSubscript`, `collectOutParamAllocations` | call-site/reachability bounds for pointer params, recursion, `make(&p)` extents | S/P: function name, parameter position, textual guards | D (parameter vs global of same name), F (call graph, actuals), C (guard reachability) |
| `collectAllocations`, `propagateReallocAliases`, `checkRawRealloc` | allocation extents, realloc-shrink invalidation, raw realloc rejection | S/P: allocator name + argument tokens | T, F (alias copies), D |
| `collectReturnExtents`, `propagateReturnExtents`, `collectCallConstants`, `collectVolatileNames`, `collectPointerNames`, `collectRawAllocationPointers`, `collectTypePunnedPointers`, `collectSizeConstants`, `collectNegativeNames`, `collectGlobals`, `collectFunctions` | supporting name/position facts; `volatile` suppression | S/P only | D, T, F to be sound |
| `checkWhileLoop`, `checkLoop`, `parseInit`, `parseCondition`, `parseStep` | loop induction variable past extent | S: `{init;cond;step}` text pattern | C (actual loop bounds/exits), E, D |
| `checkTypePun`, `checkTypePunnedDeref`, `checkUnalignedCasts`, `checkConstCastMutation`, `checkSizeofPointer` | type-punned/unaligned writes, const-cast mutation, `sizeof(pointer)` | S: cast shape and adjacent tokens | T (pointee type, alignment, constness), E |
| `checkAllocationOverflow`, `checkNegativeToUnsigned`, `evalUnsignedExpr` | size_t wrap, signed→unsigned negative length | S: literal/unsigned-name folding | T (integer width/signedness), E |
| `checkDataRaces`, `checkForkThreads`, `checkRestrictAliasing` | unsynchronized global writes, fork with threads, restrict overlap | S/P: global-name sets, call names, argument text | D, T, F, C (thread/lock state) |
| `checkStaticBufferCalls`, `checkGetenvInvalidation`, `checkCtypeChar`, `checkUncheckedIo`, `checkMemcpyOverlap`, `checkFflushStdin`, `checkZeroAlloc`, `checkNanCompare`, `checkVlaSizeof` | libc API misuse patterns | S: callee name + argument adjacency | T, F (buffer extent), E |
| `checkUnsequencedModifications` | `i++ + ++i`, `f(i++,i++)` on original tokens (before lowering hides it) | S: comma/paren/&&/||/?:/; token scan | E (sequence points), D |
| `checkMacroStatements` | multi-statement `#define` replacement lists | S: raw source lines containing `#define` | E (macro expansion), T (grouped `do{}while`) |
| `run()` inline identifier checks | reject `alloca`, `unreachable`, `_BitInt`, `nullptr`, `unsequenced`/`reproducible`; warn `setjmp`/`longjmp` | S: single identifier token + directive-line test | T, E (real call vs name), D |
| `checkVla` | no-op by design (Safe mode lowers VLAs) | none | — |
| `checkPointerLeaks` | defined but **never called** from `run()` — dead code, not a shipped guarantee | — | — |

## 1b. `CPlusRewriter.cpp` — inventory (source rewriting + guard emission)

| Check(s) / stage | What it decides | Basis | Semantic facts actually needed |
| --- | --- | --- | --- |
| `collectDeclarations`, `parseClass`, `parseResource`, `emitClass`, `emitResource`, `emitMethodBody` | class/resource/RAII lowering, missing return types, ctor rules | S: token pattern after `class`/`resource` | D, T (AST class table for external TUs) |
| `collectVariableTypes`, `looksLikeOwnedDeclaration`, `isDeclarationName`, `emitOwnedDeclaration` | `owned`/`managed` ownership transfer and cleanup emission | S: `[boundary] KEYWORD QUALS TYPE * NAME = init;` | D, T (resource type), E |
| `collectArrays`, `lengthExprFor`, `tryEmitCountof`, `tryEmitArrayLen`, `tryEmitRangeFor` | array length used for generated `.len`/range-for/`.len()` guards | S/P: `arrays_[name]` and `dynamicLengths_`, name-keyed | D (shadowing), T, F |
| `collectDynamicPointers`, `collectAliasBoundsSites`, `resolveAliasExtent`, `skipAliasCasts` | runtime bounds sites for heap/VLA/array aliases, rows, fields | S/P: allocator-name set, cast-skip, name-keyed extent maps | D, T, F, E |
| `collectTokenArithmeticSites`, `collectNonnullDerefSites`, `lowerVariableLengthArrays` | fallback checked-arithmetic / nonnull / VLA sites when AST sites are absent | S: token shapes in generated C | T, E, C |
| `instrumentDynamicArrayBounds` (emits `cplus_bounds_check`, checked arithmetic, alloc/free and pointer sites) | inserts the actual runtime guards into lowered C | AST-registered sites **merged** with the S/P fallbacks above | AST site truth + T; guards are RUNTIME_ENFORCED |
| `tryEmitDefaultDefinition`, `tryEmitDefaultCall`, `tryEmitAuto`, `tryEmitIfDecl`, `tryEmitOctal`, `tryEmitMethodCall`, `tryEmitCtorCall` | syntax rewrites affecting generated control flow/initialization | S: token adjacency | D, T |
| `lowerThrows` / exception scope guard | exception lowering and cleanup path | S on lowered text | C (try/catch regions), RAII state |
| `diags_.error` in `Lowerer` | malformed class/resource, missing default arg, range without `.len` | S: token pattern | T, D |

## 2. Classification of shipped guarantees

| Guarantee | Class | Enforcement | Honest note |
| --- | --- | --- | --- |
| fixed-array literal/constant index OOB, one-past deref, negative index | SEMANTIC in effect, SYNTACTIC/TOKEN in implementation | `StaticBoundsCheck.cpp` compile-time error (DEFAULT and STRICT) | correct on the fixtures, but keyed by name/position, not decl identity |
| call-site/recursive/out-param/struct-field bounds | SYNTACTIC/TOKEN | compile-time error | name-keyed facts; same-named parameter can inherit a global's extent (FP) |
| dynamic array/pointer-alias bounds | RUNTIME_ENFORCED | `CPlusRewriter` inserts `cplus_bounds_check`; abort on violation | abort is not a compile-time guarantee; some expressions never get a site |
| checked arithmetic, alloc/free, nonnull, VLA | SEMANTIC sites + SYNTACTIC fallback + RUNTIME_ENFORCED | AST-registered sites plus token collectors | token fallback can miss semantic operations |
| definite init, RAII cleanup, ownership/UAF/double-free, FFI contracts | SEMANTIC (AST/CFG/contract passes, outside these two files) | compile-time and runtime | not re-invented in the token pass |
| macro arithmetic in STRICT | UNSUPPORTED (fail-closed) | STRICT error `arithmetic in a macro expansion cannot be instrumented` | intentional; not a soundness claim |
| `setjmp`/`longjmp` without live resource, plain `goto` | UNSUPPORTED (warning only) | `StaticBoundsCheck` warning | DEFAULT accepts; cleanup not guaranteed |
| multi-statement macros, unsequenced modifications | SEMANTIC (targeted token scan) | compile-time error | scans original text; no full expression AST |
| `checkPointerLeaks`, `checkVla` | UNSUPPORTED | not wired / no-op | not a shipped guarantee |

## 3. Measured residual risks (do not claim these are fixed)

Suite: `tests/compiler/semantic_adversarial/run_suite.py`, 58 cases,
CPLUS_BIN=$PWD/build/c+, `--strict --sanitize=none`, hard timeouts 30/90/15 s,
generated 2026-11-25T16:17:12Z:
**58 PASS / 0 FAIL** (reject expectations 38/38, run expectations 20/20;
reject_failures=0, run_failures=0).
Raw evidence: `tests/compiler/semantic_adversarial/results/summary.{txt,json}`
(counts block and verbatim FAILURES list). Corroborating suites are in §4.

Previously measured failures are now closed in source and pinned by fixtures:
macro integer laundering, parenthesized/nested conditional indices, counted
`while` induction, byte-cast element units, live parameter shadowing, constant
condition/comma folding and the comma-instrumentation warning. The suite is
evidence for its 58 fixtures only, not a completeness or soundness claim.

## 4. Corroborating suites (current evidence, not proof of completeness)

These suites ran against the same `build/c+` binary; counts are from their
`results/summary.txt`, and each case records exact commands, exit codes, stdout
and stderr with hard timeouts and no skips:

| Suite | Current result | What it pins |
| --- | --- | --- |
| `tests/compiler/pointer_provenance/` | 14/14 PASS | canonical `VarDecl*`/`FieldDecl*` provenance through struct/union fields, field arithmetic, conditional aliases, integer round-trips and identity helpers |
| `tests/compiler/ast_identity/` | 9/9 PASS | scope-fenced shadowed arrays, typedef arrays, macro indices, cast aliases, function-pointer calls, conjunctive guards, realloc via a struct field and template-shaped sources |
| `tests/compiler/data_race/` | 9/9 PASS | AST-based helper-global races and lock discipline in `AstSafetyCheck.cpp`; locked/atomic/consistent-order safe cases |
| `tests/compiler/raii_cleanup/` | 21/21 PASS | multi-declarator owner rejection; goto/setjmp/longjmp diagnostics; exact destructor-counter order for loops, switches, returns, moves and generated owner chains |

A green suite is evidence for its fixtures only. The token-based inventory in §1 and the OPEN items in
`docs/compiler/ast_identity_audit.md` remain KNOWN_OPEN; none of these tables
should be read as a completeness or soundness claim.
