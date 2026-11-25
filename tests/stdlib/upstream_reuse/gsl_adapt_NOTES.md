# GSL upstream reuse — `narrow` / `not_null` / `span` port

Owner: subagent 1 of 10, branch `agent/modern-stdlib`.

**Strict file ownership: only these files were created or modified by this agent:**

- `stdlib/gsl_adapt.hp`
- `tests/stdlib/upstream_reuse/gsl_adapt.cp`
- `tests/stdlib/upstream_reuse/gsl_adapt_NOTES.md` (this file)

No compiler, runtime, build script, other header, other test, or fixture was
edited. No `git add` / `git commit` was run. (Sibling files in
`tests/stdlib/upstream_reuse/` belong to other parallel agents.)

## Environment

```
cwd        = /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
date       = 2026-11-23
compiler   = ./build/c+            (in-tree, repo-built, C+ 0.1.0)
backend    = Apple clang version 16.0.0 (clang-1600.0.26.6), arm64-apple-darwin
timeout    = /usr/bin/timeout       (all build/run commands wrapped: -k 5 30)
```

## Exact upstream sources fetched (branch `main`, 2026-11-23)

| component | exact URL | commit visible on GitHub history page |
|---|---|---|
| `span` | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/span` | `417ef68` (2026-08-25, "Prevent external construction of checked iterators (#1263)") |
| `not_null` / `strict_not_null` | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/pointers` | `a75212b` (2026-07-13, "feat: implementation of gsl::dyn_array (#1228)") |
| `narrow` / `narrowing_error` | `https://raw.githubusercontent.com/microsoft/GSL/main/include/gsl/narrow` | `08687fb` (2026-08-25, "Fix floating-to-integral narrow undefined behavior (#1266)") |
| license | `https://raw.githubusercontent.com/microsoft/GSL/main/LICENSE` | MIT, Copyright (c) 2015 Microsoft Corporation |

The raw URLs do not embed a commit hash in their content, so the SHAs above are
the latest per-file touches visible on the corresponding
`github.com/microsoft/GSL/commits/main/include/gsl/<file>` pages at fetch time.
The GitHub API commit endpoint returned HTTP 403 (rate limit), hence the HTML
history pages were used. The MIT text is reproduced verbatim in the header
comment of `stdlib/gsl_adapt.hp`.

## Exact upstream lines that were ported

`include/gsl/narrow` — the three-stage representability check, copied verbatim
from the fetched `main` source:

```cpp
    constexpr const bool is_different_signedness =
        (std::is_signed<T>::value != std::is_signed<U>::value);
    ...
    if (!details::static_cast_is_defined<T, U>(u, is_floating_point_to_integral{}))
    {
        throw narrowing_error{};
    }

    GSL_SUPPRESS(es.103) // don't overflow
    GSL_SUPPRESS(es.104) // don't underflow
    const T t = narrow_cast<T>(u);
    ...
    // Note: NaN will always throw, since NaN != NaN
    if (static_cast<U>(t) != u || (is_different_signedness && ((t < T{}) != (u < U{}))))
    {
        throw narrowing_error{};
    }
```

The non-arithmetic overload is the same idea without the sign check:

```cpp
    const T t = narrow_cast<T>(u);

    if (static_cast<U>(t) != u) { throw narrowing_error{}; }
```

`include/gsl/pointers` — `not_null`'s null refusal and deleted null assignment:

```cpp
        Expects(ptr_ != nullptr);
    ...
    // prevents compilation when someone attempts to assign a null pointer constant
    not_null(std::nullptr_t) = delete;
    not_null& operator=(std::nullptr_t) = delete;
    ...
    // unwanted operators...pointers only point to single objects!
    not_null& operator++() = delete;
    not_null& operator--() = delete;
    not_null operator++(int) = delete;
    not_null operator--(int) = delete;
    not_null& operator+=(std::ptrdiff_t) = delete;
    not_null& operator-=(std::ptrdiff_t) = delete;
    void operator[](std::ptrdiff_t) const = delete;
```

`include/gsl/span` — explicit storage and the exact `Expects` guards that became
refusals in C+:

```cpp
        constexpr reference operator[](size_type idx) const noexcept
        {
            Expects(idx < size());
            return data()[idx];
        }
    ...
        constexpr span<element_type, dynamic_extent> first(size_type count) const noexcept
        {
            Expects(count <= size());
            return {data(), count};
        }
    ...
        Expects((size() >= Offset) && (Count == dynamic_extent || (Count <= size() - Offset)));
    ...
        constexpr storage_type(pointer data, OtherExtentType ext) : ExtentType(ext), data_(data)
        {
            Expects(data || ExtentType::size() == 0);
        }
```

## Port mapping

| upstream | C+ additive name | adaptation |
|---|---|---|
| `gsl::narrow<T>(U)` (arithmetic) | `gsl_narrow_i64_i8/u8/i16/u16/i32/u32/i64/u64`, `gsl_narrow_u64_i64/u8/u32` | preflight → cast → round-trip equality → sign check, all four retained; failure is `STDLIB_FAIL` + `STDLIB_CATEGORY_RANGE` instead of a C++ throw |
| `gsl::narrowing_error` | `gsl_adapt_refuse_range()` | `error *err` out-param, same fail-closed convention as `stdlib/memory.hp` |
| `gsl::not_null<T>` | `gsl_not_null_i32`, `gsl_not_null_void` | single pointer member (zero overhead); guarded `*_make()` replaces the deleted null constructor; `*_get()` re-asserts the invariant; no `++`, `--`, `+=`, `[]` |
| `gsl::span<T>` (base + extent) | `gsl_span_i32 {int *base; size_t count; void *owner; size_t off_elems;}` | explicit `(base, count)`; two extra provenance fields exist only so strict-mode range checks can be satisfied; `at()` / `first()` / `last()` / `subspan()` refuse with `STDLIB_CATEGORY_BOUNDS` and never dereference on refusal |

### What was deliberately NOT changed

- Ordinary C pointer representation is untouched: `sizeof(int *) == sizeof(void *)`
  and `sizeof(gsl_not_null_i32) == sizeof(int *)` are asserted by the suite.
- No existing ABI, no existing header behaviour, and no runtime/compiler source
  was modified. The header only adds new `gsl_*` names and includes
  `stdlib/memory.hp` so the span check uses the same
  `stdlib_mem_check_range()` path as the rest of the stdlib.
- Floating-point narrow sources are out of scope for the typed entry points
  (documented in the header); the upstream float→integral preflight is quoted
  above but not re-implemented with host FP limits.

## Verification (all commands wrapped in `timeout -k 5 30`)

### 1. Strict type-check

```
$ ./build/c+ check --strict --sanitize=none tests/stdlib/upstream_reuse/gsl_adapt.cp
check: tests/stdlib/upstream_reuse/gsl_adapt.cp ok
CHECK_RC=0
```

### 2. Strict, no sanitizer

```
$ ./build/c+ build --strict --sanitize=none tests/stdlib/upstream_reuse/gsl_adapt.cp -o <tmp>/gsl_adapt_strict_none
BUILD_RC=0                      (build log: 0 bytes)
$ <tmp>/gsl_adapt_strict_none
RUN_RC=0                        (stderr: 0 bytes)
last line: GSL_ADAPT 80 0
```

### 3. Strict + UBSan

The first UBSan attempt failed **outside the owned files**, in the shared
runtime rebuild:

```
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:130:23: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/runtime/cplus_runtime.c:137:21: error: use of undeclared identifier 'CPLUS_EXIT_ARITHMETIC'
error: failed to compile instrumented C+ runtime
```

Cause (diagnosed, not edited): `c+` compiles the external runtime with
`-I <this-repo>/runtime/include`, whose `cplus/cplus.h` stops at
`CPLUS_EXIT_IO = 109`; the newer external runtime source references
`CPLUS_EXIT_ARITHMETIC = 110` defined only in the *core* header. This is a
pre-existing header/runtime version skew, independent of this port.

Non-invasive bridge (a compiler flag only — no file was changed):

```
$ ./build/c+ build --strict --sanitize=undefined tests/stdlib/upstream_reuse/gsl_adapt.cp -o <tmp>/gsl_adapt_ubsan3 -- -DCPLUS_EXIT_ARITHMETIC=110
BUILD_RC=0
$ <tmp>/gsl_adapt_ubsan3
RUN_RC=0                        (stderr: 0 bytes)
last line: GSL_ADAPT 80 0
```

Instrumentation confirmed while the binary was present:

```
$ nm -u <tmp>/gsl_adapt_ubsan3 | grep -i ubsan | head -6
___ubsan_handle_add_overflow_abort
___ubsan_handle_builtin_unreachable
___ubsan_handle_divrem_overflow_abort
___ubsan_handle_mul_overflow_abort
___ubsan_handle_negate_overflow_abort
___ubsan_handle_out_of_bounds_abort
```

The `--sanitize=none` binary has no `ubsan` undefined symbols.

### 4. Parity

```
$ cmp <tmp>/gsl_none_run.out <tmp>/gsl_ubsan3_run.out
CMP_RC=0
```

Both stdout captures are byte-identical: 81 lines (80 `<tag> <0|1>` checks plus
the `GSL_ADAPT 80 0` summary), 80 lines ending in ` 1`, 0 lines ending in ` 0`.

**Totals: 1 strict check, 2 build+run invocations passed, 80 checks, 0 failures,
0 timeouts, clean stderr in both modes.**

## Report contract

`gsl_adapt.cp` prints one `<tag> <0|1>` per check and then
`GSL_ADAPT <checks> <failures>`; it exits 0 iff failures == 0. Sections:

- `ga_n_*` — narrow accepts representable boundaries (13 checks)
- `ga_r_*` — narrow refusals: preflight, round-trip/sign, NULL out, no write on refusal (15 checks)
- `ga_nn_*` — not_null construction/get/swap/void variant/zero overhead (10 checks)
- `ga_sp_*` and `ga_abi_*` — span make/size/data, at() refusal, first/last/subspan, nested subview provenance, ABI (42 checks = 41 + 1)
