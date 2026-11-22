# Upstream study: Microsoft GSL (`span`, `not_null`, `narrow`, `owner`, contracts)

Status: **study report only**. This document does not edit headers, `stdlib/*.hp`, `compiler/**`, or
tests, and does not commit. It records what the actual upstream Microsoft GSL source says, which
exact URLs were fetched, and how the ideas map onto this repository's C+ standard library.

Method: raw source fetched from `https://raw.githubusercontent.com/microsoft/GSL/main/...` plus the
GitHub contents API used to discover real file names. Every finding below is attributed to a file
and line/identifier from that fetched text; nothing is inferred from memory of older GSL releases.

## 0. Fetch log (exact URLs and outcomes)

| # | Exact URL | Result | Notes |
|---|---|---|---|
| 1 | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/span` | **SUCCESS** | Full `gsl::span` header (31,595 B per API); basis for §2. |
| 2 | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/not_null` | **404** | No such file on `main`; `not_null` lives in `include/gsl/pointers`. |
| 3 | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/narrow` | **SUCCESS** | Full `gsl::narrow` header; basis for §4. |
| 4 | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/owner` | **404** | No such file on `main`; `owner` also lives in `include/gsl/pointers`. |
| 5 | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/assert` | **SUCCESS** | `Expects`/`Ensures`/`GSL_SUPPRESS`; basis for §6. |
| 6 | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/util` | **SUCCESS** | `narrow_cast`, `gsl::at`, `final_action`, `gsl::index`. |
| 7 | `https://api.github.com/repos/microsoft/GSL/contents/include/gsl` | **SUCCESS** | Directory listing revealed `algorithm, assert, byte, dyn_array, gsl, narrow, pointers, span, span_ext, util, zstring` (no `not_null`/`owner` files). |
| 8 | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/pointers` | **SUCCESS** | Full `owner`, `not_null`, `strict_not_null` header; basis for §3/§5. |
| 9 | `https://raw.githubusercontent.com/microsoft/GSL/main/LICENSE` | **SUCCESS** | MIT text; quoted in §1. |
| 10 | `https://api.github.com/repos/microsoft/GSL/contents/tests` | **SUCCESS** | Test inventory; **no `narrow_tests.cpp` on `main`**. Narrow cases are in `utils_tests.cpp`. |
| 11 | `https://raw.githubusercontent.com/microsoft/GSL/main/tests/narrow_tests.cpp` | **404** | Suggested path from the task does not exist on `main`. |
| 12 | `https://raw.githubusercontent.com/microsoft/GSL/main/tests/gsl/narrow_tests.cpp` | **404** | Alternate path from the task also does not exist on `main`. |
| 13 | `https://raw.githubusercontent.com/microsoft/GSL/main/tests/assertion_tests.cpp` | **SUCCESS** | Contract death tests. |
| 14 | `https://raw.githubusercontent.com/microsoft/GSL/main/tests/owner_tests.cpp` | **SUCCESS** | `owner` compile-time acceptance/rejection. |
| 15 | `https://raw.githubusercontent.com/microsoft/GSL/main/tests/notnull_tests.cpp` | **SUCCESS** | Constructor/assignment/death tests. |
| 16 | `https://raw.githubusercontent.com/microsoft/GSL/main/tests/span_tests.cpp` | **SUCCESS** | Main `span` conformance suite. |
| 17 | `https://raw.githubusercontent.com/microsoft/GSL/main/tests/utils_tests.cpp` | **SUCCESS** | Contains the actual `narrow`/`narrow_cast` tests. |
| 18 | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/span_ext` | **SUCCESS** | Defines `dynamic_extent`, comparisons, `make_span`, `gsl::at(span,...)`, `ssize`. |

All 18 URLs attempted; 13 succeeded, 5 returned 404 (rows 2, 4, 11, 12; row 7 is the API listing that
explained the first two). Commit SHA of the fetched `span` blob per the API: `090a8bd917d2fb96a2401577f50ed2b30012c437` (other blobs similarly pinned in the API response, not re-fetched at a fixed tag).

## 1. License

Every fetched GSL header carries the same notice:

> Copyright (c) 2015 Microsoft Corporation. All rights reserved.
>
> This code is licensed under the MIT License (MIT).

`LICENSE` starts with the same copyright line and grants "Permission is hereby granted, free of
charge, to any person obtaining a copy of this software and associated documentation files…" and
the standard MIT disclaimer. Exact identifier to use in any attribution: **MIT License (MIT)**,
copyright **2015 Microsoft Corporation**. This repository's own `LICENSE` is "MIT License /
Copyright (c) 2026 Haseeb Mir", so the licenses are compatible, but **verbatim code reuse requires
retaining the Microsoft copyright and permission notice**; the recommendations in §9–§11 are
reimplementations of contracts, not copies of upstream text.

## 2. How `span` carries size and checks bounds

Source: `include/gsl/span` and `include/gsl/span_ext`.

- **Two size representations in one type.** `span<ElementType, Extent>` has a template parameter
  `Extent` and the sentinel `gsl::dynamic_extent` (defined in `span_ext` as
  `constexpr const std::size_t dynamic_extent = narrow_cast<std::size_t>(-1);`).
  - `details::extent_type<Ext>` stores **no data**; `size()` just returns `Ext`, with
    `constexpr size_type size() const noexcept { return Ext; }`.
  - `details::extent_type<dynamic_extent>` stores a single `size_type size_`.
- **Zero-overhead static extent.** `storage_type<ExtentType>` privately **inherits** `ExtentType`
  (empty-base optimization) and adds only `pointer data_`. The `span_tests.cpp` case `size_optimization`
  pins this: `sizeof(span<int>) == sizeof(int*) + sizeof(ptrdiff_t)` and
  `sizeof(span<int,0>) == sizeof(int*)`.
- **Invariant at construction.** `storage_type(pointer data, OtherExtentType ext)` does
  `Expects(data || ExtentType::size() == 0)` — null + nonzero size is a contract violation.
  `extent_type<Ext>(size_type size)` asserts `size == Ext`; fixed-extent `(ptr,count)` and
  `(first,last)` constructors assert the supplied extent matches; the dynamic extent constructor
  asserts `size != dynamic_extent`. Tests: `from_nullptr_size_constructor` accepts `{nullptr,0}`
  and death-tests `{nullptr,1}` and `span<int,1>{nullptr,0}`.
- **Bounds checks are `Expects` at every access point**, not at the container boundary only:
  `operator[]` → `Expects(idx < size())`; `front()`/`back()` → `Expects(size() > 0)`;
  `first<Count>()`/`last<Count>()` → `Expects(Count <= size())` plus a `static_assert` for known
  extents; `subspan()` → `Expects((size() >= Offset) && (Count == dynamic_extent || Count <= size() - Offset))`.
- **The iterator is itself bounds-aware.** `details::span_iterator` stores `begin_`, `end_`,
  `current_`; `operator*`/`operator->` assert `current_ != end_`; `++`/`+=`/`-=`/`--` assert room in
  the correct direction; comparisons/operator- assert `begin_` and `end_` match the other iterator
  (test `incomparable_iterators` death-tests cross-span comparison). `MSVC` hooks
  (`_Verify_range`, `_Unwrapped`, `_Unwrap_when_unverified = false`, `_Seek_to`) let STL algorithms
  unwrap to a pointer only after a range check.
- **`KnownNotNull`** is a private tag used by array/container constructors and `subspan` to bypass
  the redundant `data != nullptr` check without weakening the public invariant.
- Views of the object representation: `as_bytes(span<T,E>)` and `as_writable_bytes(...)` return a
  `span<const gsl::impl::byte, size-of-object-representation>` / writable variant; `size_bytes()`
  is `size() * sizeof(element_type)`. `gsl::at(span, gsl::index)` (in `span_ext`) checks `i >= 0`
  with `Ensures` and delegates bounds to `span::operator[]`.

## 3. `not_null`: construction and assignment restrictions

Source: `include/gsl/pointers` (the file is named `pointers`, **not** `not_null`).

- Purpose documented in-header: "Restricts a pointer or smart pointer to only hold non-null values.
  Has zero size overhead over T." It works with raw pointers and smart pointers (e.g.
  `not_null<std::shared_ptr<int>>` in the tests).
- `static_assert(details::is_comparable_to_nullptr<T>::value, "T cannot be compared to nullptr.")`
  restricts the underlying type.
- Constructors:
  - `template <typename U, enable_if<is_convertible<U,T>>> constexpr not_null(U&& u)` initializes
    `ptr_` then `Expects(ptr_ != nullptr)`.
  - `template <typename = enable_if<!is_same<nullptr_t,T>>> constexpr not_null(T u)` likewise
    checks; this is why a raw null pointer passed by value terminates.
  - converting `not_null(const not_null<U>&)` delegates to `not_null(other.get())`; copy ctor/assign
    are defaulted.
  - **No default construction** for pointer-like `T` (tests assert
    `!DefaultCtorCompilesFor<void*>`), and **no construction from `nullptr_t`** (tests assert
    `!CtorCompilesFor_A<std::nullptr_t>` and `!CtorCompilesFor_B<void*,0>`).
- Explicitly deleted: `not_null(std::nullptr_t)`, `not_null& operator=(std::nullptr_t)`.
- Pointer-arithmetic/array operators are deleted on the wrapper (`++`, `--`, `+=`, `-=`, `operator[]`,
  free `operator-`, free `operator+` with `ptrdiff_t`) "pointers only point to single objects!"
- `get()` returns `details::value_or_reference_return_t<T>` — `const T` when
  `sizeof(T) <= 2*sizeof(void*)` and trivially copy-constructible, else `const T&`. Provides implicit
  `operator T()`, `operator->`, `operator*`, `swap`, all six comparisons via `std::less`/`greater`,
  `std::hash` specialization, and optional `operator<<`.
- `strict_not_null<T : public not_null<T>` makes the converting/value constructors **`explicit`**
  and copies (not moves) the pointer to preserve the invariant; `make_not_null` /
  `make_strict_not_null` are CTAD-friendly factories.
- Runtime enforcement is only `Expects(ptr_ != nullptr)` → termination (see §6); the *compile-time*
  restrictions are the deleted null constructors/operators. Tests `TestNotNullConstructors`,
  `TestNotNullAssignment`, `TestMakeNotNull`, `TestNotNullCasting` use SFINAE probes plus
  `EXPECT_DEATH` for null raw/null `unique_ptr`/null-returning functions.

## 4. `narrow` / `narrow_cast` overflow contract

Source: `include/gsl/narrow` and `include/gsl/util`.

- `narrow_cast<T>(u)` (in `util`) is deliberately **unchecked**: a searchable spelling of
  `static_cast<T>(std::forward<U>(u))`, `noexcept`, with `-Wfloat-equal` suppressed. Test:
  `narrow_cast<unsigned char>(300) == 44`.
- `narrow<T>(u)` is the checked version. On failure it throws `gsl::narrowing_error`, derived from
  `std::exception`, whose `what()` returns the literal `"narrowing_error"`.
- Arithmetic overload contract (two phases):
  1. **Pre-flight range check** `details::static_cast_is_defined<T,U>(u, is_floating_point_to_integral{})`.
     For floating→integral it builds `upper_bound` by multiplying `numeric_limits<T>::radix`
     `numeric_limits<T>::digits` times, special-cases `bool`, negative values and signedness, and
     rejects infinities/NaN. `narrow` throws if this fails.
  2. **Round-trip check**: `const T t = narrow_cast<T>(u);` then
     `if (static_cast<U>(t) != u || (is_different_signedness && ((t < T{}) != (u < U{})))) throw narrowing_error{};`.
     The comment notes "NaN will always throw, since NaN != NaN". The extra sign check catches
     negative→unsigned wrap that a plain round-trip might miss.
- Non-arithmetic overload (e.g. `std::complex<double>` → `std::complex<float>`): only the
  round-trip `static_cast<U>(t) != u` check, no signedness/range pre-flight.
- `narrow` is `constexpr` and templated, but **throws** — it is not usable in a no-exception,
  `error *`-returning API without redesign.

## 5. `owner` annotation semantics

Source: `include/gsl/pointers` (again, not a separate `owner` header).

- Exact definition:
  `template <class T, std::enable_if_t<std::is_pointer<T>::value, bool> = true> using owner = T;`
  — a **pure type alias with zero runtime effect**. The in-header comment calls it "a safety
  mechanism for code that must deal directly with raw pointers that own memory… a stepping point in
  converting legacy code to use more modern RAII constructs, such as smart pointers."
- It constrains `T` to pointer types only. `tests/owner_tests.cpp` pins this with static asserts:
  `OwnerCompilesFor<int*>` is true; `!OwnerCompilesFor<int>` and
  `!OwnerCompilesFor<std::shared_ptr<int>>` are true. The basic test simply uses
  `owner<int*> p = new int(120); … delete p;`.
- `gsl::shared_ptr` / `gsl::unique_ptr` aliases in the same header are marked
  `GSL_DEPRECATED("Use std::shared_ptr instead")` / `("Use std::unique_ptr instead")`.
- Semantics are **annotation only**: `owner<T*>` is not a class, adds no destructor, no null check
  and no move/copy restriction. All enforcement is by static analysis/tools and by convention.

## 6. `Expects` / `Ensures` assertion policy

Source: `include/gsl/assert`.

- `GSL_CONTRACT_CHECK(type, cond)` expands to
  `(GSL_LIKELY(cond) ? static_cast<void>(0) : gsl::details::terminate())`.
- `Expects(cond)` = `GSL_CONTRACT_CHECK("Precondition", cond)`;
  `Ensures(cond)` = `GSL_CONTRACT_CHECK("Postcondition", cond)`. The `type` string is passed by the
  macro but **not used in the expansion**; there is no message, no throw, no error code.
- `gsl::details::terminate()` calls `std::terminate()` in the normal configuration. In MSVC
  kernel/no-exceptions mode it instead calls a settable `terminate_handler` whose default is
  `__fastfail(RANGE_CHECKS_FAILURE)` — the only configurable path, and it is platform-specific.
- `GSL_SUPPRESS(x)` maps to `[[gsl::suppress(#x)]]` on clang and newer MSVC, otherwise expands to
  nothing; `GSL_ASSUME`, `GSL_LIKELY`, `GSL_UNLIKELY` are also defined here.
- `tests/assertion_tests.cpp` confirms the policy: it installs `std::set_terminate` and uses
  `EXPECT_DEATH` for a violated `Expects` (`f(10)`) and a violated `Ensures` (`g(9)`). There is also
  a `no_exception_ensure_tests.cpp` in the test inventory (not fetched in this study).

## 7. Upstream tests inspected and cases noted

- `tests/span_tests.cpp` (fetched): `constructors`, `constructors_with_extent`, `size_optimization`,
  `from_nullptr_size_constructor`, `from_pointer_length_constructor`, `from_pointer_pointer_construction`,
  `from_array_constructor`, `from_std_array_constructor`, `from_container_constructor`,
  `from_convertible_span_constructor`, `first`, `last`, `subspan`, `iterator_comparisons`,
  `incomparable_iterators`, `begin_end`, `rbegin_rend`, `as_bytes`, `as_writable_bytes`,
  `fixed_size_conversions`, `front_back`, `empty_span`, `comparison_operators`, `std_container_ctad`,
  `std_span`. Notable assertions: size/store layout, null+nonzero death, stale/incomparable iterator
  death, compile-time rejection of fixed→smaller/static extent conversions, `std::span` interop.
- `tests/notnull_tests.cpp` (fetched): `TestNotNullConstructors` (SFINAE ctor probes, null raw /
  `unique_ptr` / returned-pointer death), `TestNotNullCasting` (heterogeneous copy assignment allowed
  for derived→base, unrelated/static-downcast/reinterpret disallowed), `TestNotNullAssignment` (death
  on assigning a null), raw/shared/custom-pointer comparison, dereference, `TestMakeNotNull`,
  `TestStdHash`, CTAD and `std::variant` emplace.
- `tests/owner_tests.cpp` (fetched): basic use plus the three `OwnerCompilesFor` static asserts.
- `tests/assertion_tests.cpp` (fetched): `expects` and `ensures` terminate death tests.
- `tests/utils_tests.cpp` (fetched): `narrow_cast`, `static_cast_is_defined`, `narrow_exact_signed_minimum`,
  `narrow` — integer wrap, float→int bounds/NaN/inf, `bool(2.0)`, `unsigned char(256/-0.5/-1.0)`,
  `std::complex` precision, `ldexp`-derived int32/int64 boundaries.
- Not inspected: `span_compatibility_tests.cpp` (61 KB), `span_ext_tests.cpp`, `strict_notnull_tests.cpp`,
  `constexpr_notnull_tests.cpp`, `pointers_tests.cpp`, `no_exception_ensure_tests.cpp`, `at_tests.cpp`,
  `dyn_array_tests.cpp`. `narrow_tests.cpp` was attempted at two paths and does not exist on `main`.

## 8. Mapping to this repository's C+ standard library, with current gaps

**Note on paths:** the task referred to `docs/stdlib/agent2_audit/module_audits/`; in this checkout the
audit actually lives at `tests/stdlib/agent2_audit/module_audits/` (plus `known_gaps.md`,
`baseline.md`, `public_api_inventory.md` in its parent). All citations below use the real path.

C+ surface facts used here: `docs/MODERN_STDLIB.md:36` ("C+ has no overloading or static methods"),
`:42-49` (owned / borrowed / invalid / moved-from states; `borrowed` explicitly includes
`file_borrow`, `string_view`, `json_view`, `guard`, `native()`), `:53-56` (destructor inert on zeroed
handle); `docs/LANGUAGE.md:90` ("Borrowing is just a plain pointer… not tracked"), `:118-122`
("no lifetime annotations, no aliasing/exclusivity rules, and no field-level or container-level
tracking"); `docs/LIMITATIONS.md:48-52` (no inheritance/virtuals, no full type inference, no leak
detection for borrowed pointers, no full C++-style ownership).

| GSL idea | C+ analogue | Current gap (cited) |
|---|---|---|
| `span<T>` (ptr+count, checked `operator[]`) | `vector::slice` / `string_view` | C+ already goes **further** in one respect: `vector::slice` is generation-tagged and refuses stale/OOR via `at_checked()` (`vector.hp.md:120-129`). But "a slice outliving its owner is explicitly declared undetectable" (`vector.hp.md:130-134`), and the generation ref is a pointer into the owner (`vector.hp.md:243-246`). |
| `span` fixed extent / EBO | C+ fixed arrays have compile-time `len` (`docs/LANGUAGE.md:255-286`) | No generic `span<T,N>`; extent is only anchored to declaration-site arrays, not to borrowed buffers generally. |
| `not_null<T>` | `valid()` / `is_open()` states, null guards passed to `stdlib_note` | No typed non-null wrapper; `file_borrow(NULL)` is accepted without validation (`file.hp.md:234-235`); `string_view::equals(NULL)` is silent (`string.hp.md:156`); stale/dangling key after `strmap::put` is **ACCEPT / untagged compile-time gap** (`collections.hp.md:140,251-253,282-284`). |
| `owner<T*>` (annotation only) | `resource struct` + `drop` (owned), `file_borrow`/`native()` (borrowed) | C+ is stronger than GSL here: ownership is a class with an inserted destructor and copy rejection (`file.hp.md:108-121`; `docs/LANGUAGE.md:69-87`). The remaining gap is field/container-level tracking (`docs/LIMITATIONS.md:52`) and untagged use-after-move (`AGENT1_COMPILER_GAPS.md:58-61`). |
| `narrow` / `narrow_cast` | `error *` out-param + `0`/`-1` returns; no exceptions | There is no searchable narrowing helper. Signed↔unsigned and float↔int conversions are silent or UBSan-only ("Signed integer overflow… run time"; "Signed-to-unsigned comparison… warning only" — `docs/LIMITATIONS.md:66,77`). |
| `Expects`/`Ensures` terminate | `stdlib_note(category, code, message)` + sentinel; fatal probes only for UB-class errors (`cplus_check_live`) | Violations are recoverable in C+, so GSL's terminate policy cannot be transplanted; see §10. |
| Checked iterator that refuses comparison across spans | `slice::sub` / `at_checked` refusal | Concurrency caveat: slice refusal is not thread-safe and "a slice outliving its owner is explicitly declared undetectable" (`vector.hp.md:21-26,130-134`). |

Concrete gap summary for the requested classes:

- **`string_view`**: stores only `(const char *chars, size_t count)`, owns nothing, has no `drop()`;
  "valid while its owner lives", and a reallocating `reserve`/`append`/`drop` invalidates the pointer.
  "Unlike `vector::slice`, it has **no generation tracking and no library-enforced refusal** —
  detecting staleness is the caller's job" (`string.hp.md:127-136`). This is the single clearest
  place to borrow a GSL/C+ hybrid technique (checked access + refuse-on-stale).
- **`vector::slice`**: already records `generation_ref = &this->generation` and `generation_at`;
  `stale()` compares, `sub()` propagates the same reference, and `at_checked()` returns `-1` without
  reading the stale pointer (`vector.hp.md:120-129`). Keep this design; do **not** replace it with
  a GSL-style unchecked `span`.
- **`file_borrow`**: `owns` is set only when `fopen`/`tmpfile` succeeded; `close()` on a borrowed
  stream detaches without `fclose`; `drop()` is idempotent (`file.hp.md:108-116`). Gap: the factory
  accepts any `FILE*` including `NULL` without validation (`file.hp.md:234-235`).
- **collections borrowed keys**: `strmap` deliberately **copies** keys, so caller mutation/drop of
  the source `string` cannot dangle a stored key (`collections.hp.md:186-191`; cross-family CF1).
  But a caller freeing a key buffer after `put` is an un-tagged ACCEPT gap
  (`collections.hp.md:140,251-253`) — a GSL-style non-null/lifetime annotation would only help if the
  compiler enforced it (Agent 1, §9).

## 9. Feasible in C+ now vs. requiring Agent 1

C+ here is a C-shaped language: classes lower to struct + functions, methods are `this->`-only, no
overloading, no static methods, no C++ templates/concepts/SFINAE/deduction guides
(`docs/MODERN_STDLIB.md:34-38`; `docs/LIMITATIONS.md:39,48-52`). Therefore no generic `gsl::span<T,E>`,
`gsl::not_null<T>` or `gsl::narrow<T,U>` can be reproduced as library code.

**Feasible now (header-level, per-type, no templates):**

1. Keep and extend the **generation-tagged borrow + refuse-on-stale** discipline. `vector::slice`
   already implements it; adding equivalent fields/refusal to `string_view` (with explicit
   documented struct-field additions) would close its "no library-enforced refusal" gap.
2. Add **per-type checked conversion functions** (e.g. `narrow_i32_to_u32(int value, unsigned *out)`)
   that implement GSL's two-phase pre-flight + round-trip predicate and return `STDLIB_FAIL` /
   `stdlib_note` instead of throwing. Limit to the integer/float pairs actually used by the stdlib.
3. Apply the **invariant-at-construction + check-at-every-access** rule in `.hp` accessors: validate
   `(ptr != NULL) || (count == 0)` in constructors/factories, keep `owns`/`valid()` flags, and have
   every indexed accessor return `0`/`-1` on failure. Continue the existing `at_checked` pattern.
4. Adopt GSL's **API-shape discipline** where C+ allows it: do not expose default-constructed
   non-null handles; reject null in `file_borrow`/`view` factories and note the failure; keep no
   arithmetic-on-handle idioms by convention (C+ cannot delete operators).
5. Use narrowly scoped, documented suppressions only where a check is provably redundant — in C+
   this means comments plus `cplus_trusted`-style annotations, **not** `[[gsl::suppress]]` attributes.

**Requires Agent 1 (compiler/runtime):**

1. **Generic typed views/non-null wrappers** — any real `span<T>`/`not_null<T>` needs templates or
   a generics feature C+ does not have.
2. **Tagged lifetime/alias diagnostics** for dangling views and dangling map keys. Current state:
   `[CPLUS-LIFETIME]`, `[CPLUS-ALIAS]`, `[CPLUS-BORROW]` are proposed but capability #1 and #3 are
   rated **Critical** because cases are silently accepted (`docs/libc/AGENT1_COMPILER_GAPS.md:58-61`);
   collections dangling key is an ACCEPT gap (`collections.hp.md:140,251-253`).
3. **Enforced `_Owner` / `_View` annotations** and discard-owner warnings — today these are
   documentation conventions with a standing request to Agent 1 (`docs/MODERN_STDLIB.md:148-154,164-167`).
4. **Class-field lifetime tracking (`drop()` / `close()`) and container-aware borrows** — explicitly
   not implemented in the model, which covers locals only (`docs/LIMITATIONS.md:52`;
   `AGENT1_COMPILER_GAPS.md:58`).
5. **Configurable contract-check policy** if we ever want `Expects`-like annotations that emit a
   tagged diagnostic instead of terminating; today only runtime `stdlib_note` + death-probe patterns exist.

## 10. What must not be copied

- **Terminate-on-contract-violation.** `Expects`/`Ensures` call `std::terminate()` unconditionally;
  C+ reports recoverable failures through `error *` + `0`/`-1` and reserves aborts for UB-class
  probes. Copying this would break the documented failure-atomicity model
  (`docs/MODERN_STDLIB.md:35`; `string.hp.md:158-163`; `assert` source §6).
- **Exceptions.** `gsl::narrowing_error : std::exception` and `throw narrowing_error{}` have no C+
  equivalent; reimplement as a predicate + error return.
- **C++ machinery**: templates, concepts, SFINAE/`enable_if`, `decltype(auto)`, deduction guides,
  `constexpr` functions, `[[nodiscard]]`/`[[gsl::suppress]]` attributes, `std::hash` specializations,
  iostreams `operator<<`, `std::span`/`std::ranges` interop, `reinterpret_cast` to a `byte` type.
  These are unavailable in the C+ compiler surface (`docs/MODERN_STDLIB.md:36-38`;
  `docs/LIMITATIONS.md:48-52`) and strict-aliasing is only UBSan-caught (`docs/LIMITATIONS.md:69`).
- **`gsl::owner<T*>` as a safety guarantee.** It is only a constrained typedef with no destructor or
  checker; C+ resource classes are strictly stronger. Do not downgrade `resource struct`/`drop` to an
  alias, and do not reintroduce deprecated `gsl::shared_ptr`/`gsl::unique_ptr` aliases.
- **`strict_not_null`, deleted operators, `final_action`, `gsl::index`, comparison operators** in
  generic form — C+ has no operator overloading/generic callables. Take only the *idea* of making
  invalid states unrepresentable through the existing `valid()`/`owns` state model.
- **Verbatim source text** without the MIT attribution quoted in §1.

## 11. Top 3 reusable techniques

1. **Invariant at the storage constructor, enforcement at each accessor/iterator step, with a
   zero-overhead static form.** GSL's `span` checks `(data || size == 0)` once in `storage_type`,
   then `Expects` in `operator[]`, `front/back`, `first/last/subspan` and in the three-pointer
   iterator; fixed extents cost one pointer via empty-base inheritance
   (`span` source §2; `span_tests.cpp:size_optimization`). This maps cleanly onto C+ `at_checked`
   and constructor validation.
2. **Two-phase checked narrowing: pre-flight representability test + round-trip equality with an
   explicit sign check.** GSL `narrow` first rejects out-of-range/NaN/inf via `static_cast_is_defined`,
   then verifies `static_cast<U>(T(u)) == u` plus signedness agreement (`narrow` source §4). Extract
   this as a pure predicate and return `error` in C+ — no exceptions, no templates needed per type.
3. **Make invalid states unrepresentable through API shape.** GSL `not_null` deletes null
   construction/assignment and pointer arithmetic and adds `strict_not_null` with explicit-only
   constructors; `owner<T*>` is restricted to pointer types (`pointers` source §3/§5;
   `owner_tests.cpp` static asserts). C+ analogue: never default-construct a non-nullable handle,
   validate borrowed factories at entry, keep arithmetic off handles, and rely on `resource struct`
   + `drop` rather than an annotation-only alias.

## 12. Failures and study limitations

- 404s: `include/gsl/not_null`, `include/gsl/owner`, `tests/narrow_tests.cpp` (both `/tests/` and
  `/tests/gsl/`). Correct locations were found via the contents API (`include/gsl/pointers`,
  `tests/utils_tests.cpp`).
- No pinned tag/commit was explicitly requested; URLs use `main`. The API response gives blob SHAs
  (e.g. `span` = `090a8bd…`); a future study should pin a release tag to make line references stable.
- Several large test files were not inspected (list in §7); no claim is made about their contents.
- This is a source-reading study only: nothing was compiled, and no live C+ or GSL behavior was
  executed. All "VERIFIED" labels in §8 are inherited from the local audit documents, not re-run here.
