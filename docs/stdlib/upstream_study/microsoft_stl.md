# Microsoft STL upstream study — vector / string safety designs

Branch under study: `agent/modern-stdlib`.
Scope: study only. **No file other than this document was created or modified.**
Evidence labels: **[OBSERVED]** = text retrieved from the exact URLs in §1 or from
the committed local audit documents; **[INFERENCE]** = my design reading, not a
quoted upstream fact. No tests were executed for this study.

---

## 1. Retrieval log (exact URLs, status, revision)

All raw fetches below were made against branch `main` of
`https://github.com/microsoft/STL`. Raw `raw.githubusercontent.com` responses do
not embed a commit id; the revision was obtained separately from the GitHub API.

| # | URL | Result | Notes |
|---|-----|--------|-------|
| 1 | `https://raw.githubusercontent.com/microsoft/STL/main/LICENSE.txt` | SUCCESS | Full Apache-2.0 + LLVM exception text returned. |
| 2 | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/vector` | SUCCESS | 4194 lines. SPDX header `Apache-2.0 WITH LLVM-exception`. |
| 3 | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/xstring` | SUCCESS | 3671 lines. SPDX header `Apache-2.0 WITH LLVM-exception`. |
| 4 | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/xmemory` | SUCCESS | 2848 lines. SPDX header `Apache-2.0 WITH LLVM-exception`. |
| 5 | `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/xutility` | SUCCESS (via `curl` in shell; used only to check `_Xlength_error` declaration at line 7831) | Not a primary source for this study. |
| 6 | `https://api.github.com/repos/microsoft/STL/commits/main` | SUCCESS | `sha = 0cabaf3c7e0a73fe3040aabce40cd8833e80392b`, author date `2026-11-21T17:52:40Z`, message `` `<cmath>`: Avoid intrinsics for CUDA again (#6530) ``. This is the tip of `main` at study time; raw files are from `main`, i.e. this tree. **No other commit hash is asserted.** |
| 7 | `https://api.github.com/repos/microsoft/STL/git/trees/0cabaf3c7e0a73fe3040aabce40cd8833e80392b?recursive=1` | SUCCESS | `truncated: false`, 3593 entries. Used to discover the test paths in §8. |
| 8 | `https://raw.githubusercontent.com/microsoft/STL/main/tests/std/tests/Dev10_579381_vector_grow_to/test.cpp` | SUCCESS | Growth expectations. |
| 9 | `https://raw.githubusercontent.com/microsoft/STL/main/tests/std/tests/GH_005472_do_not_overlap/test.cpp` | SUCCESS | Debug-mode overlapping-range death tests. |
| 10 | `https://raw.githubusercontent.com/microsoft/STL/main/tests/std/tests/GH_002030_asan_annotate_vector/test.cpp` | SUCCESS | ASan annotation + exception-safety matrix. |
| 11 | `https://raw.githubusercontent.com/microsoft/STL/main/tests/std/tests/GH_002030_asan_annotate_string/test.cpp` | SUCCESS | ASan annotation + append/construct matrix (large output; first 1225 lines inspected). |
| 12 | `https://raw.githubusercontent.com/microsoft/STL/main/tests/std/tests/P0966R1_string_reserve_should_not_shrink/test.cpp` | SUCCESS | Exact capacity values for string reserve/growth. |
| 13 | `https://raw.githubusercontent.com/microsoft/STL/main/tests/std/tests/P1206R7_string_append_range/test.cpp` | SUCCESS | Reallocating vs non-reallocating `append_range` paths. |

**Fetch failures: none.** One attempted lookup produced no match rather than a
fetch error: `https://raw.githubusercontent.com/microsoft/STL/main/stl/inc/xutility`
does **not** define `_Xlength()`; the actual `vector` definition was found in
`stl/inc/vector:2186-2188`. The task brief names a function
`_Calculate_new_capacity`; **[OBSERVED]** the current `main` sources use
`_Calculate_growth` in both `vector` and `basic_string`, and no
`_Calculate_new_capacity` appeared in the fetched files. I did not invent a
source for it.

**Path correction (local files).** The task brief cited
`docs/stdlib/agent2_audit/module_audits/{vector,string}.hp.md`; that directory did
not exist on this branch. The actual committed audits are
`tests/stdlib/agent2_audit/module_audits/vector.hp.md` and
`tests/stdlib/agent2_audit/module_audits/string.hp.md`. All gap citations in §9
use those actual paths.

---

## 2. License (exact identifier)

Fetched `LICENSE.txt` begins verbatim:

> The Microsoft C++ Standard Library is under the Apache License v2.0 with LLVM Exception:

Each of `vector`, `xstring`, `xmemory` carries the header line:

> `// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`

**Exact identifier: `Apache-2.0 WITH LLVM-exception`** (Apache License, Version
2.0 with LLVM Exceptions). The LLVM exception relaxes sections 4(a), 4(b), 4(d)
for portions embedded in object form and adds a GPLv2 compatibility waiver.
**[INFERENCE]** For this repo it means: study and re-express the *ideas* in C+
headers; do not paste MSVC source text, and retain attribution if any
substantial excerpt were ever carried (we carry none).

---

## 3. Concrete growth / capacity algorithms

### 3.1 `vector` (`stl/inc/vector`)

**[OBSERVED] Bounds.** `vector::max_size()` (lines 1920-1923):

```cpp
constexpr auto _Diff_max = static_cast<size_type>(_STD _Max_limit<difference_type>());
return (_STD min) (_Diff_max, _Alty_traits::max_size(_Getal()));
```

i.e. the effective cap is `min(PTRDIFF_MAX-equivalent, allocator max_size())`.
`_Buy_raw()` (2034-2035) asserts `0 < _Newcapacity && _Newcapacity <= max_size()`
before allocating.

**[OBSERVED] Growth policy** — `_Calculate_growth(_Newsize)` (2009-2025):

```cpp
const size_type _Oldcapacity = capacity();
const auto _Max = max_size();
if (_Oldcapacity > _Max - _Oldcapacity / 2) return _Max; // geometric would overflow
const size_type _Geometric = _Oldcapacity + _Oldcapacity / 2;
if (_Geometric < _Newsize) return _Newsize;              // geometric insufficient
return _Geometric;                                       // geometric sufficient
```

Three distinct decisions: (a) saturate instead of wrapping; (b) never grow by
less than the actual demand; (c) otherwise 1.5x, not 2x.

**[OBSERVED] Overflow check shape.** Every reallocating path checks demand using
subtraction, before computing the new size — e.g. `_Emplace_reallocate` lines
887-892 uses `if (_Oldsize == max_size()) _Xlength();`; `_Append_counted_range`,
`insert(count)`, `_Insert_counted_range`, `_Resize`, `reserve` use the form
`if (_Count > max_size() - _Oldsize) _Xlength();` (976, 1094, 1186, 1552, 1706,
1729). `reserve` explicitly documents "never shrinks" (1725-1733).

**[OBSERVED] Demand-vs-geometric separation is intentional.** For a single
element at the back, the code takes the strong-guarantee branch; for larger
inserts it moves existing elements first and then constructs the new range,
commented as "provide basic guarantee" (e.g. 991-999, 1202-1212).

### 3.2 `basic_string` (`stl/inc/xstring`)

**[OBSERVED] Bounds.** `max_size()` (2301-2306) takes
`max(allocator_max, _BUF_SIZE) - 1` — the `-1` reserves the terminator/`npos` —
then clamps to the `difference_type` limit via `_Max_limit`.

**[OBSERVED] Growth policy** — static `_Calculate_growth(_Requested, _Old, _Max)`
(2942-2958):

```cpp
const auto _Masked = static_cast<size_type>(_Requested | _Alloc_mask);
if (_Masked > _Max) return _Max;                 // roundup overflows
if (_Old > _Max - _Old / 2) return _Max;         // geometric overflows
return max(_Masked, static_cast<size_type>(_Old + _Old / 2));
```

`_Alloc_mask` (xstring `_String_val`, around line 400) is `15 / 7 / 3 / 1 / 0`
for element sizes `<=1 / <=2 / <=4 / <=8 / else`; `_Small_string_capacity =
_BUF_SIZE - 1` with `_BUF_SIZE = max(1, 16 / sizeof(value_type))`. Thus `char`
reserves are rounded up to a multiple of 16 minus 1, then geometric 1.5x is
applied and the larger of the two is chosen.

**[OBSERVED] Overflow check shape.** `_Reallocate_grow_by` (2990-2996) uses
`if (max_size() - _Old_size < _Size_increase) _Xlen_string();` — the same
subtract-before-add discipline. `_Reallocate_for` (2961-2966) checks the absolute
target. `_Allocate_for_capacity` (around 2820) increments capacity by one for the
terminator before calling `_Allocate_at_least_helper` and decrements it on
return, keeping `_Myres` terminator-free in the object.

**[OBSERVED]** Test `P0966R1_string_reserve_should_not_shrink/test.cpp` pins the
numbers: capacity 1007 after `reserve(1000)` (`1000|15 = 1007`) and 1510 after
`reserve(1008)` (`1007 + 1007/2 = 1510`); `reserve(res_arg)` never shrinks in
C++20 mode, while the deprecated no-arg `reserve()` may shrink a large empty
string back to capacity 15 (SSO).

**[INFERENCE]** The reusable shape is: **checked demand → round up → saturating
1.5x → commit only after allocation succeeds**, with the check expressed as
`addend > limit - current` instead of `current + addend > limit` so the test
itself cannot overflow.

---

## 4. Failure / exception-safety patterns

**[OBSERVED] Guard objects with ownership flags.** `stl/inc/vector` defines:

- `_Allocation_guard` (around lines 430-440): holds allocator, pointer, size;
  destructor deallocates if `_Ptr` non-null; copy/move assignment deleted.
- `_Reallocation_guard2` (~525-540): owns `_New_begin`, `_New_capacity`,
  `_Constructed_first/_Constructed_last`; destructor destroys the constructed
  range **and** deallocates. Successful paths set `_Guard._New_begin = nullptr`
  then call `_Change_array(...)`. This is the "allocate + construct new buffer,
  commit last" pattern.
- `_Vaporization_guard` (~542-560): destructor rolls back `_Mylast` and destroys
  the detached range on failure.
- `_Tidy_guard` / `_Tidy_deallocate_guard` in `xmemory` (around lines 78-90):
  destructor calls `_Target->_Tidy()` only when `_Target != nullptr`; success
  paths clear `_Target`.

**[OBSERVED] Explicit guarantee tiers are documented at the branch points.**
Examples: `_Emplace_one_at_back` says "provide strong guarantee";
`_Emplace_reallocate` chooses `is_nothrow_move_constructible_v<_Ty> ||
!is_copy_constructible_v<_Ty>` → move, else copy, to keep the strong guarantee
(906-911); non-back inserts say "provide basic guarantee" (912-917).
`_Append_uncounted_range` even records "provide basic guarantee (despite
N4950 [vector.modifiers]/2)" (~955-965).

**[OBSERVED] Alias-safe temporaries.** In `emplace` and `insert(count)` on the
in-place path, MSVC builds `_Alloc_temporary2<_Alty> _Obj(_Al, ... /* or _Val */);
// handle aliasing` before moving elements (around 1050 and 1126). This prevents
`v.insert(v.begin(), v[3])`-style aliasing from being clobbered by the shift.

**[OBSERVED] Catch-based rollback exists but is localized.** `_Insert_counted_range`
uses `_TRY_BEGIN / _CATCH_ALL` around `_Uninitialized_copy_n` and destroys the
partially written region on failure (1228-1235). There is no blanket
`try { ... }` around the whole container.

**[OBSERVED] Debug/ASan guards follow the same pattern.** `_Asan_extend_guard`
and `_Asan_create_guard` (vector, ~350-430) apply shadow-memory annotations in
the constructor and roll them back in the destructor unless `_Release()` is
called; `_Modify_annotation` returns early when `_Count == 0`, with the comment
"avoid calling `_Apply_annotation()` with null pointers when the vector has zero
capacity, see GH-2464". `xstring` mirrors this with the `_ASAN_STRING_*` macros.

**[OBSERVED] Destructor tombstones** (`_MSVC_STL_DESTRUCTOR_TOMBSTONES` in both
`_Vector_val` and `_String_val`): after destruction, pointers are overwritten
with a sentinel value so use-after-destroy is more likely to fault. In
`_String_val` the capacity is deliberately reset to
`(_Small_string_capacity + 1) | _Alloc_mask` so the object *looks* large-mode
and the tombstone is effective.

**[INFERENCE]** For C+ the transferable part is the discipline: allocate
into a local, copy/construct into it, and only then swap/commit; on every early
return explicitly release the local. Because C+ headers already implement
failure-atomic `reserve`/`copy_from`/`shrink_to_fit` by "allocate fresh → copy →
release old", this is a formalisation of an existing idiom, not a rewrite.

---

## 5. String self-aliasing and append handling

**[OBSERVED] Reallocating append** (xstring `_Append`, 3047-3070): the lambda
passed to `_Reallocate_grow_by` first copies the old buffer
(`_Traits::copy(_New_ptr, _Old_ptr, _Old_size)`) and only then copies the source
(`_Traits_copy_batch<_Traits>(_New_ptr + _Old_size, _Ptr, _Count)`), finally
writing the terminator. Because `_Fn` receives the **old raw pointer** captured
*before* deallocation, a source pointer into the old buffer remains valid during
the copy — this is how self-append survives reallocation without an explicit
alias check.

**[OBSERVED] In-place append** (same function, 3051-3060): uses
`_Traits_move_batch<_Traits>(_Old_ptr + _Old_size, _Ptr, _Count)` —
`char_traits::move` (memmove semantics) for ordinary element types, so overlap
of source and destination is legal. For `volatile` elements
`_Traits_move_batch` (~510-548) scans for exact pointer identity and chooses a
forward or backward element loop accordingly.

**[OBSERVED] `append(const basic_string&)`** (1527-1528) forwards
`_Right._Myptr()` and `_Right._Mysize` into the same `_Append`, so `s.append(s)`
takes the identical path; `append(pos,count)` routes through
`basic_string_view::substr` (1546-1551), which clamps the length.

**[OBSERVED] Other mutators use the same "orphan, allocate, fill, commit"
structure**: `_Reallocate_for` (2961-2988) can be parameterised by a function
for `assign`/`replace`/`insert`; `_Eos` (3040-3044) centralises size update +
terminator write and performs the ASan size transition; `_Become_small` (3022-
3038) handles large→SSO transitions with `_Orphan_all()` first.

**[OBSERVED — local contrast]** `stdlib/string.hp:257-280` detects aliasing with
integer addresses in `[chars, chars + count]`, remembers an `offset`, calls
`string_reserve` (which may reallocate), then either `memmove(chars+count,
chars+offset, extra)` or `memcpy(chars+count, text, extra)`. The committed audit
`tests/stdlib/agent2_audit/module_audits/string.hp.md:222-229` (§7.1) records the
highest residual risk: after `clear()`, an interior pointer *past* `count` takes
the `memcpy` branch, i.e. potential overlapping-`memcpy` UB that ASan cannot
confirm on this host.

**[INFERENCE]** The upstream design removes the need for a fragile range test:
make the copy primitive overlap-safe on *every* in-place path, and on the
reallocating path pass the old buffer into the copy callback so any pointer into
it stays meaningful. A C+ adaptation can achieve the same effect by (a) always
using `memmove` for the in-place copy (cheap for the small `extra` values this
header handles), and (b) capturing the source offset *before* `string_reserve`
and re-basing it off the new `chars` afterwards, or simply saving a temporary
copy when `text` lies inside the current buffer.

---

## 6. Bounds / iterator validation in debug mode

**[OBSERVED] Debug level gating.** `vector` checks use
`#if _ITERATOR_DEBUG_LEVEL != 0`; `xstring` uses
`#if _ITERATOR_DEBUG_LEVEL >= 1`; full iterator-chain tracking uses
`_ITERATOR_DEBUG_LEVEL == 2`.

**[OBSERVED] Each iterator carries an owner reference.** `_Vector_const_iterator`
and `_String_const_iterator` derive from `_Iterator_base`; construction calls
`this->_Adopt(_Pvector)` (xmemory: `_Iterator_base12::_Adopt` links the iterator
into `_Container_proxy::_Myfirstiter`; `_Getcont()` returns the owning container
through the proxy). `_Container_base12::_Orphan_all()` (xmemory) invalidates all
registered iterators on mutation.

**[OBSERVED] Checks performed on every operation:**

- `operator*`/`operator->`: `_STL_VERIFY(_Ptr, ... value-initialized ...)`,
  `_STL_VERIFY(_Mycont, ... invalidated (e.g. reallocation occurred, or the
  string was destroyed))`, and range check `_Mycont->_Myfirst <= _Ptr &&
  _Ptr < _Mycont->_Mylast` (vector) or `_Contptr <= _Rawptr &&
  _Rawptr < _Contptr + _Mycont->_Mysize` (string).
- `++` / `--`: check non-null, non-orphaned, and not past `end` / before
  `begin`.
- `_Verify_offset(_Off)`: special-cases `_Off == 0`, then checks the signed
  offset against `_Myfirst - _Ptr` or `_Mylast - _Ptr`; vector writes the
  checks unconditionally (with a no-op branch when IDL==0), string calls it
  only under IDL>=1.
- `_Compat()` on `-`, `==`, `<=>`, `<` etc.: `_STL_VERIFY(this->_Getcont() ==
  _Right._Getcont(), "vector iterators incompatible")` / "string iterators
  incompatible (e.g. point to different string instances)".
- `_Verify_range(_First,_Last)`: same-container **and** `_First._Ptr <=
  _Last._Ptr` ("range transposed").
- `pointer_traits<...>::to_address`: allows `[first, last]` inclusive at the
  end; the vector source contains an explicit comment that an invalidated end
  iterator for a zero-capacity vector cannot be distinguished from a
  value-initialized one (acknowledged limitation, vector ~260-275).
- `vector::emplace` at IDL==2 also verifies the insertion point belongs to this
  container and lies in `[_Myfirst, _Oldlast]` ("vector emplace iterator
  outside range", lines ~1035-1040).
- `vector::_Orphan_range_unlocked` walks the proxy's iterator list and orphans
  only iterators whose `_Ptr` falls in the mutated range (lines ~2196-2210).

**[OBSERVED] Corresponding death test.** `GH_005472_do_not_overlap/test.cpp`
registers death tests (only when `_ITERATOR_DEBUG_LEVEL == 2`) for
`swap_ranges`, `reverse_copy`, `replace_copy`, `replace_copy_if`, `rotate_copy`
and their `ranges` forms when the output range overlaps the input.

**[OBSERVED — local contrast]** `stdlib/vector.hp` uses a different model:
`slice` stores `(int *items, size_t count, const size_t *generation_ref, size_t
generation_at)`; `stale()` compares the live counter, and every shape-changing
operation bumps `generation` (`vector.hp.md:120-134`). `stdlib/string.hp`'s
`string_view` has **no** generation tracking and no library-enforced refusal
(`string.hp.md:127-134`).

**[INFERENCE]** The generation scheme is cheaper and works in C+ today, but it
is *coarse*: it catches shape changes, not writes through an old alias, and it
cannot detect owner destruction (audit `vector.hp.md:208-214`). A useful
middle ground is to adapt the *check taxonomy* — value-initialised / invalidated
/ out-of-range / transposed-range / cross-container — into explicit C+
`stdlib_note` categories on the `at_checked`/`sub`/`field` APIs, without trying
to build the proxy/iterator registry.

---

## 7. Invalid-argument handling

**[OBSERVED] Dedicated noreturn helpers with fixed messages:**

- `vector::_Xlength()` → `_Xlength_error("vector too long")` (2186-2188)
- `vector::_Xrange()` → `_Xout_of_range("invalid vector subscript")` (2190-2192);
  called by `at()` when `size <= _Pos` (1950-1965)
- `vector<bool>::_Xlen()` / `_Xran()` use "vector<bool> too long" (3622-3628)
- `xstring::_Xlen_string()` → `_Xlength_error("string too long")` (573)
- `_String_val::_Xran()` → `_Xout_of_range("invalid string position")` (481);
  called by `_Check_offset(off)` when `_Mysize < off` and
  `_Check_offset_exclusive(off)` when `_Mysize <= off` (467-478)
- `_Clamp_suffix_size(off, size)` clamps a substring count to `_Mysize - off`
  rather than throwing (485-487)

**[OBSERVED] Compile-time rejection before runtime checks:** `static_assert`
for non-object `T`, mismatched allocator value types (vector), `char_type`
mismatch, non-trivially-copyable/non-standard-layout character types
(xstring ~650-680), `nullptr_t` string constructor `= delete` in C++23.

**[OBSERVED] Low-level validity checks in `xmemory`:** `_Get_size_of_n<_Ty_size>`
throws `bad_array_new_length` when `_Count > SIZE_MAX / _Ty_size`;
`_Allocate_manually_vector_aligned` checks `_Block_size <= _Bytes` (add overflow)
and `_STL_VERIFY(_Ptr_container != 0, "invalid argument")` **even in release**
because it dereferences `p[-1]`; `_Adjust_manually_vector_aligned` asserts a
sentinel and verifies the back-shift range; `allocator::deallocate` asserts
`_Ptr != nullptr || _Count == 0`; `_Convert_size` throws length_error on
truncation.

**[OBSERVED — local contrast]** The C+ APIs return `0`/`-1` plus
`stdlib_note(category, code, message)`; audits classify:
`reserve`/`append` overflow → `RANGE`/101, allocation failure → `MEMORY`/107,
null pointer args → `INVALID`/108 (`vector.hp.md:149-154`,
`string.hp.md:151-156`). Several cases are **silent by design**:
`vector::insert(index>used)`, `pop_back(empty)`, `find(NULL)`,
`substr`/`field` with a bad out buffer, `string_view::equals(NULL)`
(`vector.hp.md:99,162-165`; `string.hp.md:100,105,156`).

---

## 8. Upstream tests inspected (all fetched successfully)

| Test (full path under `tests/std/tests/`) | Cases covered |
|---|---|
| `Dev10_579381_vector_grow_to/test.cpp` | Asserts 1.5x growth: `vector<int>(1000)` has capacity 1000; `resize(1003)`, `push_back`, `insert(end, 3 elems)`, `insert(end, 3, val)` → capacity **1500**; `resize(8000)` / `insert(end, 7000 elems)` / `insert(end, 7000, val)` → capacity **8000** (exact demand, not 1500 or doubling). |
| `GH_005472_do_not_overlap/test.cpp` | IDL==2 death tests for overlapping copy-style algorithms (`swap_ranges`, `reverse_copy`, `replace_copy(_if)`, `rotate_copy`, ranges variants). Demonstrates that "invalid range" is a *diagnosable* condition in MSVC debug mode. |
| `GH_002030_asan_annotate_vector/test.cpp` | Shadow-memory invariant checker `verify_vector()` using `__sanitizer_contiguous_container_find_bad_address`; custom allocators with 8-byte alignment / offset buffers / annotation opt-out; full mutation matrix (constructors, push/pop, reserve/shrink, emplace, insert n/range, assign, resize, clear, move/copy assign); throwing element types (`throw_on_construction`, `throw_on_copy`) check the ASan state after each caught exception; death tests for container-overflow. |
| `GH_002030_asan_annotate_string/test.cpp` | Same idea for `basic_string`: `verify_string()` uses `__sanitizer_verify_contiguous_container`, skips SSO; large matrix of constructors, append/operator+=, assign, resize, reserve, erase and throwing conversions, over SSO/large/allocator variants. |
| `P0966R1_string_reserve_should_not_shrink/test.cpp` | Pins exact capacity values (1007, 1510) and the "reserve(res_arg) never shrinks" contract, including C++20 vs deprecated no-arg `reserve()` differences and large→SSO transitions. |
| `P1206R7_string_append_range/test.cpp` | Explicitly tests both reallocating and non-reallocating paths (`test_string(capacity, presize, rng, expected)` with `(0,0)` and `(size+2,2)`), for spans, move-only input/forward/bidi/random-access views, C arrays, lvalue vectors and forward_lists, at compile time and runtime. |

These were located through the recursive tree API (URL #7), not guessed. I did
**not** run any of them; the descriptions are from the retrieved source text.

---

## 9. Mapping to `stdlib/vector.hp` / `stdlib/string.hp` with exact gaps

Gap sources: `tests/stdlib/agent2_audit/module_audits/vector.hp.md` (V) and
`.../string.hp.md` (S). Line numbers refer to those files unless stated.

### 9.1 vector

| # | Upstream mechanism | Current C+ status / cited gap | Adaptation value |
|---|---|---|---|
| V-1 | Subtract-form overflow check + 1.5x saturating growth (`vector:2009-2025`) | `vector.hp:136-148,194-203` uses **doubling** with `space > (LIMIT/sizeof(int))/2` guard; capacity steps pinned at 4→8→16 (V §2, line 72). No `max_size()` bounded by `difference_type`; no "demand bigger than geometric" case because addend is always 1 or `count` and doubling is used. | HIGH — pure integer change, but would change pinned growth/capacity expectations in `DEEP_ERROR_VECTOR_CONTRACT.md:46` and `DEEP_VECTOR_TASK4_CONTRACT.md:48`; coordinate with fixture owners. |
| V-2 | Per-iterator owner proxy + `_Orphan_all` + `_Verify_offset`/`_Compat`/`_Verify_range` (§6) | `slice` generation counter detects shape changes only; no per-alias/iterator validation; slice outliving owner is explicitly undetectable (V §4 lines 127-134; §7.1 lines 208-214); generation wraparound and concurrency untested (V §7.3-7.4). | MEDIUM — borrow the *check taxonomy* as explicit checked APIs; full proxy registry is out of scope. |
| V-3 | Strong/basic guarantee tiers, `_Reallocation_guard2`, `_Alloc_temporary2` for aliasing (§4) | C+ is already failure-atomic for `reserve`/`push_back`/`insert`/`copy_from`/`shrink_to_fit` (V §5 lines 156-161) but this is verified with `int` elements only; no documented guarantee tier for multi-element insert and no alias-temporary concept. | MEDIUM — document the guarantee per operation and keep "allocate fresh → copy → commit". |
| V-4 | `at()` throws `out_of_range` (vector:1950-1965) | Compat `at()` returns `0` for OOR (V §5 lines 166-168; `vector.hp:155-161`); `at_checked` is the preferred API. | LOW — keep sentinel API for compatibility; do not silently change return semantics. |
| V-5 | `_Allocate_at_least_helper` + allocator `max_size()` (xmemory `allocator_traits`) | C+ uses fixed `STDLIB_ALLOC_LIMIT` default `1<<40`, overridden to 4096 in fixtures (V §2 lines 78-85). | MEDIUM — the *subtraction* check technique is portable; allocator_traits machinery is not. |
| V-6 | Tombstone destructor values / ASan container annotations | ASan unavailable on this host; audit explicitly refuses memory-safety claims (V §8 lines 258-264, opening boundary lines 21-28). | LOW/NONE now; see §11. |

### 9.2 string

| # | Upstream mechanism | Current C+ status / cited gap | Adaptation value |
|---|---|---|---|
| S-1 | `_Append` overlap-safe primitive on both paths (§5) | `string.hp:261-280` alias window is `[chars, chars+count]`; interior pointer past `count` after `clear()` uses `memcpy` (S §7.1 lines 222-229, "highest residual risk"). Self/overlap inside the window is VERIFIED (S §3 line 97). | HIGH — replace the range test with an unconditional overlap-safe copy and/or save the source offset before reallocation. This closes a recorded UB-class gap. |
| S-2 | `max_size()` minus terminator, subtract-form `_Reallocate_grow_by`, `_Alloc_mask` roundup (§3.2) | `string.hp:251-256,266-275` uses `LIMIT - 1 - extra`, `count + extra + 1`, and simple doubling capped at `need`; no roundup, no capacity values pinned like 1007/1510. | HIGH for the subtraction discipline; MEDIUM for roundup (would change capacity expectations). |
| S-3 | `_Check_offset`/`_Check_offset_exclusive`/`_Clamp_suffix_size` | `substr:330-331` refuses `start > size()`; `substr`/`field` clamp to `out_size-1` and always NUL-terminate (S §3 lines 106-107). Out-buffer edge `out_size==1` untested (S §7.3 lines 233-236). | MEDIUM — add explicit boundary fixtures; semantics already close. |
| S-4 | `_Xlen_string` / `_Xran` typed exceptions | C+ returns `-1` with `stdlib_note`; several paths are silent: `find(NULL)`, bad `substr`/`field` out buffer, `string_view::equals(NULL)` (S §5 line 156). Telemetry buckets `RANGE` into `STDLIB_TM_INVALID`, so telemetry alone cannot distinguish range from null (S §5 lines 164-167). | MEDIUM — emit `stdlib_note` consistently on currently silent refusals. |
| S-5 | Per-iterator validation / orphaning | `string_view` has no generation tracking and no library-enforced refusal; stale-view tests only compare pointers, never dereference (S §4 lines 127-141). | MEDIUM — at minimum document and test the view lifetime rule. |
| S-6 | Invalid-object state after failed construction | C+ ctor failure yields `valid()==0`; `reserve(0)` after `drop()` stays invalid, half of that nuance unpinned (S §7.2 lines 230-232). | LOW — documentation/fixture work. |
| S-7 | `operator+` concatenation picks the larger existing buffer when it fits | No C+ analogue; `append` allocates via `string_reserve`. | LOW — optimisation, not safety. |

---

## 10. What can be ADAPTED into C+ (without Agent 1 compiler features)

All items below are expressible in the current C+ header style (`static inline`
free functions, classes lowered to struct + functions, plain `char*`/`int*`,
`stdlib_alloc`/`stdlib_release`, `stdlib_note`, `stdlib_tm_*`). None requires a
new compiler feature.

1. **Subtract-form overflow checks.** Replace `current + addend > LIMIT` with
   `addend > LIMIT - current` (guard first that `current <= LIMIT`). Applies to
   `vector::reserve/push_back/insert/copy_from` and `string::reserve/append`.
   Uses only existing `size_t`/`SIZE_MAX` arithmetic.
2. **Saturating geometric growth with demand floor.** A helper such as
   `want = old + old/2; if (want < need) want = need; if (old > cap - old/2) want
   = cap;` is integer-only. For `string`, optionally also `need | 15` for char
   buffers, with a `max_size` clamp; this must be coordinated with tests that
   observe capacity.
3. **Explicit commit-last rollback.** Allocate into a local pointer, copy into
   it, and only on total success release the old buffer and publish the new
   state; on every earlier failure path release the local. Formalise the
   existing C+ pattern and write down which operations promise strong vs basic
   guarantee.
4. **Overlap-safe `append`.** Use `memmove` for the in-place copy unconditionally
   (or re-base a saved source offset after `string_reserve`). Keep the existing
   `uintptr_t` comparison style — the audit explicitly notes that comparing
   integer addresses avoids UB from relational comparison of unrelated pointers
   (S §8 lines 265-268). Extend the alias window to cover the whole allocation
   (`space`), not just `count`, which closes S-1.
5. **Checked offset helpers.** Introduce local helpers analogous to
   `_Check_offset` / `_Check_offset_exclusive` / `_Clamp_suffix_size`, implemented
   as `int checked_offset(...)` returning `-1` plus `stdlib_note(...)` so they
   fit the C+ error model.
6. **Debug check taxonomy for slices/views.** Expand `slice::at_checked`,
   `slice::sub`, `substr`/`field` notes with distinct diagnostic messages for
   "stale", "out of range", "transposed range" and "wrong owner", reusing the
   existing generation reference for slices. Add `stdlib_note` calls to the
   currently silent refusals listed in §9.
7. **Invariant checker in fixtures** (adapted from `verify_vector`/
   `verify_string`): an alloc/free-telemetry-based checker that walks the
   buffer and asserts `size <= capacity` and that `*out` is untouched on
   refusal. This is test-side, needs no ASan, and matches the audits' existing
   telemetry approach.

---

## 11. What CANNOT be adapted (as-is) into C+ today

1. **Exceptions and `throw` semantics** — `bad_alloc`, `length_error`,
   `out_of_range`, `bad_array_new_length`, `_TRY_BEGIN`/`_CATCH_ALL`. C+ reports
   via `0`/`-1` + `stdlib_note`; changing that is an API break and requires
   language/runtime support owned elsewhere.
2. **RAII guard objects whose destructor performs cleanup on scope exit** —
   `_Reallocation_guard2`, `_Tidy_guard`, `_Asan_*_guard`. C+ has compiler-inserted
   `drop` calls for owning classes (audit V §8 lines 248-251, S §8 lines 269-271),
   but relying on arbitrary scope guards would change lowering semantics; this
   belongs to Agent 1. Adapt the *pattern* with explicit cleanup branches, not
   the mechanism.
3. **Iterator proxy registry** — `_Iterator_base12`, `_Container_proxy`,
   `_Orphan_all`, `_Orphan_range`, `_Adopt`, `_Getcont`, per-iterator debug
   lifetime. Needs hidden state in every iterator and thread-safe debug locks;
   not expressible in a plain header today (and `_ITERATOR_DEBUG_LEVEL == 2`
   race behaviour is itself a dedicated upstream test area).
4. **Templates / concepts / `if constexpr` / `noexcept` / `constexpr` /
   `[[noreturn]]` / `strong_ordering` / `operator<=>` / `pointer_traits`** —
   all present in the fetched sources and all outside current C+ header
   features (Agent 1's area).
5. **Fancy pointers, `allocator_traits`, EBO/`_Compressed_pair`,
   POCCA/POCMA/POCS propagation, `allocate_at_least`** — require allocator and
   type-system machinery absent from `stdlib/error.hp`.
6. **SSO union storage and `_String_val` memcpy tricks** — require union/
   placement-new/lifetime control; C+ stores a plain `char*`. Do not attempt.
7. **ASan container annotations** (`__sanitizer_annotate_contiguous_container`,
   `_Asan_aligned_pointers`, tombstones) — need the sanitizer runtime and
   compiler flags; both audits state ASan is unavailable on this host
   (V §8:258-264; S opening boundary:23-28). Also cannot be claimed as verified
   by fixtures here.
8. **MSVC-specific declaration/ABI machinery** (`_CRT_GUARDOVERFLOW`,
   `_NODISCARD`, `__declspec(allocator)`, `#pragma pack`, `_STL_VERIFY`,
   `_Analysis_assume_`).
9. **Direct source copying** — license is permissive but requires retaining
   notices/attribution for copied text; this study transmits only design ideas
   and short quoted snippets for explanation, no vendored code.

---

## 12. Top 3 reusable techniques

1. **Subtract-form capacity arithmetic + saturating growth**
   (`limit - current` checks, `if (old > max - old/2) return max`, geometric
   1.5x floored at demand, string roundup mask). Smallest change, largest
   payoff: it eliminates the overflow *test* itself as a source of wraparound.
2. **Commit-last mutation with scoped rollback and explicit guarantee tiers**
   (allocate/copy into a local, keep a guard that owns the in-flight buffer,
   publish only on success; document strong vs basic per operation). This is the
   structural reason MSVC can promise failure atomicity across many mutators.
3. **One overlap-safe append primitive for both paths** (in-place `move`
   semantics; on reallocation copy the old buffer before touching the source,
   or save/re-base the source offset), plus explicit debug checks for range and
   owner consistency. This directly addresses the highest untested risk in the
   string audit (S §7.1).

---

## 13. Closing limits

- All upstream claims are from `main` at commit
  `0cabaf3c7e0a73fe3040aabce40cd8833e80392b`, obtained 2026-11-23; line numbers
  are from that tree and will drift.
- No upstream test was executed, no injected-DLL/game claim is made, and no
  memory-safety claim follows from the passing C+ fixtures cited by the audits.
- No source file, header, test, or git index entry was modified during this
  study; only `docs/stdlib/upstream_study/microsoft_stl.md` was created.
