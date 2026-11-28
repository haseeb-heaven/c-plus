# Semantic safety inventory: token rewriting vs semantic analysis (v1)

Version 1, 2026-11-24. Scope: issue #29 compiler portion, branch `agent/core-safety`.
Evidence binary: `build/c+` (`C+ 0.1.0`, sha256 prefix `f92946cb0483c125684c4958`,
mtime 12:27 > both source files below, so it includes the working-tree edits).
Source read: `compiler/Sema/StaticBoundsCheck.cpp` (5226 lines) and
`compiler/Sema/CPlusRewriter.cpp` (5195 lines). No compiler source was changed.

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
CPLUS_BIN=$PWD/build/c+, `--strict --sanitize=none`, hard timeouts 30/90/15 s:
**51 PASS / 7 FAIL** (reject expectations 34/38, run expectations 17/20).
Raw evidence: `tests/compiler/semantic_adversarial/results/summary.{txt,json}`.

1. **Macro + integer laundering evades both token bounds and provenance checks**
   (`macro_cast_launder`): `LAUNDER(x) ((int*)(uintptr_t)(x) + 9)` is accepted;
   run exits 0 with stdout `1\n` (silent OOB read). Needs E + T + F, not token adjacency.
   Other direct integer-laundering cases *are* rejected (`launder_*`, 6/6 PASS).
2. **Parenthesized / nested ternary OOB branches are missed**
   (`cond_parenthesized_oob`, `cond_nested_oob`): accepted; runtime aborts with
   `C+ error: index 20 ...` / `index 99 ...` (rc=-6). The token ternary parser
   only recognises a top-level `?` inside `[]`. Needs real expression structure.
   Symmetrically, `cond_constant_safe` (`a[0?9:1]`) is a **false positive**:
   rejected although only index 1 is reachable.
3. **Name-keyed extents ignore declaration identity and byte/element type**
   (`scope_param_shadows_global`): parameter `int *a` called with `int b[64]`
   is rejected as index 9 past global `a[4]`. `byte_cast_safe` (`char* b = a; b[4]`)
   is rejected as index 4 past `a[4]` although 4 bytes are in range. Needs D + T.
4. **Loop induction OOB is only runtime-enforced** (`flow_while_loop_oob`):
   `while(i<10) a[i++]` accepted, runtime aborts at index 4. The `for` form is
   rejected, so the two loop shapes disagree. Needs C + F.

All 7 FAILs are listed verbatim in the suite summary; none is skipped or hidden.
