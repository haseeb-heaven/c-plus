# Compiler Hand-Off (Agent 1): `.len` Property for Managed SafeCRT / Libc Allocators

## Problem Observed
When compiling user code with `build/c+` (`c-plus-core` / `c-plus-stdlib`):

```c
int *arr = (int *)malloc_s(sizeof(int) * 5);
for (int i = 0; i < arr.len; i++) { ... }
```

The compiler currently emits:
`error: '.len' requires a static array or a C+ managed allocation; pointer 'arr' has no tracked length`

## Root Cause
`compiler/Sema/CPlusRewriter.cpp` (`collectDynamicPointers()`) only recognizes the compiler-runtime allocators:
- `cplus_alloc`
- `cplus_zalloc`
- `cplus_realloc`
- `cplus_new`
- `cplus_new_array`

It does NOT yet recognize SafeCRT (`malloc_s`, `calloc_s`, `realloc_s`, `aligned_alloc_s`) or Hardened Libc (`cplus_malloc`, `cplus_calloc`, `cplus_realloc`) allocations, nor standard `malloc`/`calloc`/`realloc` when lowered in safe mode, and `cplus_safe_ptr_size` / `cplus_alloc_size` runtime side-table queries.

## Library-Side Metadata Already Implemented (Agent 2)
Both SafeCRT and Hardened Libc maintain thread-safe allocation metadata (`byte_size`, `elem_size`, `generation`) and already expose element-count helpers:
- SafeCRT (`stdlib/safe/safe_alloc.h`):
  - `size_t cplus_safe_ptr_size(const void *ptr);`
  - `size_t cplus_safe_ptr_elem_size(const void *ptr);`
  - `size_t cplus_safe_ptr_count(const void *ptr);`
  - `int cplus_safe_query_length(const void *ptr, size_t elem_size, size_t *out_count);`
  - `#define cplus_safe_len(ptr) ((ptr) != NULL ? (cplus_safe_ptr_size(ptr) / sizeof(*(ptr))) : (size_t)0)`
- Hardened Libc (`libc/install-include/cplus_libc.h`):
  - `size_t cplus_alloc_size(const void *ptr);`
  - `size_t cplus_alloc_elem_size(const void *ptr);`
  - `size_t cplus_alloc_length(const void *ptr);`
  - `int cplus_alloc_query_length(const void *ptr, size_t elem_size, size_t *out_count);`
  - `#define cplus_alloc_len(ptr) ((ptr) != NULL ? (cplus_alloc_size(ptr) / sizeof(*(ptr))) : (size_t)0)`

## Required Compiler Integration (Agent 1 / `c-plus-core`)
1. Extend `kAllocators` in `CPlusRewriter.cpp::collectDynamicPointers()` (and the corresponding `libc_contracts.json` / lifetime pass in `c-plus-core`) to track pointers assigned from:
   - SafeCRT: `malloc_s(bytes)`, `calloc_s(count, elem_size)`, `realloc_s(ptr, bytes)`, `aligned_alloc_s(align, bytes)`
   - Hardened Libc: `cplus_malloc(bytes)`, `cplus_calloc(count, elem_size)`, `cplus_realloc(ptr, bytes)`
   - Optionally in safe mode: `malloc(bytes)`, `calloc(count, elem_size)`, `realloc(ptr, bytes)`
2. Lower `ptr.len` to:
   - Static arrays (`T a[N]`): `(sizeof(a) / sizeof((a)[0]))` / constant `N`
   - `calloc_s(count, sz)` / `cplus_calloc(count, sz)`: `(((size_t)(count) * (size_t)(sz)) / sizeof(*ptr))` or `cplus_safe_ptr_size(ptr) / sizeof(*ptr)` when reassigned/reallocated dynamically
   - `malloc_s(bytes)` / `realloc_s(p, bytes)` / `aligned_alloc_s(a, bytes)`: `cplus_safe_len(ptr)` (runtime side-table) or `((bytes) / sizeof(*ptr))` when the allocation size expression is in scope and `ptr` has not been reassigned
   - `cplus_malloc` / `cplus_realloc`: `cplus_alloc_len(ptr)` or `((bytes) / sizeof(*ptr))`
   - General dynamic pointer passed across function boundaries: query `cplus_safe_ptr_size(ptr) / sizeof(*ptr)` -> `cplus_alloc_size(ptr) / sizeof(*ptr)` -> `cplus_alloc_length(ptr)`.
3. Also clear `nullVars_` on unary `&var` out-parameters in `c-plus-core` lifetime/safety checks and `AstSafetyCheck.cpp`.
