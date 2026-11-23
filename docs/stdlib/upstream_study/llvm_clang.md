# LLVM / Clang upstream study — sanitizer interfaces, libc++ checked access, and hardening modes

Branch under study: `agent/modern-stdlib`.
Scope: **study only**. The only file created or modified for this task is this
document. No headers, build files, tests, or git index entries were touched. No
upstream test was **executed**; the upstream tests listed in §7 were fetched and
read only.

Evidence labels used below:

- **[OBSERVED]** — text retrieved from the exact URLs in §1, or from a
  committed repository file quoted with a path and line number.
- **[INFERENCE]** — my design reading; not a quoted upstream fact.
- **[NOT FOUND]** — the task brief named an artifact that does not exist at the
  pinned revision; reported rather than invented.

Revision policy: every raw URL below uses the **same pinned commit**
`424c2d9b7e4de40d0804dd374721e6411c27d1d1` (= tag `llvmorg-20.1.1`). This is not
an arbitrary pin: `docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md:17-33` resolves that
tag to this exact commit (tag object `46174b7e09f5a975e582d35f6674cd31cae414f9`)
and states the URL form `https://raw.githubusercontent.com/llvm/llvm-project/424c2d9b7e4de40d0804dd374721e6411c27d1d1/libc/<path>`.
Reusing it keeps this study consistent with the repository's existing LLVM
provenance.

---

## 1. Retrieval log (exact URLs, status, revision)

Base prefix for every `raw.githubusercontent.com` URL in the table:

```
https://raw.githubusercontent.com/llvm/llvm-project/424c2d9b7e4de40d0804dd374721e6411c27d1d1/
```

All responses below were retrieved with `curl -sSL -w '%{http_code}'` into a
session temp directory outside the repository
(`/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/llvm_study/`);
byte counts are the actual response bodies.

| # | Path appended to base prefix | Result | Bytes | Notes |
|---|------------------------------|--------|-------|-------|
| 1 | `LICENSE.TXT` | HTTP 200 | 15 141 | Full Apache-2.0-with-exceptions text; see §2. |
| 2 | `compiler-rt/include/sanitizer/asan_interface.h` | HTTP 200 | 13 654 | Public ASan interface, §3.1. |
| 3 | `compiler-rt/include/sanitizer/ubsan_interface.h` | HTTP 200 | 1 090 | Also fetched via `webfetch` (text) — **SUCCESS**, complete file; §3.2. |
| 4 | `compiler-rt/include/sanitizer/common_interface_defs.h` | HTTP 200 | 22 789 | Container annotation APIs, §3.3, §6. |
| 5 | `compiler-rt/include/sanitizer/lsan_interface.h` | HTTP 200 | 3 993 | Fetched for completeness; §3.4. |
| 6 | `compiler-rt/include/sanitizer/hwasan_interface.h` | HTTP 200 | 5 043 | Fetched for completeness; §3.4. |
| 7 | `compiler-rt/include/sanitizer/tsan_interface.h` | HTTP 200 | 14 952 | Fetched for completeness; §3.4. |
| 8 | `compiler-rt/include/sanitizer/coverage_interface.h` | HTTP 200 | 1 133 | Fetched for completeness; §3.4. |
| 9 | `libcxx/include/vector` | HTTP 200 | 14 486 | Umbrella header; delegates implementation to `__vector/*`. |
| 10 | `libcxx/include/string` | HTTP 200 | 203 938 | `basic_string` implementation is inline in this file at this pin. |
| 11 | `libcxx/include/__vector/vector.h` | HTTP 200 | 58 171 | Real `std::vector` implementation, §4.1, §5.1. |
| 12 | `libcxx/include/__vector/vector_bool.h` | HTTP 200 | 45 303 | Fetched; not used for detailed claims. |
| 13 | `libcxx/include/__config` | HTTP 200 | 54 003 | Checked; does **not** define `_LIBCPP_ASSERT` at this pin (it is in `<__assert>`). |
| 14 | `libcxx/include/__memory/allocator_traits.h` | HTTP 200 | 17 274 | Fetched for allocator-bound context; not quoted. |
| 15 | `libcxx/include/__assert` | HTTP 200 | 7 124 | Assertion macros + hardening tiers, §4.2. |
| 16 | `libcxx/include/__debug_utils/sanitizers.h` | HTTP 200 | 4 009 | ASan container annotations, §6.1. |
| 17 | `libcxx/include/__verbose_abort` | HTTP 200 | 2 401 | Abort customization point, §4.3, §6.2. |
| 18 | `libcxx/src/verbose_abort.cpp` | HTTP 200 | 1 932 | Fetched; not deeply inspected. |
| 19 | `clang/include/clang/Basic/AttrDocs.td` | HTTP 200 | 354 591 | Attribute documentation, §6.4. |
| 20 | `libc/src/string/memset_explicit.cpp` | HTTP 200 | 1 022 | Provenance verification, §11. |
| 21 | `libcxx/test/libcxx/assertions/modes/fast.pass.cpp` | HTTP 200 | 983 | §7. |
| 22 | `libcxx/test/libcxx/assertions/modes/none.pass.cpp` | HTTP 200 | 1 041 | §7 (fetched; not quoted). |
| 23 | `libcxx/test/libcxx/assertions/default_verbose_abort.pass.cpp` | HTTP 200 | 864 | §7. |
| 24 | `libcxx/test/libcxx/assertions/customize_verbose_abort.compile-time.pass.cpp` | HTTP 200 | 960 | §7. |
| 25 | `libcxx/test/libcxx/assertions/single_expression.pass.cpp` | HTTP 200 | 931 | §7. |
| 26 | `libcxx/test/support/check_assertion.h` | HTTP 200 | 13 667 | §7, §6.3. |
| 27 | `libcxx/test/libcxx/containers/sequences/vector/assert.index.oob.pass.cpp` | HTTP 200 | 1 065 | §7. |
| 28 | `libcxx/test/libcxx/containers/sequences/vector/assert.pop_back.empty.pass.cpp` | HTTP 200 | 859 | §7 (fetched; not quoted). |
| 29 | `libcxx/test/libcxx/containers/sequences/vector/asan.pass.cpp` | HTTP 200 | 2 271 | §7. |
| 30 | `libcxx/test/libcxx/containers/sequences/vector/asan_throw.pass.cpp` | HTTP 200 | 4 762 | §7 (fetched; not deeply inspected). |
| 31 | `libcxx/test/libcxx/containers/sequences/vector/asan_turning_off.pass.cpp` | HTTP 200 | 2 223 | §7. |
| 32 | `https://api.github.com/repos/llvm/llvm-project/git/trees/424c2d9b7e4de40d0804dd374721e6411c27d1d1?recursive=1` | HTTP 200 | n/a | `truncated: true`, 63 908 entries; used only to discover test paths. |

### 1.1 Fetch failures and corrections (recorded honestly)

The following were **guessed paths that returned HTTP 404** (14-byte body
`404: Not Found`). They are listed so a later agent does not repeat them:

| Guessed URL suffix | Result |
|---|---|
| `libcxx/include/__assert/assert.h` | HTTP 404 |
| `libcxx/include/__assert.h` | HTTP 404 |
| `libcxx/include/__string/string.h` | HTTP 404 |
| `libcxx/include/__debug_utils/constexpr_debug.h` | HTTP 404 |
| `libcxx/include/__assertion_handler.in` | HTTP 404 |
| `libcxx/include/__assertion_handler` | HTTP 404 |
| `libcxx/include/__assertion_handler.h` | HTTP 404 |
| `libcxx/utils/generate_assertion_handler.py` | HTTP 404 |

**Corrections [OBSERVED]:**

- The implementation of `<vector>` is `libcxx/include/__vector/vector.h`; the
  umbrella `libcxx/include/vector` only contains the synopsis and includes.
  `basic_string` at this pin lives directly in `libcxx/include/string`, not in a
  `__string/string.h`.
- Assertion macros live in `libcxx/include/__assert` (no file extension). Its
  first lines state: `#include <__assertion_handler> // Note: this include is
  generated by CMake and is potentially vendor-provided.` — the handler header
  is a **generated/vendored artifact**, which is why no raw path exists in the
  tree. **[INFERENCE]** this is itself a deliberate Agent-1-style seam.
- GitHub code search
  (`https://api.github.com/search/code?q=LIBCPP_ASSERTION_HANDLER+repo:llvm/llvm-project`)
  returned **HTTP 401 `Requires authentication`**; the recursive tree API was
  used instead. That tree response was `truncated: true`, so the test-path list
  in §7 is not claimed to be exhaustive.

### 1.2 Task-brief path corrections (local files)

The brief cited `docs/stdlib/agent2_audit/module_audits/` and
`docs/stdlib/agent2/SANITIZER_MATRIX.md`. Those exact directories do **not**
exist on this branch. The actual committed files are:

- `tests/stdlib/agent2_audit/module_audits/vector.hp.md`
- `tests/stdlib/agent2_audit/module_audits/string.hp.md`
- `tests/stdlib/agent2/SANITIZER_MATRIX.md`
- `docs/libc/AGENT2_CURRENT_AUDIT.md`, `docs/libc/ASAN_AUDIT.md`,
  `docs/libc/AGENT1_COMPILER_GAPS.md` (companion evidence; `docs/libc/` rather
  than `docs/stdlib/`).

All gap citations in §8 and §9 use these actual paths. No citation below points
at a file that was not read.

---

## 2. License (exact identifier)

`LICENSE.TXT` at the pinned commit begins verbatim:

> ==============================================================================
> The LLVM Project is under the Apache License v2.0 with LLVM Exceptions:
> ==============================================================================
>
> Apache License
> Version 2.0, January 2004
> http://www.apache.org/licenses/

Every fetched LLVM source header (sanitizer interfaces, `libcxx/include/vector`,
`libcxx/include/string`, `libcxx/include/__assert`,
`libcxx/include/__debug_utils/sanitizers.h`, `libcxx/include/__verbose_abort`,
`clang/include/clang/Basic/AttrDocs.td`, and
`libc/src/string/memset_explicit.cpp`) carries the SPDX line:

> `// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`

So the governing identifier is **`Apache-2.0 WITH LLVM-exception`**, identical
to the identifier already recorded for LLVM libc in
`docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md:19-22` and
`docs/libc/C23_SMALL_UPSTREAM.md:58-62`. Details on what this permits and what
this project should *not* port are in §10.

---

## 3. Sanitizer interface functions available to runtimes

### 3.1 ASan — `compiler-rt/include/sanitizer/asan_interface.h`

**[OBSERVED]** complete list of `__asan_*` entry points declared in the public
header (line numbers from the fetched file):

| Line | Declaration | Grouping |
|---|---|---|
| 34 | `void __asan_poison_memory_region(void const volatile *addr, uptr size)` | manual shadow control |
| 49 | `void __asan_unpoison_memory_region(void const volatile *addr, uptr size)` | manual shadow control |
| 97 | `int __asan_address_is_poisoned(void const volatile *addr)` | query |
| 107 | `void *__asan_region_is_poisoned(void *beg, size_t size)` | query |
| 114 | `void __asan_describe_address(void *addr)` | diagnostics |
| 123 | `int __asan_report_present(void)` | report introspection |
| 132/141/150/159 | `__asan_get_report_pc/bp/sp/address(void)` | report introspection |
| 167/175/183 | `__asan_get_report_access_type/access_size/description()` | report introspection |
| 204 | `const char *__asan_locate_address(void *addr, char *name, size_t name_size, void **region_address, size_t *region_size)` | symbolication |
| 221/236 | `__asan_get_alloc_stack / __asan_get_free_stack(void *addr, void **trace, size_t size, int *thread_id)` | allocation/free stacks |
| 244 | `void __asan_get_shadow_mapping(size_t *shadow_scale, size_t *shadow_offset)` | shadow layout |
| 257 | `void __asan_report_error(void *pc, void *bp, void *sp, void *addr, int is_write, size_t access_size)` | forced report |
| 262 | `void __asan_set_death_callback(void (*callback)(void))` | lifecycle hook |
| 277 | `void __asan_on_error(void)` | user override hook |
| 281 | `void __asan_print_accumulated_stats(void)` | telemetry |
| 290 | `const char *__asan_default_options(void)` | user override hook |
| 302/322 | `__asan_get_current_fake_stack / __asan_addr_is_in_fake_stack` | use-after-return support |
| 330 | `void __asan_handle_no_return(void)` | longjmp/unwind support |
| 334 | `int __asan_update_allocation_context(void *addr)` | allocation context |

Also declared (found by symbol scan of the same file):
`__asan_set_error_report_callback`. The header's SPDX line and the comment
`This file is a part of AddressSanitizer (ASan). Public interface header.` are
at lines 1-11.

**[OBSERVED — negative result]** `asan_interface.h` does **not** declare
`__sanitizer_annotate_contiguous_container`; that API is in
`common_interface_defs.h` (§3.3) and is redeclared locally by libc++ (§6.1).

### 3.2 UBSan — `compiler-rt/include/sanitizer/ubsan_interface.h`

**[OBSERVED]** the entire public UBSan interface header is 1 090 bytes and
declares exactly **one** function:

```c
const char *SANITIZER_CDECL __ubsan_default_options(void);
```

with the documented purpose: *"You can provide your own implementation of this
function to return a string containing UBSan runtime options (for example,
`verbosity=1:halt_on_error=0`)."*

This is a key architectural fact for Agent 1 / Agent 2 coordination: **UBSan's
checks are compiler-instrumented, not library calls exposed to a standard
library.** There is no `__ubsan_report_*` or `__ubsan_poison_*` in the public
header for a stdlib to call. [INFERENCE] a C+ stdlib cannot "opt into" UBSan
checks by linking a header; it can only (a) avoid undefined behaviour so the
instrumented build stays quiet, and (b) provide its own tagged runtime checks
that work with `--sanitize=none`.

### 3.3 Common interface — `compiler-rt/include/sanitizer/common_interface_defs.h`

Functions relevant to a runtime/library author:

| Line | API | Relevance |
|---|---|---|
| 45/48/51 | `__sanitizer_set_report_path`, `__sanitizer_set_report_fd`, `__sanitizer_get_report_path` | redirect diagnostics |
| 63 | `__sanitizer_report_error_summary(const char *error_summary)` | emit a summary line |
| 111 | `int __sanitizer_acquire_crash_state()` | "Returns 1 on the first call, then returns 0 thereafter. Called by the tool to ensure only one report is printed when multiple errors occur simultaneously." |
| 147-161 | `void __sanitizer_annotate_contiguous_container(const void *beg, const void *end, const void *old_mid, const void *new_mid)` | vector/string capacity-vs-size shadow, §6.1 |
| 191 | `void __sanitizer_annotate_double_ended_contiguous_container(...6 pointers...)` | deque chunks |
| 229 | `void __sanitizer_copy_contiguous_container_annotations(src_begin, src_end, dst_begin, dst_end)` | move annotations for trivially relocatable objects |
| 249/274/288/302 | `__sanitizer_verify_contiguous_container`, `...double_ended...`, `__sanitizer_contiguous_container_find_bad_address`, `...double_ended...` | verification/test hooks |
| 308 | `void __sanitizer_print_stack_trace(void)` | diagnostics |
| 320/324 | `__sanitizer_symbolize_pc`, `__sanitizer_symbolize_global` | diagnostics |
| 343 | `void __sanitizer_set_death_callback(void (*callback)(void))` | lifecycle hook |
| 359-424 | `__sanitizer_weak_hook_memcmp/strncmp/strncasecmp/strcmp/strcasecmp/strstr/strcasestr/memmem` | weak interception hooks |
| 434 | `__sanitizer_print_memory_profile(size_t top_percent, size_t max_number_of_contexts)` | telemetry |
| 464/479 | `__sanitizer_start_switch_fiber`, `__sanitizer_finish_switch_fiber` | fake-stack / fiber support |
| 485 | `__sanitizer_get_module_and_offset_for_pc` | symbolication |

**[OBSERVED]** the annotation contract, quoted in part (lines 113-128):

> *"Annotates the current state of a contiguous container, such as
> `<c>std::vector</c>`, `<c>std::string</c>`, or similar. ... The container owns
> the region of memory `[beg, end)`; the memory `[beg, mid)` is used to store
> the current elements, and the memory `[mid, end)` is reserved for future
> elements (`beg <= mid <= end`). ... This annotation tells the Sanitizer tool
> about the current state of the container so that the tool can report errors
> when memory from `[mid, end)` is accessed. Insert this annotation into methods
> like `push_back()` or `pop_back()`. Supply the old and new values of `mid`.
> In the initial state `mid == end`, so that should be the final state when the
> container is destroyed or when the container reallocates the storage."*

Two explicit cautions in the same block (lines 134-140):

> *"Use this function with caution and do not use for anything other than
> vector-like classes."*
> *"Unaligned `beg` or `end` may miss bugs in these granules."*

### 3.4 Other sanitizer interfaces (fetched for completeness)

- `lsan_interface.h`: the complete `__lsan_*` symbol set observed in the fetched
  file is `__lsan_disable`, `__lsan_enable`, `__lsan_ignore_object`,
  `__lsan_register_root_region`, `__lsan_unregister_root_region`,
  `__lsan_do_leak_check`, `__lsan_do_recoverable_leak_check`,
  `__lsan_is_turned_off`, `__lsan_default_options`,
  `__lsan_default_suppressions`.
- `tsan_interface.h`: observed names include `__tsan_acquire`,
  `__tsan_release`, `__tsan_default_options`, `__tsan_default_suppressions`,
  `__tsan_on_initialize`, `__tsan_external_read`, `__tsan_external_write`,
  `__tsan_external_register_tag/header`, `__tsan_create_fiber`,
  `__tsan_destroy_fiber`, `__tsan_get_current_fiber`,
  `__tsan_get_current_report`, the `__tsan_mutex_*` family, and
  `__tsan_check_no_mutexes_held`. **[OBSERVED — negative result]** the strings
  `__tsan_read*`, `__tsan_write*`, `__tsan_func_entry/exit`, and `__tsan_init`
  do **not** appear as identifiers in this header at the pinned revision; they
  are not claimed here.
- `hwasan_interface.h`: observed names include `__hwasan_default_options`,
  `__hwasan_disable_allocator_tagging`, `__hwasan_enable_allocator_tagging`,
  `__hwasan_tag_memory`, `__hwasan_tag_pointer`,
  `__hwasan_get_tag_from_pointer`, `__hwasan_test_shadow`,
  `__hwasan_print_shadow`, `__hwasan_print_memory_usage`,
  `__hwasan_set_error_report_callback`, `__hwasan_thread_enter/exit`,
  `__hwasan_handle_longjmp`, `__hwasan_handle_vfork`, `__hwasan_init_static`.
  **[OBSERVED — negative result]** `__hwasan_poison_stack_memory` and
  `__hwasan_unpoison_stack_memory` are **not** in this header at this pin.
- `coverage_interface.h` (1 133 bytes): the complete set is
  `__sanitizer_cov_dump`, `__sanitizer_cov_reset`, and
  `__sanitizer_dump_coverage(const uintptr_t *pcs, uintptr_t len)`.
  **[OBSERVED — negative result]** `__sanitizer_cov_trace_pc_guard`,
  `__sanitizer_cov_trace_pc_guard_init`, `__sanitizer_cov_trace_pc_indir` and
  `__sanitizer_cov_8bit_counters_init` are **not** declared in this header.

**[INFERENCE]** none of these is a primitive a C++ standard library should call
directly in normal builds; they are runtime/plugin integrations. The one
exception where libc++ *does* call one is the container annotation of §6.1, and
`docs/libc/ASAN_AUDIT.md:§2` gives the repository's reason for not copying that
behavior (§9.2 below).

---

## 4. Checked access patterns in libc++ `vector` / `string`

### 4.1 `std::vector` — `libcxx/include/__vector/vector.h`

**[OBSERVED]** element access splits into two deliberately different policies:

```cpp
// lines 402-408
reference operator[](size_type __n) _NOEXCEPT {
  _LIBCPP_ASSERT_VALID_ELEMENT_ACCESS(__n < size(), "vector[] index out of bounds");
  return this->__begin_[__n];
}
// lines 410-413
reference at(size_type __n) {
  if (__n >= size())
    this->__throw_out_of_range();
  return this->__begin_[__n];
}
```

`operator[]` is a *debug/hardening assertion*; `at()` is an unconditional,
standard-mandated exception (`__throw_out_of_range()` at line 765 calls
`std::__throw_out_of_range("vector")`).

Other categorized assertion sites in the same file:

| Line | Check | Category |
|---|---|---|
| 422/426 | `!empty()` in `front()` | `_LIBCPP_ASSERT_VALID_ELEMENT_ACCESS` |
| 430/434 | `!empty()` in `back()` | `_LIBCPP_ASSERT_VALID_ELEMENT_ACCESS` |
| 474 | `!empty()` in `pop_back()` | `_LIBCPP_ASSERT_VALID_ELEMENT_ACCESS` |
| 1147 | `__position != end()` in `erase(iterator)` | `_LIBCPP_ASSERT_VALID_ELEMENT_ACCESS` |
| 1158 | `__first <= __last` in `erase(first,last)` | `_LIBCPP_ASSERT_VALID_INPUT_RANGE` |
| 1377 | allocator compatibility | `_LIBCPP_ASSERT_COMPATIBLE_ALLOCATOR` |

### 4.2 `_LIBCPP_ASSERT`, `_LIBCPP_ASSUME`, and the hardening tiers — `libcxx/include/__assert`

**[OBSERVED]** the base macro (lines 18-23) is a single expression and routes
through a **customizable handler**:

```cpp
#define _LIBCPP_ASSERT(expression, message)                                    \
  (__builtin_expect(static_cast<bool>(expression), 1)                            \
       ? (void)0                                                                 \
       : _LIBCPP_ASSERTION_HANDLER(__FILE__ ":" _LIBCPP_TOSTRING(__LINE__)       \
             ": assertion " _LIBCPP_TOSTRING(expression) " failed: " message "\n"))
```

Line 13 of the same file: `#include <__assertion_handler> // Note: this include
is generated by CMake and is potentially vendor-provided.`

`_LIBCPP_ASSUME` (lines 29-36) wraps `__builtin_assume` with a warning about
optimization inhibition, and degrades to `((void)0)` when the builtin is absent.

The bulk of the header is a **category × hardening-mode matrix**. Ten named
categories exist:

`VALID_INPUT_RANGE`, `VALID_ELEMENT_ACCESS`, `NON_NULL`,
`NON_OVERLAPPING_RANGES`, `VALID_DEALLOCATION`, `VALID_EXTERNAL_API_CALL`,
`COMPATIBLE_ALLOCATOR`, `ARGUMENT_WITHIN_DOMAIN`, `PEDANTIC`,
`SEMANTIC_REQUIREMENT`, `INTERNAL`, `UNCATEGORIZED` (12 counting the last two).

Tier behavior **[OBSERVED]**:

| `_LIBCPP_HARDENING_MODE` | Enabled categories |
|---|---|
| `FAST` | `VALID_INPUT_RANGE`, `VALID_ELEMENT_ACCESS` only |
| `EXTENSIVE` | all except `SEMANTIC_REQUIREMENT` and `INTERNAL` |
| `DEBUG` | all categories |
| none (else branch) | all macros expand to `((void)0)` |

In-file rationale (lines 38-40): *"Fast hardening mode checks."*; for
`NON_NULL` the comment says *"On most modern platforms, dereferencing a null
pointer does not lead to a real memory access"*; for `NON_OVERLAPPING_RANGES`,
*"Overlapping ranges will make algorithms produce incorrect results but don't
directly lead to a security vulnerability."*

**[NOT FOUND — important correction]** The task brief asked about
`_LIBCPP_VERBOSE_ASSERT`. A scan of **all** fetched libc++ files at this pin
(`vector` umbrella, `string`, `__vector/vector.h`, `__config`, `__assert`,
`__debug_utils/sanitizers.h`, `__verbose_abort`) found **zero** occurrences of
`_LIBCPP_VERBOSE_ASSERT`. The 20.1.1 spelling is instead:

- `_LIBCPP_ASSERT(expr, message)` plus the categorized `_LIBCPP_ASSERT_*` family
  above (the "verbose" part is that the message includes file, line, expression,
  and text); and
- `_LIBCPP_VERBOSE_ABORT(format, ...)` (§4.3), which handles termination.

I did not invent a definition for a macro that is not in the pinned source.

**[OBSERVED]** usage counts in `libcxx/include/string` (a rough measure of which
categories matter in practice): `VALID_ELEMENT_ACCESS` 8, `NON_NULL` 28,
`VALID_INPUT_RANGE` 1, `COMPATIBLE_ALLOCATOR` 1, `INTERNAL` 12; all other
categories 0 in that file.

### 4.3 `_LIBCPP_VERBOSE_ABORT` — `libcxx/include/__verbose_abort`

**[OBSERVED]** lines 23-53 declare

```cpp
[[__noreturn__]] ... void __libcpp_verbose_abort(const char* __format, ...) _LIBCPP_VERBOSE_ABORT_NOEXCEPT;
```

with the comment *"This function should never be called directly from the code
-- it should only be called through the `_LIBCPP_VERBOSE_ABORT` macro."* The
macro is user-overridable:

```cpp
#if !defined(_LIBCPP_VERBOSE_ABORT)
#  if !_LIBCPP_AVAILABILITY_HAS_VERBOSE_ABORT
void __use(const char*, ...);
#    define _LIBCPP_VERBOSE_ABORT(...) (decltype(::std::__use(__VA_ARGS__))(), __builtin_abort())
#  else
#    define _LIBCPP_VERBOSE_ABORT(...) ::std::__libcpp_verbose_abort(__VA_ARGS__)
#  endif
#endif
```

This is the single most reusable upstream pattern for Agent-1/Agent-2
coordination: **one overridable termination seam** behind all assertion
messages.

---

## 5. Allocation failure and size overflow checks

### 5.1 `std::vector`

**[OBSERVED]** `max_size()` (`__vector/vector.h:393-395`) bounds by *both* the
allocator and `difference_type`:

```cpp
size_type max_size() const _NOEXCEPT {
  return std::min<size_type>(__alloc_traits::max_size(this->__alloc_),
                             numeric_limits<difference_type>::max());
}
```

`__vallocate()` (lines 553-568) checks before allocating and documents the
contract in comments above it:

```cpp
//  throws length_error if __n > max_size()
//  throws (probably bad_alloc) if memory run out
...
if (__n > max_size())
  __throw_length_error();
auto __allocation = std::__allocate_at_least(this->__alloc_, __n);
__begin_ = __allocation.ptr;
__end_   = __allocation.ptr;
__cap_   = __begin_ + __allocation.count;
__annotate_new(0);
```

`__recommend()` (lines 886-895) guards the growth multiplication:

```cpp
const size_type __ms = max_size();
if (__new_size > __ms)
  this->__throw_length_error();
const size_type __cap = capacity();
if (__cap >= __ms / 2)
  return __ms;
return std::max<size_type>(2 * __cap, __new_size);
```

The `__cap >= __ms / 2 → return __ms` branch is what makes `2 * __cap`
provably non-overflowing. Call sites use `__recommend(size() + __n)` (e.g.
lines 943, 1114, 1196, 1265) after relying on the invariant established by the
`__n <= max_size()` family of checks.

### 5.2 `basic_string`

**[OBSERVED]**

- `max_size()` (`libcxx/include/string:1305-1313`) combines
  `__alloc_traits::max_size(__alloc_)`, `numeric_limits<size_type>::max()/2`,
  the MSB short/long flag, `__endian_factor`, and `__alignment`:
  `if (__m <= max/2) return __m - __alignment; else return (__uses_lsb ? __m - __alignment : (__m/2) - __alignment);`
- `__recommend()` (lines 2087-2099) aligns with `__align_it<__boundary>`
  (`(__s + (__a-1)) & ~(__a-1)`), bumps `__guess` by `__endian_factor` when it
  equals `__min_cap`, and asserts `_LIBCPP_ASSERT_INTERNAL(__guess >= __s,
  "recommendation is below the requested size")`.
- `__init()` (2337-2350): `if (__reserve > max_size()) __throw_length_error();`
  then either SSO or `std::__allocate_at_least(__alloc_, __recommend(__reserve) + 1)`.
- `reserve`/`append`/`resize` entry points repeat the `> max_size()` guard at
  lines 2361, 2391, 2409, 2472, 3351, each followed by
  `__throw_length_error()` (line 2260-2261:
  `std::__throw_length_error("basic_string")`).
- `__grow_by_and_replace()` (2505-2540) uses a **subtraction-form overflow
  guard**, which is the strongest pattern here:

```cpp
size_type __ms = max_size();
if (__delta_cap > __ms - __old_cap - 1)
  __throw_length_error();
...
size_type __cap =
    __old_cap < __ms / 2 - __alignment
        ? __recommend(std::max(__old_cap + __delta_cap, 2 * __old_cap))
        : __ms - 1;
```

  The subtraction is safe because `__old_cap <= __ms - 1` is a class invariant;
  it avoids the addition overflow that `__old_cap + __delta_cap` would risk.
  The legacy `__grow_by()` at 2549+ uses the weaker
  `if (__delta_cap > __ms - __old_cap)` and is marked deprecated precisely
  because it "may not update the size at all" — an ABI-kept path, not a model.
- Exception safety of the reallocation path is expressed with a scope guard:
  line 2527-2529 uses `__annotate_delete();` then
  `auto __guard = std::__make_scope_guard(__annotate_new_size(*this));`.
- `operator[]` asserts `__pos <= size()` (lines 1345, 1353) — deliberately
  inclusive of the terminating NUL, unlike vector's `__n < size()`.

---

## 6. Diagnostic / annotation patterns useful for Agent 1 coordination

### 6.1 ASan container annotations — `libcxx/include/__debug_utils/sanitizers.h`

**[OBSERVED]** libc++ re-declares the sanitizer entry points itself under
`extern "C"` when `_LIBCPP_HAS_ASAN` is set (lines 17-25):

```cpp
extern "C" {
_LIBCPP_EXPORTED_FROM_ABI void
__sanitizer_annotate_contiguous_container(const void*, const void*, const void*, const void*);
_LIBCPP_EXPORTED_FROM_ABI void __sanitizer_annotate_double_ended_contiguous_container(...);
_LIBCPP_EXPORTED_FROM_ABI int
__sanitizer_verify_double_ended_contiguous_container(...);
}
```

The wrappers `__annotate_contiguous_container`, `__annotate_new`,
`__annotate_delete`, `__annotate_increase`, `__annotate_shrink` (used at
`__vector/vector.h:532,566,683-710`) compile to no-ops when
`_LIBCPP_HAS_ASAN == 0` and also skip during constant evaluation
(`!__libcpp_is_constant_evaluated()`).

Crucially, annotations are gated by a **public customization point** (lines
37-41):

```cpp
template <class _Alloc>
struct __asan_annotate_container_with_allocator : true_type {};
```

with the comment: *"`__asan_annotate_container_with_allocator` determines
whether containers with custom allocators are annotated. This is a public
customization point to disable annotations if the custom allocator assumes that
the memory isn't poisoned."*

**[INFERENCE]** This is a clean "library declares intent, sanitizer owns shadow
memory, allocator can opt out" three-way contract. Any future C+ stdlib
annotation work should copy this *shape*, not the raw calls.

### 6.2 Termination seam — §4.3 above

`_LIBCPP_VERBOSE_ABORT` can be redefined at compile time or link time, letting a
test or an embedding runtime capture a failure instead of dying. The upstream
test `customize_verbose_abort.compile-time.pass.cpp` (§7) demonstrates it with
`-D_LIBCPP_VERBOSE_ABORT(...)=my_abort(__VA_ARGS__)`.

### 6.3 Death-test harness — `libcxx/test/support/check_assertion.h`

**[OBSERVED]** the harness matches assertion text with a regex that stays
"synchronized with how we format assertions in the library" (lines 39-42):

```cpp
std::regex assertion_format(".*###\\n(.*):(\\d+): assertion (.*) failed: (.*)\\n###");
```

and classifies a failure by `DeathCause` enum: `VerboseAbort`, `StdAbort`,
`StdTerminate`, `Trap`, vs invalid `DidNotDie`, `SetupFailure`, `Unknown`.
`TEST_LIBCPP_ASSERT_FAILURE` is the macro the container tests use to demand a
specific message. **[INFERENCE]** the practical takeaway is to pin the *message
format* as a contract and to have one tagged "assertion occurred" channel that
tests can observe without `-fsanitize` being available.

### 6.4 Clang attributes — `clang/include/clang/Basic/AttrDocs.td`

**[OBSERVED]** relevant attribute docs (line numbers from the fetched file):

- `EnableIfDocs` (972) — `enable_if` selects overloads from argument values;
  the expression is part of the signature and the second argument is a
  human-readable reason string emitted in diagnostics. Note the documented
  experimental/ABI caveats.
- `DiagnoseIfDocs` (1106) — *"emit warnings or errors at compile-time if calls
  to the attributed function meet certain user-defined criteria"*, with
  `warning`/`error` severity and the explicit note that *"run-time checks are
  not emitted for `diagnose_if` attributes"*, plus the difference from
  `enable_if`: overload resolution ignores `diagnose_if` until after candidate
  selection.
- `NoSanitizeDocs` (3457) — `no_sanitize("address","thread")`, `no_sanitize("coverage")`.
- `DisableSanitizerInstrumentationDocs` (3479) — explicitly **not** the same as
  `no_sanitize`: the latter "may still insert instrumentation to prevent false
  positive reports".
- `NoSanitizeAddressDocs` (3491) — `no_sanitize_address` / `no_address_safety_analysis`.
- `LifetimeBoundDocs` (3993) — extends Clang's dangling-reference warnings
  through user functions; references, pointers, `initializer_list<T>`, and
  aggregates are followed by default.
- `NonNullDocs` (4789), `RestrictDocs` (4829), `FormatDocs` (3763) — parameter
  contracts expressible at the declaration.

**[INFERENCE]** `diagnose_if` + `enable_if` + `lifetimebound` are the upstream
way to push "this call is wrong" into compile-time diagnostics. They are
**compiler-owned** (§9), so the C+ stdlib should describe the contract in a way
the C+ front end can key off, not reimplement them as runtime macros.

---

## 7. Upstream tests inspected

All tests below were **fetched and read only — none were compiled or run** by
this study. This distinction matters because this host's toolchain cannot run
parts of the sanitizer suite (§9.3).

| Test (under `libcxx/test/`, at pinned revision) | What it establishes |
|---|---|
| `libcxx/assertions/modes/fast.pass.cpp` | `REQUIRES: libcpp-hardening-mode=fast`; asserts `true` does not fire and `TEST_LIBCPP_ASSERT_FAILURE(... false ...)` does. Comment: *"assertions trigger without the user having to do anything when the fast hardening mode has been enabled by default."* |
| `libcxx/assertions/modes/none.pass.cpp` | Fetched as the counterpart for the "all checks disabled" tier; not quoted. |
| `libcxx/assertions/default_verbose_abort.pass.cpp` | Installs a `SIGABRT` handler, calls `std::__libcpp_verbose_abort("%s", "foo")`, exits success on `SIGABRT`. Proves the default terminates. |
| `libcxx/assertions/customize_verbose_abort.compile-time.pass.cpp` | `-D_LIBCPP_VERBOSE_ABORT(...)=my_abort(__VA_ARGS__)`; proves the compile-time seam. Notes "cross-file macros ... don't work with modules". |
| `libcxx/assertions/single_expression.pass.cpp` | Proves `_LIBCPP_ASSERT` and `_LIBCPP_ASSUME` are each a **single expression**, usable in constructor initializer lists (`(_LIBCPP_ASSERT(true,"m"), 3)`). |
| `libcxx/containers/sequences/vector/assert.index.oob.pass.cpp` | `UNSUPPORTED: libcpp-hardening-mode=none`; checks both `min_allocator` and default allocator; expects message `vector[] index out of bounds` via `TEST_LIBCPP_ASSERT_FAILURE(c[1], ...)`. |
| `libcxx/containers/sequences/vector/assert.pop_back.empty.pass.cpp` | Fetched as the empty-container counterpart; not quoted. |
| `libcxx/containers/sequences/vector/asan.pass.cpp` | `REQUIRES: asan`; calls `__sanitizer_set_death_callback(do_exit)`, verifies `is_contiguous_container_asan_correct(v)` after inserts/reserve, then verifies `__sanitizer_verify_contiguous_container(...)` returns false after reserve and that `volatile T foo = c[c.size()]` triggers ASan. Also covers `min_allocator` and an `unaligned_allocator`. |
| `libcxx/containers/sequences/vector/asan_throw.pass.cpp` | Fetched; annotation behavior on throwing paths; not deeply inspected. |
| `libcxx/containers/sequences/vector/asan_turning_off.pass.cpp` | Specializes `std::__asan_annotate_container_with_allocator<user_allocator<T>> : false_type` for an allocator that reuses freed buffers (Chromium bug 1419798); proves the opt-out. |
| `libcxx/test/support/check_assertion.h` | Death-test framework; see §6.3. |

Discovery method: recursive GitHub tree API at the pinned commit (`truncated:
true`, 63 908 entries), filtered to `libcxx/test/`. Because the tree was
truncated, this list is **not exhaustive**; it is the set actually inspected.

---

## 8. Mapping to C+ stdlib, with current gaps

The repository-side documents cited below were read at their committed state on
`agent/modern-stdlib`. They are the actual locations of the paths named in the
brief (§1.2).

| Upstream mechanism (source) | C+ stdlib today | Gap / difference, with citation |
|---|---|---|
| `_LIBCPP_ASSERT_VALID_ELEMENT_ACCESS` in `operator[]` (`__vector/vector.h:403-407`) + unconditional `at()` throw (410-418) | `vector.hp:155-161` `at()` returns `0` and calls `stdlib_note(STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS, ...)`; `at_checked` (165-175) returns `-1` | **No two-tier "assert vs throw" split**: C+ has only the tagged runtime return path; compile-time `[CPLUS-BOUNDS]` rejection is a compiler gap (`docs/libc/AGENT1_COMPILER_GAPS.md:62,373-435`). Audit: `tests/stdlib/agent2_audit/module_audits/vector.hp.md:97-98` marks both `[VERIFIED]` for runtime behavior. |
| Twelve categorized assert kinds × four hardening tiers (`libcxx/include/__assert:38-120`) | `stdlib/error.hp:45-53` has nine `STDLIB_CATEGORY_*` values (OK, INVALID, RANGE, IO, MEMORY, BOUNDS, OWNERSHIP, LIFECYCLE, UTF8) and one `stdlib_note` channel; there is no compile-time tier selection | Categories are **always on** and indistinguishable from "hardening off" builds; no `FAST/EXTENSIVE/DEBUG` analog. [INFERENCE] a C+ compile-time tier flag could be a future Agent-1 coordination item, but is not present today. |
| `_LIBCPP_ASSERT` routes to `_LIBCPP_ASSERTION_HANDLER` / `_LIBCPP_VERBOSE_ABORT` (§4.2, §4.3) | `stdlib_note` + `stdlib_set_error_callback` in `error.hp` plus `CPLUS_EXIT_*` codes | Functionally analogous *single seam*, different name/payload. C+ also has OFF-by-default atomic telemetry (`docs/libc/AGENT2_CURRENT_AUDIT.md` §2.C). No upstream code copied. |
| `max_size()` = min(allocator max, `difference_type` max) (`__vector/vector.h:393-395`) | `vector.hp:116-120` checks `want > STDLIB_ALLOC_LIMIT / sizeof(int)`; `STDLIB_ALLOC_LIMIT` defaults to `1<<40` (`error.hp:61-67`) | C+ uses a fixed limit, **not** allocator- and pointer-difference-derived. Audit: `tests/stdlib/agent2_audit/module_audits/vector.hp.md:81-83` notes the default limit and that the growth boundary is reachable with a 513-int buffer. |
| `__recommend()` half-max guard (`__vector/vector.h:886-895`); string `__delta_cap > __ms - __old_cap - 1` (`libcxx/include/string:2512-2514`) | `vector.hp:138-145` doubling with `(space > (LIMIT/sizeof(int))/2)` guard; `string.hp:209-214` `want > STDLIB_ALLOC_LIMIT` | Same *shape*, different constant. Both audited `[VERIFIED]`: `tests/stdlib/agent2_audit/module_audits/vector.hp.md:94-96` and `string.hp.md:104` (`t4_ovf_reserve_*`). |
| Failure-atomic reallocation + scope guard (`string:2527-2529`, `__allocate_at_least`) | `vector.hp:111-127`, `string.hp:209-230` allocate fresh, copy, then release old; failure returns `-1` with state untouched | C+ deliberately returns an error code instead of throwing `length_error`/`bad_alloc`; `docs/libc/AGENT2_CURRENT_AUDIT.md` §2.C and `tests/stdlib/agent2_audit/module_audits/vector.hp.md:78-81` pin fault-injection atomicity. |
| ASan container annotations, opt-out trait (`__debug_utils/sanitizers.h`, §6.1) | **Not implemented.** `docs/libc/ASAN_AUDIT.md:§2` explicitly decides *not* to add `asan_interface.h` poisoning; the allocators are thin wrappers over host `malloc`/`calloc`/`realloc`/`posix_memalign` | `docs/libc/ASAN_AUDIT.md:§1-2` gives four reasons (nothing to unpoison; couples release libs to ASan; could hide real bugs; OOB already detected by the interceptor). Experiment 5 there shows a 1-byte overflow through `cplus_malloc(8)` is reported with **no source change**. |
| `string::operator[]` allows `pos <= size()` incl. terminator (`libcxx/include/string:1345,1353`) | `string.hp:151` `at()` + `substr`/`field` clamps | C+ `string` has no terminator-inclusive checked accessor; see `tests/stdlib/agent2_audit/module_audits/string.hp.md:218-235` for the residual alias/overlap risk. |
| `__sanitizer_acquire_crash_state()` ("only one report") (`common_interface_defs.h:111`) | `stdlib_note` telemetry counts violations; no crash-state arbitration | [INFERENCE] relevant if a future C+ runtime tries to report multiple violations; today each call is independent. |
| `diagnose_if`/`enable_if`/`lifetimebound` (§6.4) | C+ `[CPLUS-BOUNDS]`, `[CPLUS-LIFETIME]`, `[CPLUS-OWNER]`, `[CPLUS-SAFE-*` diagnostic tags produced by the C+ front end | Compiler-owned, currently incomplete: `docs/libc/AGENT1_COMPILER_GAPS.md:58-63` lists class-field lifetime tracking and stdlib class bounds metadata as Critical/High gaps; `:407-411` notes `compiler/Analysis/BoundsAnalysis.cpp` is a 1-line placeholder. |

### 8.1 The sanitizer matrix as shipped

**[OBSERVED]** `tests/stdlib/agent2/SANITIZER_MATRIX.md:§3-§7`: 9 deep tests × 8
configurations = 72 build attempts. `address`, `leak`, `thread`, `memory`, and
`cfi` were all rejected at build by the driver compile-and-run probe against
`/usr/bin/clang` (Apple clang 16); no binary, no run. Only `strict_none`,
`ubsan`, and `integer` ran (27 runs). Four `integer` runs aborted with a genuine
UBSan report from `cplus_runtime.c:174:5` (the splitmix64 multiplier
`x *= (uintptr_t)0xbf58476d1ce4e5b9ULL;`) and are recorded as FAIL, not
UNSUPPORTED. 23 runs passed with byte-exact fixtures. The document's honesty
statement: *"`address` was not executed on any test — its cells are
UNSUPPORTED, not PASS."*

Corroboration from `docs/libc/ASAN_AUDIT.md:§3.1-3.2`: Apple clang 16's ASan
runtime aborts at startup for even `int main(){return 0;}` with
`sanitizer_malloc_mac.inc:189 "((!asan_init_is_running)) != (0)"`; Homebrew
clang 23 with `-isysroot /Library/Developer/CommandLineTools/SDKs/MacOSX.sdk`
works. [INFERENCE] any future upstream-derived ASan verification must therefore
run on Linux CI or with the Homebrew toolchain, not the project default
`CPLUS_CLANG=/usr/bin/clang`.

---

## 9. What is compiler-owned (Agent 1) and must NOT be implemented in stdlib

### 9.1 Compile-time diagnostics and instrumentation

**[OBSERVED]** from `docs/libc/AGENT1_COMPILER_GAPS.md`:

- `:41` — C+ diagnostic tokens are `[CPLUS-BOUNDS]`, `[CPLUS-LIFETIME]`,
  `[CPLUS-RAII]`, `[CPLUS-LIBC]`, `[CPLUS-FFI]` (plus `[CPLUS-OWNER]`,
  `[CPLUS-SAFE-*]` elsewhere).
- `:58-63` — class-field lifetime tracking for `drop()`/`close()` and stdlib
  class bounds metadata are **Critical/High compiler gaps**; dynamic array
  bounds are only UBSan-caught today.
- `:460-461` — *"runtime error: index 5556 out of bounds for type 'handler_fn[3]'
  — emitted by **Clang UBSan**, not by C+. With `--sanitize=none` there is no C+
  check at all."*
- `:474` / `:493-495` — acceptance requires a C+ tagged diagnostic that is
  *"distinguishable from UBSan"* and works with `--sanitize=none`; *"A UBSan-only
  abort does not count."*
- `:433-435` — raw stale-buffer dereference has **no** C+ bounds diagnostic and
  only optional `malloc_s`/`cplus_memcpy_s` runtime mitigation.
- `:529`, `:556`, `:567` — the nested-UBSan `InReport` limitation after
  `longjmp` recovery is documented and "must not be claimed as fixed".

Therefore the following are **Agent 1 / compiler-runtime responsibilities**, not
stdlib features:

1. `[CPLUS-BOUNDS]` / `[CPLUS-SAFE-BOUNDS]` static and dynamic bounds analysis
   (§6.4's `diagnose_if`/`enable_if` are the Clang-side analog).
2. `[CPLUS-LIFETIME]` / `[CPLUS-OWNER]` / `[CPLUS-RAII]` / `[CPLUS-FFI]`
   lifetime, ownership, unwind, and FFI diagnostics (`LifetimeBoundDocs`, §6.4).
3. All sanitizer instrumentation and runtime callbacks: ASan/UBSan/TSan/LSan/
   HWASan intercepts, `__sanitizer_*` and `__asan_*` entry points (§3), shadow
   memory, quarantine, `-fsanitize=undefined,bounds,null,alignment` and
   `-fno-sanitize-recover=all` (compile flags recorded in
   `tests/stdlib/agent2_audit/module_audits/vector.hp.md:258-261`).
4. UBSan option/override hooks (`__ubsan_default_options`, the public UBSan
   interface in §3.2) and the nested-report behavior — a library cannot fix
   `InReport` re-entrancy.
5. Compile-time hardening-tier selection. libc++'s `_LIBCPP_HARDENING_MODE`
   is set by the build, not by a header author (§4.2).

### 9.2 What the stdlib must provide instead

The C+ stdlib's legitimate lane is the **runtime, tagged, failure-atomic**
lane that works even with `--sanitize=none`:

- return `-1`/`0` plus `stdlib_note(category, CPLUS_EXIT_*, message)`;
- failure-injectable allocation and telemetry counters (`error.hp:226-251`,
  `docs/libc/AGENT2_CURRENT_AUDIT.md` §2.C);
- checked accessors (`at_checked`), stale slices (`slice::stale()`), and
  explicit `valid()` for invalid constructors.

`docs/libc/AGENT2_CURRENT_AUDIT.md` §3 (host limits) and
`tests/stdlib/agent2_audit/module_audits/vector.hp.md:21-25` both state the
boundary explicitly: passing fixtures are evidence of *contract conformance*,
**not proof of memory safety**; with ASan unavailable, out-of-bounds /
use-after-free classes are not ruled out. `vector.hp:17-19` declares a slice
outliving its owner a lifetime error the library **cannot detect**.

### 9.3 What must not be copied even though the license allows it

- Do **not** add `__asan_poison_memory_region` / `__asan_unpoison_memory_region`
  or container annotations to the hardened allocators — see
  `docs/libc/ASAN_AUDIT.md:§2` (four reasons, quoted in §8 table).
- Do **not** fabricate compiler diagnostics inside a header by mimicking
  `[CPLUS-*]` strings; that would be an untagged or duplicated diagnostic and
  is explicitly rejected in `docs/libc/AGENT1_COMPILER_GAPS.md:25,29`.
- Do **not** claim live-sanitizer success from a build or emulator result alone
  (project `AGENTS.md` verification rule).

---

## 10. Licenses and what may be ported

**[OBSERVED]** identifier: `Apache-2.0 WITH LLVM-exception` (§2). The LLVM
exception grants additional permission to link with other code; the source
headers require the license/SPDX notice to be retained when copied.

The repository already demonstrates the sanctioned pattern verbatim:
`libc/src/c23_upstream_small.c:72-80` reproduces the upstream license header
for `libc/src/string/memset_explicit.cpp`, documents the blob SHA, and lists
the minimal adaptations. `docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md:46-50` states
the integration rule: *"copy only when the upstream dependency closure is
self-contained, or depends solely on libc functions this project already
provides ... if it needs LLVM libc C++ internals (`src/__support/...`), do not
copy — record the exact blocker."*

**[INFERENCE]** applied to this study:

| Upstream artifact | Port decision | Reason |
|---|---|---|
| `_LIBCPP_ASSERT` / categorized assert matrix (§4.2) | **Port the pattern, not the code.** C+ already has `stdlib_note` + categories; adopting 12 named check kinds and 4 tiers is a design change requiring orchestration, and the handler header is CMake-generated. |
| `_LIBCPP_VERBOSE_ABORT` overridable seam (§4.3) | **Safe to mirror conceptually.** Small, public, self-contained; C+ already has `stdlib_set_error_callback`. No need to copy code. |
| `max_size()` dual bound and subtraction-form overflow guard (§5) | **Safe to mirror as an algorithm shape** (no upstream code survives translation); C+ already implements analogous guards with its own limit. |
| `__sanitizer_annotate_contiguous_container` (§3.3, §6.1) | **Do not port** into current allocators — `docs/libc/ASAN_AUDIT.md:§2`. Revisit only if C+ ever suballocates from arenas/pools. |
| `asan_interface.h` / `ubsan_interface.h` / `tsan_interface.h` declarations | **Do not vendor.** They are compiler-runtime ABI, available from the toolchain; copying them would couple the library to a sanitizer build. |
| `libc/src/string/memset_explicit.cpp` | **Already ported** at `llvmorg-20.1.1` per §11. |
| `clang/include/clang/Basic/AttrDocs.td` | **Do not vendor** (documentation generation input). Use it only as design evidence for Agent 1. |

Any future copy must reproduce the SPDX header, cite the exact pinned URL and
blob SHA, and record every adaptation — the convention demonstrated in
`docs/libc/C23_SMALL_UPSTREAM.md:17-21,58-62`.

---

## 11. `memset_explicit` provenance — verification

The task's claim that "this repo already copied `memset_explicit` from LLVM libc
at llvmorg-20.1.1" is **[VERIFIED]** from both sides:

**Repository side:**

- `docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md:17-33` pins LLVM libc to tag
  `llvmorg-20.1.1`, commit `424c2d9b7e4de40d0804dd374721e6411c27d1d1`, and
  records `memset_explicit.cpp` blob `a3e4dad874397463cb5315679f99a312ac3a53fe`
  at `docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md:29-30`.
- `docs/libc/C23_SMALL_UPSTREAM.md:17-21` marks `memset_explicit` **CLOSED**,
  source `libc/src/string/memset_explicit.cpp`, blob
  `a3e4dad874397463cb5315679f99a312ac3a53fe`, license
  `Apache-2.0 WITH LLVM-exception`, *"Body copied; only the 5 documented minimal
  adaptations applied"*; `:60-62` gives the exact raw URL.
- `libc/src/c23_upstream_small.c:5-20,67-103` contains the vendored body, the
  upstream SPDX header, the blob SHA, and the five numbered adaptations:
  (1) rename to `cplus_memset_explicit`; (2) `[[gnu::noinline]]` → C spelling;
  (3) `inline_memset(...)` → existing `cplus_memset` because the upstream
  `inline_memset.h` closure is **not** self-contained; (4) `__asm__` instead of
  C++ `asm`; (5) one `cplus_libc_log` marker.

**Upstream side (this study):** fetched
`.../424c2d9b7e4de40d0804dd374721e6411c27d1d1/libc/src/string/memset_explicit.cpp`
(HTTP 200, 1 022 bytes). Its body is exactly:

```cpp
[[gnu::noinline]] LLVM_LIBC_FUNCTION(void *, memset_explicit,
                                     (void *dst, int value, size_t count)) {
  inline_memset(dst, static_cast<uint8_t>(value), count);
  // avoid dead store elimination
  // The asm itself should also be sufficient to behave as a compiler barrier.
  asm("" : : "r"(dst) : "memory");
  return dst;
}
```

with SPDX header `Apache-2.0 WITH LLVM-exception`. This matches the five
adaptations claimed locally, including the `noinline` + empty-asm memory
barrier and the return value. The blob SHA was **not** recomputed in this study
(it is a git blob hash, not a plain file hash); it is quoted from the local docs
only.

---

## 12. Top 3 reusable patterns

1. **Single-expression, categorized, tiered assertions with one overridable
   termination seam.** `_LIBCPP_ASSERT` is one expression
   (`libcxx/include/__assert:18-23`), split into 12 named `_LIBCPP_ASSERT_*`
   categories enabled by `_LIBCPP_HARDENING_MODE` (`FAST` / `EXTENSIVE` /
   `DEBUG` / none), and all terminate through `_LIBCPP_ASSERTION_HANDLER` /
   `_LIBCPP_VERBOSE_ABORT` (`__verbose_abort:23-53`). C+ analogue already
   present: `stdlib_note` + `STDLIB_CATEGORY_*` + `stdlib_set_error_callback`;
   the missing piece is a **tier** and a pinned message format that tests can
   match (§6.3).

2. **Overflow guards expressed as subtraction against a precomputed safe
   maximum, not as checked addition.** `string.hp`'s upstream model is
   `if (__delta_cap > __ms - __old_cap - 1) __throw_length_error();`
   (`libcxx/include/string:2512-2514`), and `vector::__recommend()` uses
   `__cap >= __ms/2 → return __ms` before any `2 * __cap`
   (`__vector/vector.h:886-895`). C+ already uses the analogous
   `want > STDLIB_ALLOC_LIMIT / sizeof(int)` and half-limit doubling guards
   (`stdlib/vector.hp:116-120,138-145`); these should be kept and extended to
   every size arithmetic site rather than replaced by after-the-fact checks.

3. **ASan annotations as an optional, allocator-opt-out-able layer, never as a
   mandatory library dependency.** `__annotate_new/delete/increase/shrink`
   compile to no-ops without `_LIBCPP_HAS_ASAN` and are gated by the public
   `__asan_annotate_container_with_allocator<_Alloc>` trait
   (`__debug_utils/sanitizers.h:31-41,43-96`). For this repository the correct
   decision is currently "no annotations at all" (`docs/libc/ASAN_AUDIT.md:§2`),
   but the *pattern* — intent in the library, shadow memory owned by the
   runtime, explicit opt-out for custom allocators — is the right shape if
   suballocation is ever introduced.

---

## 13. Failures, limitations, and non-claims

1. **HTTP 404** for eight guessed raw paths and **HTTP 401** for GitHub code
   search, all listed in §1.1. The correct paths were found via the recursive
   tree API, which itself returned `truncated: true` (63 908 entries), so §7's
   test list is not exhaustive.
2. **`_LIBCPP_VERBOSE_ASSERT` not found** at the pinned revision (§4.2). The
   study reports the actual `_LIBCPP_ASSERT` / `_LIBCPP_VERBOSE_ABORT` mechanism
   instead and does not fabricate a definition.
3. **No upstream test was executed.** §7 is a source-inspection list. The only
   executed experiments cited are the repository's own, already committed in
   `docs/libc/ASAN_AUDIT.md` and `tests/stdlib/agent2/SANITIZER_MATRIX.md`.
4. **No live-game, live-compiler, or sanitizer success is claimed** by this
   document. Host ASan is broken for `/usr/bin/clang` (Apple clang 16) per
   `docs/libc/ASAN_AUDIT.md:§3`, and the C+ driver rejects `address` per
   `SANITIZER_MATRIX.md:§3`.
5. **No per-file blob SHA was recomputed.** The `a3e4dad...` SHA in §11 is
   quoted from repository docs, not independently hashed.
6. `libcxx/include/__vector/vector_bool.h`,
   `libcxx/include/__memory/allocator_traits.h`, `libcxx/src/verbose_abort.cpp`,
   and `libcxx/test/.../asan_throw.pass.cpp` were fetched but not deeply
   inspected; no claim in this document rests on them.
7. Path corrections in §1.2: the brief's `docs/stdlib/agent2_audit/...` and
   `docs/stdlib/agent2/SANITIZER_MATRIX.md` are actually under `tests/stdlib/`.

---

## 14. Return summary (for the coordinating session)

- **Document:** `docs/stdlib/upstream_study/llvm_clang.md`
- **Revision:** `424c2d9b7e4de40d0804dd374721e6411c27d1d1` (`llvmorg-20.1.1`).
- **URLs:** 31 raw GitHub URLs + 1 GitHub API tree URL, all recorded in §1 with
  HTTP status and byte counts; 8 guessed URLs returned 404, 1 code-search URL
  returned 401. One `webfetch` (ubsan_interface.h) succeeded.
- **License:** `Apache-2.0 WITH LLVM-exception` (LICENSE.TXT identifier quoted
  in §2; per-file SPDX headers observed).
- **Top 3 reusable patterns:** (1) categorized, tiered, single-expression
  assertions with one overridable abort seam; (2) subtraction-form max-size /
  half-max overflow guards; (3) optional, allocator-opt-out-able ASan container
  annotations — with "do not port into current allocators" per
  `docs/libc/ASAN_AUDIT.md:§2`.
- **Failures:** `_LIBCPP_VERBOSE_ASSERT` absent at this pin; eight 404 paths;
  GitHub code search 401; truncated tree API; no upstream tests executed;
  brief's local doc paths were under `tests/stdlib/` rather than `docs/stdlib/`.
