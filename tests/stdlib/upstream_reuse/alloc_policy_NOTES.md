# Upstream reuse — mimalloc size-overflow / size-class technique

## Provenance (fetched 2026-11-23)

| Item | Exact URL | Result |
|---|---|---|
| Allocation call sites | `https://raw.githubusercontent.com/microsoft/mimalloc/master/src/alloc.c` | HTTP 200, 30,835 bytes, blob SHA (ETag) `ee4783ccfb7974c80e563c52d3fb3e374c7f16fc5dff5b5548cc0da8bafaf5d9` |
| Overflow / rounding primitives | `https://raw.githubusercontent.com/microsoft/mimalloc/master/include/mimalloc/internal.h` | HTTP 200, blob SHA (ETag) `b8154bfad2f738d586ce2aeb7d076d152473604b17f8552144efd1158930ecaf` |
| License | `https://raw.githubusercontent.com/microsoft/mimalloc/master/LICENSE` | HTTP 200, blob SHA (ETag) `d8b2a954dac0c7196af1e44cba66cbd8d311d9952b9171d0ecd5c0e4b7dd42eb` |
| Refs | `https://github.com/microsoft/mimalloc.git/info/refs?service=git-upload-pack` | `refs/heads/main` = `c683b7c60c91cb2809977e37d3f4d084d7497180`; no `refs/heads/master` exists — GitHub's raw `master` path resolves to `main` (verified: `raw/master` and `raw/main` return byte-identical `src/alloc.c`, both with the ETag above). |

The task's suggested primitive path `https://raw.githubusercontent.com/microsoft/mimalloc/master/src/prim/prim.h` returned **404**. The relevant primitives are in `include/mimalloc/internal.h` (confirmed by `src/alloc.c` including `mimalloc/internal.h`); that file was fetched instead. `include/mimalloc/types.h` was also inspected for allocation-limit context (blob SHA `b69df68dc28f295aad6d11e334bca1cb25e003fc19f7ff50468931e542cb19ba`).

License: **MIT**. The source headers say `Copyright (c) 2018-2024, Microsoft Research, Daan Leijen`; the root `LICENSE` says `Copyright (c) 2018-2025 Microsoft Corporation, Daan Leijen`. Exact fetched LICENSE text:

```
MIT License

Copyright (c) 2018-2025 Microsoft Corporation, Daan Leijen

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

The full notice and both copyright lines are reproduced in `stdlib/alloc_policy.hp`.

## Exact upstream source lines (at the revision above)

`include/mimalloc/internal.h`:

```c
404: // Align upwards
405: static inline uintptr_t _mi_align_up(uintptr_t sz, size_t alignment) {
406:   mi_assert_internal(alignment != 0);
407:   const uintptr_t mask = alignment - 1;
408:   if ((alignment & mask) == 0) {  // power of two?
409:     return ((sz + mask) & ~mask);
410:   }
411:   else {
412:     return (((sz + mask)/alignment)*alignment);
413:   }
414: }
...
462: // i.e. byte size == `wsize*sizeof(void*)`.
463: static inline size_t _mi_wsize_from_size(size_t size) {
464:   mi_assert_internal(size <= SIZE_MAX - sizeof(uintptr_t));
465:   return (size + sizeof(uintptr_t) - 1) / sizeof(uintptr_t);
466: }
467:
468: // Overflow detecting multiply
469: #if __has_builtin(__builtin_umul_overflow) || (defined(__GNUC__) && (__GNUC__ >= 5))
474: static inline bool mi_mul_overflow(size_t count, size_t size, size_t* total) {
475:   #if (SIZE_MAX == UINT_MAX)
476:     return __builtin_umul_overflow(count, size, (unsigned int *)total);
477:   #elif (SIZE_MAX == ULONG_MAX)
478:     return __builtin_umull_overflow(count, size, (unsigned long *)total);
479:   #else
480:     return __builtin_umulll_overflow(count, size, (unsigned long long *)total);
481:   #endif
482: }
483: #else /* __builtin_umul_overflow is unavailable */
484: static inline bool mi_mul_overflow(size_t count, size_t size, size_t* total) {
485:   #define MI_MUL_COULD_OVERFLOW ((size_t)1 << (4*sizeof(size_t)))  // sqrt(SIZE_MAX)
486:   *total = count * size;
487:   // note: gcc/clang optimize this to directly check the overflow flag
488:   return ((size >= MI_MUL_COULD_OVERFLOW || count >= MI_MUL_COULD_OVERFLOW) && size > 0 && (SIZE_MAX / size) < count);
489: }
490: #endif
491:
492: // Safe multiply `count*size` into `total`; return `true` on overflow.
493: static inline bool mi_count_size_overflow(size_t count, size_t size, size_t* total) {
494:   if (count==1) {  // quick check for the case where count is one (common for C++ allocators)
495:     *total = size;
496:     return false;
497:   }
498:   else if mi_unlikely(mi_mul_overflow(count, size, total)) {
499:     #if MI_DEBUG > 0
500:     _mi_error_message(EOVERFLOW, "allocation request is too large (%zu * %zu bytes)\n", count, size);
501:     #endif
502:     *total = SIZE_MAX;
503:     return true;
504:   }
505:   else return false;
506: }
```

`src/alloc.c`:

```c
132:   #if (MI_PADDING || MI_GUARDED)
133:   if (size == 0) { size = sizeof(void*); }
134:   #endif
...
221: mi_decl_nodiscard extern inline mi_decl_restrict void* mi_heap_calloc(mi_heap_t* heap, size_t count, size_t size) mi_attr_noexcept {
222:   size_t total;
223:   if (mi_count_size_overflow(count,size,&total)) return NULL;
224:   return mi_heap_zalloc(heap,total);
225: }
...
268: mi_decl_nodiscard mi_decl_restrict void* mi_ucalloc(size_t count, size_t size, size_t* pblock_size) mi_attr_noexcept {
269:   size_t total;
270:   if (mi_count_size_overflow(count,size,&total)) return NULL;
271:   return mi_uzalloc(total, pblock_size);
```

## Port map: upstream -> `stdlib/alloc_policy.hp`

| Upstream | Port | Adaptations (all behaviour-preserving for valid inputs) |
|---|---|---|
| `mi_count_size_overflow` (internal.h:493-506) + `mi_mul_overflow` (474-482, 484-489) | `stdlib_count_size_overflow(count, size, &total)` | Mirrors return convention (1 = overflow/reject, 0 = product written), the `count == 1` quick path, and `*total = SIZE_MAX` on overflow. Uses `__builtin_mul_overflow` (the compiler builtin form of mimalloc's `__builtin_umul*`) on GCC/Clang. NULL `total` returns 1 instead of being dereferenced. The portable fallback checks `count > SIZE_MAX / size` **before** multiplying (mimalloc writes the wrapped product first), and short-circuits `size == 0` so no division by zero is possible. No `_mi_error_message`/error callback: rejection is reported by the return value only. |
| `_mi_align_up` fast branch (internal.h:405-410) | `stdlib_size_class_round_up(size, alignment, &out)` | Same `mask = alignment-1; (size+mask) & ~mask` computation. Returns 0/-1 instead of assuming valid input: rejects NULL out, zero/non-power-of-two alignment, and `size > SIZE_MAX - mask` (mimalloc relies on assert-only preconditions). |
| `_mi_wsize_from_size` (internal.h:463-466) | `stdlib_size_class_words(size, &words)` | Same `(size + sizeof(uintptr_t) - 1) / sizeof(uintptr_t)` ceiling division. Turns mimalloc's debug assert `size <= SIZE_MAX - sizeof(uintptr_t)` into a real -1 rejection using `SIZE_MAX - (w-1)` (tighter and correct for both `w=4` and `w=8`). Zero maps to zero words. |
| zero-size rule (alloc.c:133) | `stdlib_size_normalize_zero(size, zero_size, &out)` | Mimalloc substitutes `sizeof(void*)` under padding/guarded mode; this repo's `stdlib_calloc` (error.hp:412) substitutes `1u`. The fallback is therefore an explicit argument so callers can select either policy. Non-zero sizes pass through unchanged. |

No ABI change: the header contains only `static inline` scalar helpers with caller-provided out-parameters, two `<stddef.h>`/`<stdint.h>` includes, an include guard, and comments. No struct, enum, global, exported symbol, allocator state, or existing file was modified. Strict C+ patterns used: all out-param targets explicitly zero-initialized by callers (the strict checker does not model writes through pointers), no pointer arithmetic/subscripts, no divisions by a potentially zero operand, no wrapping operations on any accepted input, and no state mutation. `stdlib_calloc`'s existing independent guard in `stdlib/error.hp` is unchanged and still required for real allocations.

## Test

`tests/stdlib/upstream_reuse/alloc_policy.cp` prints `<tag> <0|1>` per check and
`ALLOC_POLICY <checks>` / `ALLOC_POLICY_FAILURES <failures>` at the end; exit 0
means zero failures. It covers:

- **SIZE_MAX / overflow boundaries**: `1 x SIZE_MAX` (quick path, no overflow), `SIZE_MAX x 2`, `2 x SIZE_MAX`, `(SIZE_MAX/2+1) x 2` (overflow) vs `SIZE_MAX/2 x 2 = SIZE_MAX-1` (exact boundary), `SIZE_MAX x SIZE_MAX`, `(SIZE_MAX/4+1) x 4` vs `SIZE_MAX/4 x 4 = SIZE_MAX-3`, plus zero-count/zero-element cases and NULL out-parameter.
- **Exact fit**: `1024 x 4 = 4096`, `3 x 7 = 21`, with the product carried through the full `count*size -> normalize -> word-round -> align-round` pipeline.
- **Zero-size normalization**: `0 -> sizeof(void*)` (mimalloc) and `0 -> 1u` (this repo), non-zero and SIZE_MAX pass-through, NULL out-parameter.
- **Size-class rounding**: 0/1/8/9/21 rounded to 8-byte classes, non-power-of-two and zero alignments rejected, SIZE_MAX wrap rejected, out-parameters untouched on every failure.
- **Word rounding**: 0/1/exact-word/word+1, and SIZE_MAX rejected with out untouched.
- **Composed boundary**: `SIZE_MAX/2 x 2` produces `SIZE_MAX-1`, and the subsequent word-rounding step rejects it without wrapping (verifies short-circuiting across helpers).

No memory is allocated and no system limit is exhausted.

## Verification (repository root, `build/c+`)

Strict / no sanitizer first:

```sh
timeout 30 build/c+ build tests/stdlib/upstream_reuse/alloc_policy.cp \
  --strict --sanitize=none -o build/alloc_policy_strict
timeout 30 build/alloc_policy_strict
```

Result: build rc 0 **with no compiler warnings**, run rc 0, stdout
`ALLOC_POLICY 42` / `ALLOC_POLICY_FAILURES 0` (all 42 per-check lines are `1`),
stderr empty.

Then UBSan, using the cross-repo runtime-header workaround documented in
`tests/stdlib/allocator/OVERFLOW_NOTES.md` (observation D2):

```sh
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  timeout 30 build/c+ build tests/stdlib/upstream_reuse/alloc_policy.cp \
  --strict --sanitize=undefined -o build/alloc_policy_ubsan
timeout 30 build/alloc_policy_ubsan
```

Result: build rc 0, run rc 0, stdout `ALLOC_POLICY 42` /
`ALLOC_POLICY_FAILURES 0`, stderr empty — **no UBSan diagnostic** (`runtime error: ...`) was emitted.

Note: `tests/stdlib/run_stdlib_tests.sh` only globs top-level `tests/stdlib/*.cp`,
so this subdirectory test is intentionally built/run with the explicit commands
above rather than by that script. No harness, CMake, or existing test file was
changed.

## Files returned

- `stdlib/alloc_policy.hp` (new, additive)
- `tests/stdlib/upstream_reuse/alloc_policy.cp` (new, self-checking)
- `tests/stdlib/upstream_reuse/alloc_policy_NOTES.md` (this file)

No other files were created or modified; no git add/commit was performed.
