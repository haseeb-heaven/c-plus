# Pointer provenance policy

C+ does not have a provenance-bearing pointer type. Pointer values are
ordinary C pointers; provenance is a compiler-side fact with an explicit
unknown state. No rule silently converts an unknown pointer into a bounded or
owned one.

## Representable provenance facts

| Fact | Source | Scope |
| --- | --- | --- |
| allocation base and byte extent | fixed array declaration, registered `malloc/calloc/realloc`, managed allocator | same TU; expression/alias propagation is partial |
| element count / one-past bound | array decay, `countof`, call-extent summaries | same TU |
| owner identity | `cplus_alloc` side table, `cplus_move`, RAII markers | same TU, locals |
| release generation | not implemented as a token; bounded recent-free cache only | runtime |

## Propagation rules

- Copy/alias: `int *q = p` and explicit pointer casts copy the recorded extent
  and owner identity (`VisitVarDecl` alias groups, `castedVarName` in
  `heapStateOf`).
- Array decay: the array's extent is attached to the decayed pointer value and
  substituted into callee parameters through `callParamExtents_`; conflicting
  call-site extents degrade to unknown and fail closed in STRICT.
- Heap factories: `return malloc(N)` and `return p` where `p` is initialized
  by `malloc/calloc` produce `returnedAllocExtent_` summaries.
- Pointer arithmetic: only simple side-effect-free `p + i` with a provable
  extent is guarded by `cplus_check_ptr_formation`; one-past formation is
  legal, dereference is not.
- Cast: a pointer-to-pointer cast preserves provenance; integer round-trips
  are accepted only in narrow bitwise forms. Arbitrary provenance laundering
  is not claimed.

## Unknown-provenance policy

1. **No synthesized bounds.** Never use `sizeof(pointer)`, `1`, `256`, or any
   default as an inferred extent. `tryEmitArrayLen` diagnoses an untracked
   pointer instead of emitting a fabricated length.
2. **STRICT fails closed** on a subscript or formation whose extent is
   unknown. DEFAULT warns where the analyzer has a rule and stays silent
   where it has none; LEGACY performs no analysis.
3. **ABI-visible pointers are opaque.** A pointer received from C, a callback,
   a precompiled archive, or an incomplete-struct handle has unknown extent,
   ownership and release effect. It may be passed, compared and stored; it is
   not dereferenced or bounded by C+ inference.
4. **Allocator identity matters.** Raw C memory is released only by the routed
   `cplus_free_registered`; C+ managed memory only by `cplus_release`/RAII.
   Cross-allocator release is diagnosed or trapped.

## Known limitations

- Runtime-indexed elements, `int **` and struct pointer fields lose extents;
  those cases are `asan_only` in `tests/compiler/runtime_memory_safety/`.
- Integer-to-pointer casts with arbitrary bit manipulation are not validated;
  `VisitCastExpr` accepts a conservative bitwise round-trip shape.
- There is no generation token, so the 4096-entry recent-free cache can
  misattribute a stale alias after address reuse.
- No cross-TU or whole-program provenance; no race-safe provenance under
  concurrent mutation.

Upstream inspiration (design only, no source copied): SoftBoundCETS
base/bound/key/lock and propagate-on-GEP discipline, CHERI provenance selection
rules, Apple one-way unsafe boundary, Checked C bounds casts. See
`docs/compiler/runtime-safety-upstream-reuse.md`.
