# EDGE100 adapters -- API manifest

47 public APIs, derived exhaustively from every `static inline` definition in the
in-scope headers. "Modern allocator helpers" is interpreted as the two NEW
MODERN-LAYER helper headers `stdlib/alloc_iface.hp` (unified allocator vtable
dispatch) and `stdlib/alloc_policy.hp` (overflow/rounding policy helpers); the
Arena/MemoryPool/DataStack container APIs are out of this agent's scope.

| # | API | Header | Edge dimensions exercised | Cases |
|---|-----|--------|---------------------------|-------|
| 1 | `gsl_adapt_refuse_null` | gsl_adapt.hp | err set, NULL err | 2 |
| 2 | `gsl_adapt_refuse_range` | gsl_adapt.hp | err set, NULL err | 2 |
| 3 | `gsl_adapt_refuse_bounds` | gsl_adapt.hp | err set, NULL err | 2 |
| 4 | `gsl_narrow_i64_i8` | gsl_adapt.hp | 0, min, max, min-1, max+1, NULL out, NULL err | 7 |
| 5 | `gsl_narrow_i64_u8` | gsl_adapt.hp | 0, min, max, -1, max+1, NULL out, NULL err | 7 |
| 6 | `gsl_narrow_i64_i16` | gsl_adapt.hp | 0, min, max, min-1, max+1, NULL out, NULL err | 7 |
| 7 | `gsl_narrow_i64_u16` | gsl_adapt.hp | 0, min, max, -1, max+1, NULL out, NULL err | 7 |
| 8 | `gsl_narrow_i64_i32` | gsl_adapt.hp | 0, min, max, min-1, max+1, NULL out, NULL err | 7 |
| 9 | `gsl_narrow_i64_u32` | gsl_adapt.hp | 0, min, max, -1, max+1, NULL out, NULL err | 7 |
| 10 | `gsl_narrow_i64_i64` | gsl_adapt.hp | 0, LLONG_MIN, LLONG_MAX, NULL out, NULL err | 5 |
| 11 | `gsl_narrow_i64_u64` | gsl_adapt.hp | 0, negative refusal, LLONG_MAX, NULL out, NULL err | 6 |
| 12 | `gsl_narrow_u64_i64` | gsl_adapt.hp | 0, LLONG_MAX, LLONG_MAX+1, NULL out, NULL err | 5 |
| 13 | `gsl_narrow_u64_u8` | gsl_adapt.hp | 0, UCHAR_MAX, UCHAR_MAX+1, NULL out, NULL err | 5 |
| 14 | `gsl_narrow_u64_u32` | gsl_adapt.hp | 0, UINT_MAX, UINT_MAX+1, NULL out, NULL err | 5 |
| 15 | `gsl_not_null_i32_make` | gsl_adapt.hp | valid, NULL p, NULL out, both NULL | 4 |
| 16 | `gsl_not_null_i32_get` | gsl_adapt.hp | valid, NULL nn, NULL out, broken invariant | 4 |
| 17 | `gsl_not_null_i32_swap` | gsl_adapt.hp | valid swap, NULL a, NULL b, both NULL | 4 |
| 18 | `gsl_not_null_void_make` | gsl_adapt.hp | valid, NULL p, NULL out | 3 |
| 19 | `gsl_not_null_void_get` | gsl_adapt.hp | valid, NULL nn, NULL out, broken invariant | 4 |
| 20 | `gsl_span_i32_view` | gsl_adapt.hp | valid interior view, NULL out, NULL owner, off overflow, count overflow, range past owner | 6 |
| 21 | `gsl_span_i32_make` | gsl_adapt.hp | valid, NULL+nonzero, NULL+zero empty, NULL out, stack origin, interior origin | 6 |
| 22 | `gsl_span_i32_size` | gsl_adapt.hp | sized span, NULL span, empty span | 3 |
| 23 | `gsl_span_i32_size_bytes` | gsl_adapt.hp | sized span, NULL span | 2 |
| 24 | `gsl_span_i32_empty` | gsl_adapt.hp | non-empty, empty, NULL | 3 |
| 25 | `gsl_span_i32_data` | gsl_adapt.hp | valid, NULL | 2 |
| 26 | `gsl_span_i32_at` | gsl_adapt.hp | valid read, idx==count, SIZE_MAX idx, NULL span, NULL out, stale owner, recycled address | 7 |
| 27 | `gsl_span_i32_first` | gsl_adapt.hp | valid, count>size, count==size, count 0, NULL span | 5 |
| 28 | `gsl_span_i32_last` | gsl_adapt.hp | valid, count>size, count==size, count 0, NULL out | 5 |
| 29 | `gsl_span_i32_subspan` | gsl_adapt.hp | valid, offset>size, count>size-offset, count_is_all, offset==size, NULL span | 6 |
| 30 | `lifetime_check` | lifetime_adapt.hp | valid, NULL ptr, NULL token, untracked origin, stale, recycled address | 6 |
| 31 | `lifetime_owner_init` | lifetime_adapt.hp | valid, NULL owner, already held, zero bytes, SIZE_MAX refusal | 5 |
| 32 | `lifetime_owner_release` | lifetime_adapt.hp | valid, NULL owner, double release, moved-from owner, stale token | 5 |
| 33 | `lifetime_transfer` | lifetime_adapt.hp | valid, NULL dst/src, dst==src, occupied dst, stale source, recycled address | 7 |
| 34 | `lifetime_borrow_from` | lifetime_adapt.hp | valid, NULL b/o, released owner, moved-from owner | 5 |
| 35 | `lifetime_borrow_is_live` | lifetime_adapt.hp | live, NULL, zeroed, stale, recycled address | 5 |
| 36 | `lifetime_owner_is_live` | lifetime_adapt.hp | live, NULL, zeroed, released, recycled address | 5 |
| 37 | `lifetime_read` | lifetime_adapt.hp | valid, NULL dst, NULL borrow, off>cap, n>cap-off, empty at cap, stale, recycled address | 8 |
| 38 | `lifetime_write` | lifetime_adapt.hp | valid, NULL src, NULL borrow, off>cap, n>cap-off, empty at cap, stale, recycled address | 8 |
| 39 | `stdlib_allocate` | alloc_iface.hp | valid custom dispatch, size 0, align 0 default, bad align, NULL allocator, NULL err | 6 |
| 40 | `stdlib_reallocate` | alloc_iface.hp | valid grow, size 0, bad align, NULL allocator, NULL err | 5 |
| 41 | `stdlib_deallocate` | alloc_iface.hp | custom release, NULL ptr no-op, NULL allocator, heap vtable | 4 |
| 42 | `stdlib_allocator_reset` | alloc_iface.hp | custom reset, NULL allocator, NULL reset fn, heap vtable | 4 |
| 43 | `stdlib_heap_allocator` | alloc_iface.hp | all-NULL vtable fields, dispatch through it | 2 |
| 44 | `stdlib_count_size_overflow` | alloc_policy.hp | normal product, NULL total, overflow, count==1, size==0, SIZE_MAX product | 6 |
| 45 | `stdlib_size_class_round_up` | alloc_policy.hp | ordinary round-up, NULL out, align 0, non-power-of-two, overflow, zero size | 6 |
| 46 | `stdlib_size_class_words` | alloc_policy.hp | zero, one word, two words, NULL out, SIZE_MAX refusal (out untouched) | 5 |
| 47 | `stdlib_size_normalize_zero` | alloc_policy.hp | zero->zero_size, non-zero unchanged, NULL out | 3 |

**Total: 47 APIs, 233 cases.**
