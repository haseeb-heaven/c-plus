# Phase 5 — allocation-size arithmetic audit (branch `agent/modern-stdlib`)

## Scope and method

Audited every allocation-size arithmetic path reachable from the checked C+
allocator and its containers, plus the checked SafeCRT allocators and the raw
runtime array allocator.  Tests are self-checking `.cp` programs that print
`<tag> <0|1>` per check and a final `<SUITE> <checks> <failures>` line; exit 0
means zero failures.  No system memory is exhausted: `SIZE_MAX` is passed only
as a *size value*, and the request ceiling is lowered in source.

**Bounded override.** Every test defines `#define STDLIB_ALLOC_LIMIT ((size_t)4096)`
*before* including `stdlib/error.hp`.  A source-level define is required
because the C+ strict checker does not consume `-D` flags (same convention as
`tests/stdlib/regressions/adv_integer_bounds.cp`); `-DSTDLIB_ALLOC_LIMIT=...`
would change the compiled limit but not the checker's view of it.  The default
limit (1<<40 on 64-bit) would make doubling/limit boundaries unreachable.
With 4096, a 1024-element `int` buffer is exactly at the ceiling and the next
doubling (2048) must be refused.

**Proof of "rejected before allocator call".**  Each rejection probe captures
`stdlib_alloc_seq()` and a telemetry snapshot around the call and asserts
`seq delta == 0`, `alloc_count delta == 0`, an increment in `api_failures`, the
documented category/code/message via `stdlib_set_error_callback`, and that a
pre-existing live block's size and bytes are unchanged.  SafeCRT probes use
the same pattern with `cplus_safe_alloc_seq()`, `errno`, and
`cplus_safe_ptr_size()`; the SafeCRT wrappers increment their sequence only
after validation, so a rejection must leave it untouched.

Verification order was **strict/no-sanitizer first, then UBSan**, every build
and run wrapped in `timeout 30`.

## Coverage map

| Category | Path (file:line) | Guard before arithmetic / allocation | Test |
|---|---|---|---|
| count × sizeof(element) | `error.hp:395-412` `stdlib_calloc` | `__builtin_mul_overflow(count, element_size, &total)` + `total > LIMIT`; zero count/elem → 1 byte | `overflow_count_size.cp` |
| array allocation | `runtime/cplus_runtime.c:211-225` `cplus_alloc_array` | `count > SIZE_MAX/element_size` → fatal *before* `cplus_zalloc(count*size)` | `overflow_count_size.cp` (forked child, exit 107) |
| capacity doubling | `vector.hp:137-147,190-203`; `collections.hp:705-715,948-958,1018-1028` | `space > (LIMIT/sizeof(int))/2` before `space*2` | `overflow_capacity.cp` |
| power-of-two rounding | `collections.hp:56-85` `stdlib_map_next_pow2`, `stdlib_map_rebuild_capacity` | `cap > SIZE_MAX>>2` before `cap <<= 1` / `capacity*2`; rebuild re-checks against LIMIT | `overflow_capacity.cp` |
| ring copy / rebuild sizing | `collections.hp:88-103,162-205,368-405,568-590` | `newspace`/`cap > LIMIT/sizeof(element)` before `sizeof(element)*n` | `overflow_capacity.cp` |
| string length + terminator | `string.hp:119-130,210-298,313-323` ctor/reserve/append/copy_from | `n < LIMIT`, `want > LIMIT`, `extra > LIMIT-1 || count > LIMIT-1-extra` before `count+extra+1`; doubling guarded by `space <= LIMIT/2` | `overflow_string.cp` |
| owned key length + 1 | `collections.hp:333-345` `strmap::stdlib_copy_key` | `len >= LIMIT` before `len + 1`; prime map so no rebuild allocation precedes the key copy | `overflow_string.cp` |
| substring/field `n+1` | `string.hp:326-345,366-385` | `n <= count <= LIMIT-1`, so `n+1` cannot wrap; `out_size-1` clamp | `overflow_string.cp` |
| reallocation sizes | `error.hp:422-449` `stdlib_realloc` | `actual = bytes==0?1:bytes`; `actual > LIMIT` → RANGE/BOUNDS before `cplus_realloc`/`stdlib_alloc`; metadata lookup refusal for untracked ptr | `overflow_realloc.cp` |
| growth reallocation | `vector.hp:107-132`; `string.hp:210-232` | new buffer size from guarded capacity; old released only after success; injection probe verifies atomic rollback | `overflow_realloc.cp` |
| header + payload / side table | `error.hp:262-355` `stdlib_meta_*`, `stdlib_alloc_size` | recorded payload is exactly the requested bytes; side-table growth uses `calloc(ncap, sizeof(slot))` with `ncap <= SIZE_MAX/4/sizeof(slot)` | `overflow_alignment.cp` |
| alignment rounding | `stdlib/safe/src/safe_alloc.c:217-239` `aligned_alloc_s` | power-of-two + `size > RSIZE_MAX_MEM` validation; rounding delegated to `posix_memalign` | `overflow_alignment.cp` (SafeCRT mode) |
| SafeCRT limit/overflow | `safe_alloc.c:155-255` `malloc_s/calloc_s/realloc_s` | `size > RSIZE_MAX_MEM`, `count > SIZE_MAX/size` before allocator; errno EINVAL/EOVERFLOW | `overflow_alignment.cp` (SafeCRT mode) |
| fixed-size node allocation | `collections.hp:830-858` `list` | `sizeof(list_node)` has no multiplication; alloc before link mutation | `overflow_alignment.cp` |
| image / http stride (static audit) | `image.hp:96-106,150-166`; `http.hp:99-121` | `cols > INT_MAX/channels`; `width > SIZE_MAX/channels || width*channels > SIZE_MAX/height`; `nmemb > SIZE_MAX/size` and `bytes > size-used-1` | code review; not runtime-tested (requires stb/curl link flags) |

**Result:** no overflow defect was found in any of the audited in-tree stdlib
allocation paths.  Every tested rejection returns the documented failure,
leaves state unchanged, and produces `seq/alloc_count` delta 0.  Two
out-of-scope observations are recorded below.

## Error / errno contract observed

| Trigger | Return | Category | `cplus_exit_code` | Message / errno |
|---|---|---|---|---|
| `stdlib_alloc(bytes > LIMIT)` | NULL | `STDLIB_CATEGORY_MEMORY` (4) | `CPLUS_EXIT_RESOURCE` (107) | `allocation request exceeds STDLIB_ALLOC_LIMIT` |
| `stdlib_calloc` product overflow or over-limit | NULL | MEMORY (4) | RESOURCE (107) | `allocation size overflow` |
| `stdlib_realloc(bytes > LIMIT)` (incl. NULL ptr) | NULL | `STDLIB_CATEGORY_RANGE` (2) | `CPLUS_EXIT_BOUNDS` (101) | `stdlib_realloc: request exceeds STDLIB_ALLOC_LIMIT` |
| `stdlib_realloc` untracked pointer | NULL | `STDLIB_CATEGORY_INVALID` (1) | `CPLUS_EXIT_UNSUPPORTED` (108) | `stdlib_realloc: pointer is not a live stdlib block` |
| `vector::reserve` / push_back / insert over-limit | -1 | RANGE (2) | BOUNDS (101) | `vector::reserve: capacity too large` / `vector::push_back: capacity overflow` (`insert` is silent) |
| `stdlib_ring_copy` over-limit | NULL | RANGE (2) | BOUNDS (101) | `ring buffer: capacity too large` |
| `string::reserve` / `append` over-limit | -1 | RANGE (2) | BOUNDS (101) | `string::reserve: capacity too large` / `string::append: result too large` |
| `malloc_s(size > RSIZE_MAX_MEM)` | NULL | n/a | n/a | `errno = EINVAL` |
| `calloc_s` product overflow | NULL | n/a | n/a | `errno = EOVERFLOW` |
| `realloc_s(size > RSIZE_MAX_MEM)` | NULL | n/a | n/a | `errno = EINVAL`, original block untouched |
| `aligned_alloc_s` bad alignment / huge size | NULL | n/a | n/a | `errno = EINVAL` |
| `cplus_alloc_array` product overflow | process exit 107 | n/a | `CPLUS_EXIT_RESOURCE` (107) | stderr: `C+ runtime error: allocation overflow: array size multiplication overflows size_t` |

## Exact verification commands and results

Working directory: repository root.  Compiler: `build/c+`.  All invocations
bounded with `timeout 30`.  Build command shape (no sanitizer first):

```sh
timeout 30 build/c+ build tests/stdlib/allocator/<name>.cp \
  --strict --sanitize=none -o build/<name>_strict
timeout 30 build/<name>_strict
```

UBSan command shape (note the environment workaround for observation D2):

```sh
CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include \
  timeout 30 build/c+ build tests/stdlib/allocator/<name>.cp \
  --strict --sanitize=undefined -o build/<name>_ubsan
timeout 30 build/<name>_ubsan
```

| Test | strict build/run | UBSan build/run | final line |
|---|---|---|---|
| `overflow_count_size.cp` | rc 0 / rc 0 | rc 0 / rc 0 (only expected child fatal lines on stderr) | `OVERFLOW_COUNT_SIZE 18` / `..._FAILURES 0` |
| `overflow_capacity.cp` | rc 0 / rc 0 | rc 0 / rc 0 | `OVERFLOW_CAPACITY 28` / `..._FAILURES 0` |
| `overflow_string.cp` | rc 0 / rc 0 | rc 0 / rc 0 | `OVERFLOW_STRING 19` / `..._FAILURES 0` |
| `overflow_realloc.cp` | rc 0 / rc 0 | rc 0 / rc 0 | `OVERFLOW_REALLOC 13` / `..._FAILURES 0` |
| `overflow_alignment.cp` (plain) | rc 0 / rc 0 | rc 0 / rc 0 | `OVERFLOW_ALIGNMENT 6` / `..._FAILURES 0` |
| `overflow_alignment.cp` (SafeCRT) | rc 0 / rc 0 | rc 0 / rc 0 | `OVERFLOW_ALIGNMENT 19` / `..._FAILURES 0` |
| `overflow_caller_wrap_repro.cp` (gap repro) | rc 0 / rc 0 | rc 0 / rc 0 | prints three `caller_*_accepted 1` lines |

SafeCRT mode build (both sanitizer settings):

```sh
timeout 30 build/c+ build tests/stdlib/allocator/overflow_alignment.cp \
  --strict --sanitize=none -DSTDLIB_OVERFLOW_SAFECRT -I. \
  -o build/overflow_alignment_safecrt_strict \
  -- build/safe/libcplus_safecrt.a -pthread
```

No UBSan diagnostic (`runtime error: ...` from the sanitizer with a
source location) appeared in any run.  The two stderr lines from
`overflow_count_size.cp` are the intentional fatal diagnostics of the forked
`cplus_alloc_array` child.

## Reproduced defects / observations (both outside this phase's file ownership)

### D1 — strict mode accepts caller-side multiplication/addition wrap before allocation

The checked allocator cannot detect an overflow that happens *in the caller*
before it is entered.  The `--strict` front end currently accepts such
expressions even when the operands are opaque (function-returned), so a
mathematically huge request becomes a small live allocation.

Minimal repro: `tests/stdlib/allocator/overflow_caller_wrap_repro.cp`

```c
static size_t opaque(size_t v) { return v; }
size_t n = opaque(SIZE_MAX - 8u);
void *p = stdlib_alloc(n + 16u);          /* wraps to 7 bytes  -> succeeds */
size_t m = opaque(SIZE_MAX / 4u + 1u);
void *q = stdlib_alloc(m * 4u);           /* 2^64 wraps to 0  -> 1-byte block */
```

Observed under both `--strict --sanitize=none` and `--strict --sanitize=undefined`:

```
caller_add_wrap_accepted 1
caller_mul_wrap_accepted 1
caller_const_wrap_accepted 1
```

Impact: a caller computing `count*sizeof(T)` or `len+1` itself bypasses
`stdlib_calloc`'s `__builtin_mul_overflow` guard.  In-tree stdlib code never
does this unguarded (every path in the coverage map checks first).  Fixing it
requires the compiler's strict arithmetic checker (see also
`scratch/bugs51.cp`, another agent's compile-time attack family), not
`stdlib/error.hp`; no header change was made here.

### D2 — UBSan builds are broken by a cross-repo runtime header mismatch

Plain UBSan build fails at the instrumented-runtime compile step:

```
$ timeout 30 build/c+ build tests/stdlib/allocator/overflow_count_size.cp \
    --strict --sanitize=undefined -o build/overflow_count_size_ubsan
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:129:23: error:
    use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

Cause: the sibling `c-plus-core` runtime source now references
`CPLUS_EXIT_ARITHMETIC`, but the driver compiles that source with
`-I<this-repo>/runtime/include`, whose `cplus/cplus.h` does not yet define the
enum.  Workaround used for all UBSan runs above:
`CPLUS_INCLUDE=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/include`.
Permanent fix (syncing the enum into this repo's public header or the driver's
include selection) is outside this phase's file ownership.

### O1 — inconsistent reporting for `vector::insert` doubling refusal

`vector::push_back` calls `stdlib_note(RANGE, CPLUS_EXIT_BOUNDS,
"vector::push_back: capacity overflow")`; `vector::insert` uses the same guard
but returns -1 silently.  Both are failure-atomic, so this is a telemetry
consistency issue, not a memory-safety defect.  The capacity test asserts the
current documented behavior (one callback for the two calls).

### O2 — `cplus_alloc_array` refuses overflow fatally, not recoverably

`cplus_alloc_array(SIZE_MAX, 4)` logs and calls `cplus_print_fatal`, which
`_Exit`s with `CPLUS_EXIT_RESOURCE` (107) instead of returning NULL.  This is
the documented raw-runtime contract; the recoverable path is
`stdlib_calloc`, which is covered by `overflow_count_size.cp`.

### O3 — `stdlib_map_next_pow2(1<<62)` returns `1<<62`, not 0

The doubling guard checks `cap > SIZE_MAX>>2` *before* shifting, so `cap`
can become `2^62`.  The resulting capacity is still refused by every
`rebuild()` (`cap > LIMIT/sizeof(element)`) before any allocation, so this is
safe; the capacity test records the actual value.

## Files returned

- `tests/stdlib/allocator/overflow_count_size.cp`
- `tests/stdlib/allocator/overflow_capacity.cp`
- `tests/stdlib/allocator/overflow_string.cp`
- `tests/stdlib/allocator/overflow_realloc.cp`
- `tests/stdlib/allocator/overflow_alignment.cp`
- `tests/stdlib/allocator/overflow_caller_wrap_repro.cp` (known-gap repro)
- `tests/stdlib/allocator/OVERFLOW_NOTES.md` (this file)

No source header was changed: no reproduced defect required a fix in
`stdlib/error.hp`, and D1/D2 require changes outside this phase's ownership.
