# ISO C API gap closure plan — exact, evidence-based classification of the 27 MISSING and 49 UNSUPPORTED APIs

**Date:** 2026-11-23
**Branch:** `agent/modern-stdlib`
**Role:** planning/evidence deliverable only. This file changes no source, header, contract, CMake, test, or CI file. It closes no API and runs no test runner.
**File ownership:** this document only. No other file was created, modified, staged, or committed by this task.
**Authoritative gap set:** `docs/libc/iso_c_inventory.json` → `.headers[].apis[]` records with `classification == "MISSING"` (27) or `"UNSUPPORTED"` (49), recomputed from the per-API records (not from the stale per-header `by_classification` metadata — see §7.1).

## 0. Sources (all read, none modified)

| Short name | Path | What it establishes |
|---|---|---|
| INV | `docs/libc/iso_c_inventory.json` | The exact 27 + 49 names, per-API `limitation`, and host-probe notes (`:16`). |
| MATHCLOSURE | `docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md` | 23 math blockers (§3, `:117-147`), 4 adjacent C23 blockers (§4, `:151-161`), pins (§1, `:17-33`), unblock conditions, proof commands (§5, `:165-243`), no-fabrication rule (§6, `:247-266`). |
| PLAN | `docs/libc/IMPLEMENTATION_PLAN.md` | Owner labels (§0.3, `:29-36`), 27/49 enumeration (§1.2, `:67-83`), blocked-item conditions (§6.1-6.3, `:288-311`), recorded test commands. |
| PROVENANCE | `docs/libc/DEPENDENCY_PROVENANCE.md` | 11 vendored/integrated dependencies (§1, `:15-33`); LLVM `memset_explicit` is the only verbatim upstream copy (row 7, `:27`); C23 closure gap (`:107`). |
| SAFECRT | `docs/libc/SAFECRT_INTEGRATION_PLAN.md` | Pinned safeclib `NOT_INTEGRATED` / `ADAPTER_ONLY` sources, compile probes, dependency closures and batches B1/B4/B5 (`:176-247`). |
| SAFECRTINV | `docs/libc/safeclib_inventory.json` | Per-API `implementation_source`, `source_compile_probe`, `dependency_closure`, `classification` for the 437 vendored safeclib names. |
| MATRIX | `docs/libc/api_status_matrix.json` | `top_gaps` ranks 3, 8, 9 (placeholder `_s` contracts; Annex K sort/search; stdatomic ABI) and its `claim`. |
| COVERAGE | `docs/libc/COVERAGE.md` | Explicit not-shipped list (`:70-83`), platform-backed `_s` adapters (`:66`). |
| TREE | `third_party/` (9 dirs) + on-disk vendored sources | Confirms which pinned implementations physically exist (notably `third_party/safeclib/src/{misc,io,str}`). |

**No fabrication rule (inherited from MATHCLOSURE §6 and PLAN §6):** no body, constant, lookup table, rounding value, signature or algorithm is defined here. Where a source does not exist at a pin, this document says so; it does not propose deriving one. "Could be closed now" means *an owner with write access can integrate an existing pinned artifact and pass the named proof gates* — not that the API is implemented today.

## 1. Classification legend (exact meanings used below)

| Class | Operational definition, grounded in the sources |
|---|---|
| `BLOCKED_NO_SOURCE` | No pinned, conforming upstream source exists at any revision recorded in this repo (404 / no file / no host declaration). No amount of build tooling helps; the artifact must first exist. |
| `BLOCKED_HOST_SYMBOL` | The host CRT/toolchain does not supply the declaration, symbol, macro or capability, and no conforming local substitute is shipped. Publishing a value here would be fabrication. |
| `BLOCKED_TOOLCHAIN` | A source or capability exists, but the current C99/C11-only build model cannot consume it: C++20 `__support` closure, or a type-generic `_Generic` API that cannot be one C99 C ABI symbol. |
| `IMPLEMENTABLE_FROM_PINNED_SOURCE` | A conforming pinned source is already on disk, compiles in the recorded probe, and needs only integration (build-script entry, prefixed symbol, wrapper/declaration, contract, tests) plus an owner semantics/policy decision. |
| `ALREADY_HOST_PROVIDED_NEEDS_WRAPPER` | The host already exports the runtime symbol; there is no source or toolchain blocker. Only a namespacing/deprecation policy decision remains. |

**Tie-break rule (applied consistently):** when an API has both no pinned source and no host symbol, `BLOCKED_NO_SOURCE` is used if no host primitive can legitimately be delegated to (e.g. `timespec_getres`, UTF-8 conversions); `BLOCKED_HOST_SYMBOL` is used where the recorded reason is an absent host capability/macro and no conforming local source exists (e.g. `FE_TIESAWAY`, Annex K `fclose_s`/`fread_s`). A pinned source that exists but cannot be built under this repository's rules is `BLOCKED_TOOLCHAIN`, never `BLOCKED_NO_SOURCE`.

## 2. Counts by classification

| Class | MISSING (27) | UNSUPPORTED (49) | Total (76) |
|---|---:|---:|---:|
| `BLOCKED_NO_SOURCE` | 9 | 0 | **9** |
| `BLOCKED_HOST_SYMBOL` | 1 | 2 | **3** |
| `BLOCKED_TOOLCHAIN` | 17 | 34 | **51** |
| `IMPLEMENTABLE_FROM_PINNED_SOURCE` | 0 | 11 | **11** |
| `ALREADY_HOST_PROVIDED_NEEDS_WRAPPER` | 0 | 2 | **2** |
| **Total** | **27** | **49** | **76** |

By family: `math.h` 23 MISSING; `time.h` 1, `uchar.h` 2, `fenv.h` 1 MISSING; `stdatomic.h` 34, `stdio.h` 13, `stdlib.h` 2 UNSUPPORTED. These are per-API derivations; INV's `math.h` `by_classification` block is wrong (§7.1).

## 3. Unblock routes and exact commands / conditions (referenced per row)

Routes are the *only* sanctioned unblock mechanisms; they are transcribed from the named sources.

- **U1 — new toolchain builtin.** Condition: a toolchain exposing the relevant `__builtin_*` for the API, so the existing builtin-gated skeleton in `libc/src/math/math_c23.c` can call it. Exact host state: Apple Clang 16 exposes **none** of these builtins (`libc/src/math/math_c23.c:12-28,117-121,179-184,296-302,315-319`). No command exists until such a toolchain is adopted; proof is P-MATH.
- **U2 — approved C++20 translation unit + vendored closure.** Condition (PLAN §6.1(b); MATHCLOSURE §2): explicit orchestrator decision to vendor the LLVM libc `src/__support/{FPUtil,CPP,macros,common.h}` stack plus `libc/include/hdr/*` and `llvm-libc-macros/*` at commit `424c2d9b7e4de40d0804dd374721e6411c27d1d1` (`llvmorg-20.1.1`), build as C++20 with LLVM namespace/config macros, and add a C ABI shim with the **correct C23 signature**. Exact compile/probe commands (MATHCLOSURE §5.1):

  ```sh
  /usr/bin/clang++ -std=c++20 -Wall -Wextra \
    -I<vendored-llvm-libc-root> -Ilibc -Ilibc/install-include \
    -c <new_cpp_implementation>.cpp -o /tmp/<new_cpp_implementation>.o
  nm -g /tmp/<new_cpp_implementation>.o | grep cplus_faddl
  ```

  The `nm` check must show `long double faddl(long double, long double)` semantics, not `float`. Copying a wrapper alone is insufficient; the `__support` closure is the unit of work.
- **U3 — different/new pinned source.** Condition: a pinned upstream revision (pin + SHA + license recorded in a repo document before import) that actually ships a self-contained C source with the C23 name **and** the C23 signature. This is the route for every HTTP-404 file; "vendoring a neighbouring header does not create an entry point" (MATHCLOSURE §3 rows 8/11/14/17/19/21).
- **U4 — host/toolchain capability appears.** Condition: the host `<fenv.h>` defines `FE_TIESAWAY` (then `CPLUS_C23_UPSTREAM_FE_TIESAWAY` becomes a live direct alias), or a toolchain exposes the relevant builtin/`<uchar.h>` declarations. Choosing a numeric value locally is forbidden.
- **U5 — explicit orchestrator authorisation to implement against a host primitive.** Condition: a named decision overriding the "no new algorithm" rule (e.g. authorising `timespec_getres` against a host `clock_getres`-style primitive). Until that decision exists, no body may be emitted.
- **U6 — safeclib pinned-source integration (B1).** Exact condition (SAFECRT §5, B1; SAFECRTINV probes): add the pinned `.c` path(s) to the source loop in `scripts/build_safecrt.sh:102-121`, add the `-D` symbol prefixes to `SC_DEFS` (`:32`), add `cplus_sc_*` wrappers in `stdlib/safe/src/safeclib_ext_wrappers.c` plus declarations, a contract entry, and tests. No config change for this batch. Build/test:

  ```sh
  sh scripts/build_safecrt.sh
  sh tests/crt/unit/run_unit_tests.sh
  sh tests/libc/run_libc_tests.sh
  ```

- **U7 — safeclib source integration with wrapper-semantics decision (B4/B5).** Same mechanical steps as U6, but the owner must first decide whether to replace the existing non-ISO `cplus_*_s` adapter semantics (SAFECRT §3.3: "Replacing them with upstream sources is a *wrapper-semantics decision*, not new coverage"; §5 B4 adds `vsnprintf_s.c`, `wcsnlen_s.c`, `wcstombs_s.c` in the same archive). No new algorithm is authored.
- **U8 — explicit product/policy decision.** Condition: a recorded decision to ship (or keep excluded) an insecure/removed ISO name or an Annex K ABI. No implementation is proposed by this document.
- **U9 — document-the-host pattern (stdatomic).** Exact condition (MATRIX `top_gaps` rank 9; PLAN §6.3): do **not** invent `cplus_atomic_*` linkable symbols for `_Generic` macros; add docs demonstrating direct use of host `<stdatomic.h>` in C+ code, a compile-only consumer test `tests/compat/atomic_tgmath.c`, and explicit UNSUPPORTED rows in installed SDK docs. Compiler Agent 1 is involved only if type-generic dispatch is ever wanted.

Proof gates (all currently PASS **with SKIPs**; a SKIP/non-zero `skipped` count means the API is still not closed):

- **P-MATH** (MATHCLOSURE §5.1): `clang -std=c99 ... libc/src/math/math_c23_upstream.c tests/libc/test_math_c23_upstream.c build/libc/libcplus_hardened.a -lm -pthread -o /tmp/mcu_proof`; then `! grep -q 'math_c23_upstream: SKIP' /tmp/mcu_proof.log` and `grep -q 'APIs tested=23, skipped=0' /tmp/mcu_proof.log`. Full regression: `cmake -S . -B build-macos-verify ... && cmake --build ... && ctest --test-dir build-macos-verify --output-on-failure && python3 tools/format_cpp.py --check`.
- **P-SMALL** (MATHCLOSURE §5.2): build with `-isysroot "$SYS" -std=c99 ... libc/src/c23_upstream_small.c ...`, then `! grep -q '^SKIP timespec_getres:'`, `! grep -q '^SKIP FE_TIESAWAY:'`, `grep -q '^c23_upstream_small: ok'`.
- **P-UCHAR** (MATHCLOSURE §5.3): build `libc/src/uchar/uchar_c23.c ... tests/libc/test_uchar_c23.c`, then `! grep -q '^SKIP mbrtoc8/c8rtomb:'`, `grep -q '^uchar_c23: ok'`.
- **P-SAFECRT**: `sh scripts/build_safecrt.sh` + `sh tests/crt/unit/run_unit_tests.sh` + `sh tests/libc/run_libc_tests.sh`; new ISO-signature tests and contract entries must exist (PLAN §1.4, MATRIX rank 3).

## 4. Per-API enumeration (all 76, exact names)

### 4.1 The 23 `math.h` APIs classified MISSING

Every row's upstream paths are pinned at commit `424c2d9…` (MATHCLOSURE §1). "C++ bundle" tags are defined in MATHCLOSURE §3 (`MANIP`, `ADD`, `MUL`, `DIV`, `HYPOT`, `FMA`, `EXP16`). Host has the `_Float16` type (`__FLT16_MANT_DIG__ = 11`); type absence is **not** a blocker (MATHCLOSURE §2 correction, §7.3).

| # | Exact API name | Class | Evidence (MATHCLOSURE §3 row unless noted) | Exact unblock condition | Route / proof |
|---:|---|---|---|---|---|
| 1 | `nextup` | `BLOCKED_TOOLCHAIN` | row 1: `generic/nextup.cpp` + **MANIP**; `fputil::nextupdown<false>`, `FPBits::uintval/min_subnormal/max_normal` have no `cplus_*` equivalent | Vendor **MANIP** and add a C++20 TU + C ABI shim with the C23 signature | U2; P-MATH |
| 2 | `nextupf` | `BLOCKED_TOOLCHAIN` | row 2: same template, `FPBits<float>` | Same as `nextup` (or `__builtin_nextupf`) | U2 / U1; P-MATH |
| 3 | `nextupl` | `BLOCKED_TOOLCHAIN` | row 3: same template, `FPBits<long double>` | Same as `nextup` (or `__builtin_nextupl`) | U2 / U1; P-MATH |
| 4 | `nextdown` | `BLOCKED_TOOLCHAIN` | row 4: `generic/nextdown.cpp` + **MANIP**, `nextupdown<true>` | Vendor **MANIP** + C++20 shim (or `__builtin_nextdown`) | U2 / U1; P-MATH |
| 5 | `nextdownf` | `BLOCKED_TOOLCHAIN` | row 5: same closure, `FPBits<float>` | Same as `nextdown` (or `__builtin_nextdownf`) | U2 / U1; P-MATH |
| 6 | `nextdownl` | `BLOCKED_TOOLCHAIN` | row 6: same closure, `FPBits<long double>` | Same as `nextdown` (or `__builtin_nextdownl`) | U2 / U1; P-MATH |
| 7 | `fadd` | `BLOCKED_TOOLCHAIN` | row 7: `generic/fadd.cpp` + **ADD**; upstream is `float fadd(double,double)`, C23 is `double fadd(double,double)` (contract mismatch) | Vendor **ADD** + C++20 shim **and** adapt result type to `double`, with rounding-mode tests | U2; P-MATH + `nm` |
| 8 | `faddf` | `BLOCKED_NO_SOURCE` | row 8: `generic/faddf.cpp` — **HTTP 404 at pin**; **ADD** would be needed | A different pinned upstream with a self-contained C `faddf`, or newer toolchain with `__builtin_faddf`. Vendoring `add_sub.h` alone does not create an entry point | U3 / U1; P-MATH |
| 9 | `faddl` | `BLOCKED_TOOLCHAIN` | row 9: `generic/faddl.cpp` + **ADD**; upstream `float faddl(long double,long double)` vs C23 `long double faddl(...)` | Vendor **ADD** + C++20 shim **and** adapt result type to `long double` | U2; P-MATH + `nm` |
| 10 | `fsub` | `BLOCKED_TOOLCHAIN` | row 10: `generic/fsub.cpp` + **ADD**; upstream returns `float` from `double` args | Same as `fadd` (adapt to `double`) | U2; P-MATH + `nm` |
| 11 | `fsubf` | `BLOCKED_NO_SOURCE` | row 11: `generic/fsubf.cpp` — **HTTP 404 at pin** | Same as `faddf` (or `__builtin_fsubf`) | U3 / U1; P-MATH |
| 12 | `fsubl` | `BLOCKED_TOOLCHAIN` | row 12: `generic/fsubl.cpp` + **ADD**; upstream returns `float` from `long double` args | Same as `faddl` (adapt to `long double`) | U2; P-MATH + `nm` |
| 13 | `fmul` | `BLOCKED_TOOLCHAIN` | row 13: `generic/fmul.cpp` + **MUL**; upstream `float fmul(double,double)` vs C23 `double fmul(...)` | Vendor **MUL** + C++20 shim + adapt result to `double` | U2; P-MATH + `nm` |
| 14 | `fmulf` | `BLOCKED_NO_SOURCE` | row 14: `generic/fmulf.cpp` — **HTTP 404 at pin** | Same as `faddf` (or `__builtin_fmulf`) | U3 / U1; P-MATH |
| 15 | `fmull` | `BLOCKED_TOOLCHAIN` | row 15: `generic/fmull.cpp` + **MUL**, `DyadicFloat`, `quick_mul`; contract mismatch vs C23 | Vendor **MUL** + C++20 shim + adapt result to `long double` | U2; P-MATH + `nm` |
| 16 | `fdiv` | `BLOCKED_TOOLCHAIN` | row 16: `generic/fdiv.cpp` + **DIV**; upstream `float fdiv(double,double)` vs C23 `double fdiv(...)` | Vendor **DIV** + C++20 shim + adapt result to `double` | U2; P-MATH + `nm` |
| 17 | `fdivf` | `BLOCKED_NO_SOURCE` | row 17: `generic/fdivf.cpp` — **HTTP 404 at pin** | Same as `faddf` (or `__builtin_fdivf`) | U3 / U1; P-MATH |
| 18 | `fdivl` | `BLOCKED_TOOLCHAIN` | row 18: `generic/fdivl.cpp` + **DIV**; contract mismatch vs C23 | Vendor **DIV** + C++20 shim + adapt result to `long double` | U2; P-MATH + `nm` |
| 19 | `hypot (3-argument C23 overload)` | `BLOCKED_NO_SOURCE` | row 19: upstream `hypot.h` declares only `double hypot(double,double)`; `generic/hypot3.cpp` — **HTTP 404 at pin**. Naive `sqrt(x*x+y*y+z*z)` is an invented algorithm with different overflow/rounding | A different pinned upstream with a genuine 3-arg implementation, or newer toolchain with a 3-arg builtin. Vendoring `hypot.cpp` cannot satisfy this API | U3 / U1; P-MATH |
| 20 | `frexp16` | `BLOCKED_TOOLCHAIN` | row 20: `generic/frexpf16.cpp` + **MANIP**; LLVM name is `frexpf16`, not C23 `frexp16` | Vendor **MANIP** + C++20 TU using `float16`; add a C ABI shim named `frexp16` with the `_Float16` signature (or `__builtin_frexp16`) | U2 / U1; P-MATH |
| 21 | `fma16` | `BLOCKED_NO_SOURCE` | row 21: `generic/fmaf16.cpp` — **HTTP 404**; closest `f16fma.cpp` has signature `float16 f16fma(double,double,double)`, not `_Float16 fma16(_Float16,_Float16,_Float16)` | A pinned source with the C23 name and `_Float16` signature (or `__builtin_fma16`). Vendoring the mismatched `f16fma.cpp` alone is not a closure | U3 / U1; P-MATH |
| 22 | `logb16` | `BLOCKED_TOOLCHAIN` | row 22: `generic/logbf16.cpp` + **MANIP**; LLVM name is `logbf16` | Vendor **MANIP** + C++20 TU + `logb16` C ABI shim with `_Float16` signature (or `__builtin_logb16`) | U2 / U1; P-MATH |
| 23 | `exp16` | `BLOCKED_TOOLCHAIN` | row 23: `generic/expf16.cpp` + `generic/expxf16.h` + **EXP16**; this is the full algorithm, not a wrapper (tables `EXP_HI`/`EXP_MID`, minimax polynomials, `ExceptValues<float16,*>`, `polyeval`, …); no `cplus_*` substitute | Vendor the algorithm, its generated tables and the whole **EXP16** closure in a C++20 TU and add a `_Float16 exp16(_Float16)` C ABI shim (or `__builtin_exp16`). Copying tables alone would be fabricating a partial implementation | U2 / U1; P-MATH |

Subtotal: `BLOCKED_TOOLCHAIN` 17, `BLOCKED_NO_SOURCE` 6.

### 4.2 The four other APIs classified MISSING

| # | Exact API name | Class | Evidence | Exact unblock condition | Route / proof |
|---:|---|---|---|---|---|
| 24 | `timespec_getres` | `BLOCKED_NO_SOURCE` | MATHCLOSURE §4 `:158`: no source at musl `v1.2.6`/`master` (only `timespec_get.c`) or LLVM `llvmorg-20.1.1`/`main`; host `<time.h>` does not declare it, no host symbol; deriving from `clock_getres` is a new algorithm and forbidden | A pinned upstream revision that actually ships `timespec_getres`, **or** explicit authorisation to implement it against a host `clock_getres`-style primitive. No `cplus_timespec_getres` body is emitted today | U3 / U5; P-SMALL |
| 25 | `mbrtoc8` | `BLOCKED_NO_SOURCE` | MATHCLOSURE §4 `:160`: 404 at musl pin `7813e911…`, tag `v1.2.6`, `master`, bminor mirror; only an unmerged 2023 patch (`8645e112…`/`0c73fa10…`) with correctness objections; host has **no `<uchar.h>`** at all (INV `:16`); vendored tree has no `src/multibyte/` | A pinned musl revision that ships the function, or explicit orchestrator authorisation to integrate the unmerged patch with the documented host-adaptation edits. No `cplus_mbrtoc8` body is emitted today | U3 / U5; P-UCHAR |
| 26 | `c8rtomb` | `BLOCKED_NO_SOURCE` | MATHCLOSURE §4 `:161`: same 404/patch status; additionally the patch relies on musl `wcrtomb` ignoring its state argument while `cplus_wcrtomb` delegates to the host | Same as `mbrtoc8`. No `cplus_c8rtomb` body is emitted today | U3 / U5; P-UCHAR |
| 27 | `FE_TIESAWAY` | `BLOCKED_HOST_SYMBOL` | MATHCLOSURE §4 `:159`: absent from host `<fenv.h>` and LLVM's pinned `fenv-macros.h`; AArch64 FPCR has no ties-away encoding; it is a compiler-provided macro, not a function; capability macro stays 0 | A toolchain/host whose `<fenv.h>` defines `FE_TIESAWAY` (then the alias activates), a pinned header providing a portable value, or explicit authorisation to publish a value | U4 / U5 / U8; P-SMALL |

### 4.3 The 34 `<stdatomic.h>` APIs classified UNSUPPORTED

All 34 share one exact class and one exact condition. INV limitation: *"No standalone `cplus_atomic_*` C ABI symbol is shipped: C11 `_Generic` macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host `<stdatomic.h>`/`__atomic_*` builtins are used internally only."* MATRIX `top_gaps` rank 9 / PLAN §6.3 add: **do not fake generic symbols**; the supported pattern is host-header calls plus a compile-only consumer test. Class: `BLOCKED_TOOLCHAIN` for every row. Unblock for every row: **U9** (docs + `tests/compat/atomic_tgmath.c` + explicit UNSUPPORTED SDK rows; Agent 1 only if type-generic dispatch is ever wanted). No C ABI body is proposed.

C11 group (27): `atomic_init`, `atomic_is_lock_free`, `atomic_store`, `atomic_store_explicit`, `atomic_load`, `atomic_load_explicit`, `atomic_exchange`, `atomic_exchange_explicit`, `atomic_compare_exchange_strong`, `atomic_compare_exchange_strong_explicit`, `atomic_compare_exchange_weak`, `atomic_compare_exchange_weak_explicit`, `atomic_fetch_add`, `atomic_fetch_add_explicit`, `atomic_fetch_sub`, `atomic_fetch_sub_explicit`, `atomic_fetch_or`, `atomic_fetch_or_explicit`, `atomic_fetch_xor`, `atomic_fetch_xor_explicit`, `atomic_flag_clear`, `atomic_flag_clear_explicit`, `atomic_flag_test_and_set`, `atomic_flag_test_and_set_explicit`, `atomic_signal_fence`, `atomic_thread_fence`, `kill_dependency`.

C23 group (7): `atomic_fetch_and`, `atomic_fetch_and_explicit`, `atomic_fetch_nand`, `atomic_fetch_nand_explicit`, `atomic_wait`, `atomic_notify_one`, `atomic_notify_all` — INV: *"C23 addition; not shipped for the same type-generic reason as the C11 atomics."*

### 4.4 The 13 `<stdio.h>` / `<stdlib.h>` APIs classified UNSUPPORTED

| Exact API name | Header | Class | Evidence | Exact unblock condition | Route / proof |
|---|---|---|---|---|---|
| `gets` | stdio.h | `ALREADY_HOST_PROVIDED_NEEDS_WRAPPER` | INV: host symbol exists; *"Removed from ISO C in C11 (unsafe, unbounded). Deliberately not shipped; use bounded fgets / cplus_fgets instead."* | Recorded product decision to ship a namespaced deprecated wrapper **or** keep UNSUPPORTED with the documented bounded alternative; no source/toolchain work | U8; P-SAFECRT |
| `tmpnam` | stdio.h | `ALREADY_HOST_PROVIDED_NEEDS_WRAPPER` | INV: host symbol exists; *"Insecure TOCTOU race; deliberately not shipped. Use cplus_tmpfile or mkstemp (cplus_mkstemp) instead."* | Same as `gets` | U8; P-SAFECRT |
| `fopen_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/io/fopen_s.c`, probe **OK**, 5-source closure, `ADAPTER_ONLY`; local `cplus_fopen_s(const char*,const char*)` returns `FILE*` (SAFE_IO.H:10), while pinned upstream is `errno_t fopen_s(FILE *restrict *restrict, …)` (`:83`, C11 K.3.5.2.1) | Owner wrapper-semantics decision, then add pinned path/prefix + ISO-signature declaration, contract, tests; existing adapter must not silently change | U7; P-SAFECRT |
| `fclose_s` | stdio.h | `BLOCKED_HOST_SYMBOL` | INV: *"host macOS CRT lacks Annex K semantics"*; no pinned `fclose_s.c` in safeclib (SAFECRTINV has no entry); local `cplus_fclose_s(FILE*)` is non-ISO (ISO: `errno_t fclose_s(FILE *restrict *restrict)`) | Host/toolchain Annex K provider, or explicit authorisation for a new ISO-conformant namespaced body (no pinned source to copy) | U4 / U5 / U8; P-SAFECRT |
| `fread_s` | stdio.h | `BLOCKED_HOST_SYMBOL` | INV: same host limitation; no pinned `fread_s.c`; local `cplus_fread_s(void*,size_t,size_t,size_t,FILE*)` returns `size_t` (non-ISO) | Same as `fclose_s` | U4 / U5 / U8; P-SAFECRT |
| `fprintf_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/io/fprintf_s.c`, probe **OK**, `ADAPTER_ONLY`, closure adds `vsnprintf_s.c`, `wcsnlen_s.c`, `wcstombs_s.c` (all pinned; SAFECRT §5 B4) | U7 wrapper-semantics decision + B4 archive additions + contract/tests | U7; P-SAFECRT |
| `printf_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/io/printf_s.c`, probe **OK**, `ADAPTER_ONLY`, same B4 closure | Same as `fprintf_s` | U7; P-SAFECRT |
| `sprintf_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/str/sprintf_s.c`, probe **OK**, `ADAPTER_ONLY`; `int sprintf_s(char *restrict dest, rsize_t dmax, const char *restrict fmt, …)` (`:93`, K.3.5.3.6) | Same as `fprintf_s`; note SAFECRT §3.2 excludes Darwin-fortify-conflicting `strlcpy*` only, not this source | U7; P-SAFECRT |
| `scanf_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/io/scanf_s.c`, probe **OK**, `ADAPTER_ONLY`, closure includes `safec_floatscan.c`, `safec_intscan.c`, `safec_shgetc.c`, `safec_vfscanf_s.c` | Same as `fprintf_s` | U7; P-SAFECRT |
| `fscanf_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/io/fscanf_s.c`, probe **OK**, `ADAPTER_ONLY`, same scan closure | Same as `fprintf_s` | U7; P-SAFECRT |
| `sscanf_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/io/sscanf_s.c`, probe **OK**, `ADAPTER_ONLY`, same scan closure | Same as `fprintf_s` | U7; P-SAFECRT |
| `gets_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/io/gets_s.c`, probe **OK**, 5-source closure, `NOT_INTEGRATED`; SAFECRT §3.4 `:187` explicitly excludes it *"unless policy changes"* (base `gets` is UNSUPPORTED) | Policy decision to ship the Annex K name, then U6-style integration. Pinned source exists; the gate is policy, not code | U8 + U6; P-SAFECRT |
| `tmpnam_s` | stdio.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRTINV: `src/io/tmpnam_s.c`, probe **OK**, closure adds `strncpy_s.c`; SAFECRT §3.4 `:187` and `:245-247` exclude it: requires `SAFECLIB_ENABLE_UNSAFE`, deliberately avoided | Policy decision + config change to enable `SAFECLIB_ENABLE_UNSAFE`, then U6-style integration | U8 + U6; P-SAFECRT |
| `bsearch_s` | stdlib.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRT §3.4/§4 ranks it #1: `src/misc/bsearch_s.c`, probe **OK**, green 4-source closure, `NOT_INTEGRATED`, no adapter, low risk; MATRIX rank 8 records the ISO gap | U6 B1: add path + prefix + `cplus_sc_bsearch_s` wrapper/declaration, contract entry, ISO-signature tests | U6; P-SAFECRT |
| `qsort_s` | stdlib.h | `IMPLEMENTABLE_FROM_PINNED_SOURCE` | SAFECRT §3.4/§4 ranks it #2: `src/misc/qsort_s.c`, probe **OK**, green 5-source closure, `NOT_INTEGRATED`; source is `errno_t qsort_s(void *base, rsize_t nmemb, rsize_t size, …)` (`:348`, K.3.6.3.2) | U6 B1, paired with `bsearch_s` | U6; P-SAFECRT |

Subtotal (13): `IMPLEMENTABLE_FROM_PINNED_SOURCE` 11, `BLOCKED_HOST_SYMBOL` 2. Note the deliberate contradiction: MATRIX rank 8 says `bsearch_s`/`qsort_s` have `source: NO EVIDENCE`, but SAFECRTINV and `third_party/safeclib/src/misc/` prove the pinned source is on disk and compiles — this is a traceability gap, not an absent source (§7.2).

## 5. Exact list of APIs that could be closed now

Definition used: no external source/toolchain/host dependency remains; the only work is owner integration of an artifact already on disk plus the named proof gates. **These APIs are still MISSING/UNSUPPORTED today — nothing here is closed by this document.**

**Tier A — pinned source, green closure, no adapter conflict (2):**
`bsearch_s`, `qsort_s`. Route U6 (SAFECRT batch B1).

**Tier B — pinned source and compiling probe, but the existing non-ISO `cplus_*_s` adapters require a wrapper-semantics/contract decision first (7):**
`fopen_s`, `fprintf_s`, `printf_s`, `sprintf_s`, `scanf_s`, `fscanf_s`, `sscanf_s`. Route U7 (SAFECRT batch B4); the pinned sources use ISO Annex K signatures and the adapters do not.

**Tier C — pinned source, but explicitly policy-excluded today (2):**
`gets_s` (base `gets` removed from ISO C), `tmpnam_s` (requires `SAFECLIB_ENABLE_UNSAFE`). Routes U8 then U6.

**Tier D — host symbol already present, only a deprecation/namespacing policy decision (2):**
`gets`, `tmpnam`. Route U8; no implementation needed.

**Strict "no policy gate at all" answer: Tier A + Tier B = 9 APIs** (`bsearch_s`, `qsort_s`, `fopen_s`, `fprintf_s`, `printf_s`, `sprintf_s`, `scanf_s`, `fscanf_s`, `sscanf_s`). If policy decisions are included as available owner actions, the full answer is **13 APIs** (Tiers A–D). Nothing in the 27 MISSING set can be closed now: 17 need an approved C++20 closure/toolchain, 9 need a source or authorisation that does not exist, and 1 needs a host macro.

## 6. Completion statement — who, when, and what cannot be claimed

**A 100% ISO C coverage / "all gaps closed" claim cannot be made while any of the 76 APIs above remains classified MISSING or UNSUPPORTED in `docs/libc/iso_c_inventory.json`.** Passing builds, SKIP-marked harnesses, nearest-equivalent `EQUIV … exercised` lines in `tests/family_gaps/math_gaps.c`, and host-provided alternatives (`cplus_fgets`, `cplus_tmpfile`/`cplus_mkstemp`, direct `<stdatomic.h>`) are evidence of *behaviour around* the gaps, not closure of the named APIs (MATHCLOSURE §6; PLAN §0.1/§1.8).

Responsibility and timing (roles from PLAN §0.3, not invented names; no dates are promised by this docs-only task):

| Blocker set | Who can close it | When / condition |
|---|---|---|
| 11 `IMPLEMENTABLE_FROM_PINNED_SOURCE` + 2 `ALREADY_HOST_PROVIDED_NEEDS_WRAPPER` (§5) | **Agent 2 / `A2-runtime`** for build script, wrappers, contracts, tests; **product/ABI owner** for the Tiers B–D semantics/policy decisions (PLAN §0.3; SAFECRT §5) | In a dedicated integration change after the wrapper-semantics decision is recorded; close only when U6/U7/U8 artifacts exist and P-SAFECRT passes with ISO-signature tests and no SKIP |
| 17 `BLOCKED_TOOLCHAIN` math APIs | **Parent/orchestrator** approves C++20 TU + vendoring LLVM `__support`/`hdr`/`macros`/`CPP` at `424c2d9…` (MATHCLOSURE §2, PLAN §6.1); then `A2-runtime` integrates; ABI/signature adaptations need the same approval | Only after that decision and a C ABI shim with correct C23 signatures; prove with P-MATH (`skipped=0`) + `nm` check + full regression. `libc/src/math/math_c23_upstream.c` is not in CMake today; even the extern-only file needs a parent decision (PLAN §6.1) |
| 9 `BLOCKED_NO_SOURCE` APIs (`faddf/fsubf/fmulf/fdivf`, `hypot` 3-arg, `fma16`, `timespec_getres`, `mbrtoc8`, `c8rtomb`) | **Upstream providers** (LLVM libc, musl) publish a pinned conforming source, or the **orchestrator** explicitly authorises a named alternative (U5); **toolchain provider** may supply builtins (U1) | When a pin + SHA + license is recorded for a conforming source, or a written authorisation exists. Do not derive/import HTTP-404 files or the unmerged UTF-8 patch before then |
| 1 `FE_TIESAWAY` | **Toolchain/host provider** (or explicit authorisation to publish a value) | When a host `<fenv.h>` defines it and the alias activates (U4), or a pinned portable header/authorisation exists (U5/U8) |
| 2 Annex K `fclose_s`/`fread_s` (`BLOCKED_HOST_SYMBOL`) | **Product/ABI owner** + host provider | When an Annex K-capable host exists, or an explicit decision authorises a new ISO-conformant namespaced body (no pinned source to copy) |
| 34 `stdatomic.h` APIs (`BLOCKED_TOOLCHAIN`) | **`A2-docs`** now; **Agent 1 / `A1-compiler`** only if type-generic dispatch is ever wanted | When U9 docs + `tests/compat/atomic_tgmath.c` + SDK UNSUPPORTED rows land. Do not invent `cplus_atomic_*` symbols to make a count go to zero |

No subagent, build, or emulator run closes any of these. Each closure requires the owning agent's change plus the proof command named in §3, and the recorded inventory classification must be updated only after that proof passes.

## 7. Evidence caveats (reported only — nothing here was edited)

1. **INV `math.h` metadata is inconsistent with its own records.** `by_classification` says `MISSING: 34`; the 223 `apis[]` entries actually contain 23 `MISSING`, 23 `HOST/COMPILER-PROVIDED`, 174 `PLATFORM_BACKED`, 3 `IMPLEMENTED_UPSTREAM` (the last category is absent from the metadata). The 27 total used here matches PLAN §1.2 and is derived from per-API records, as MATHCLOSURE §7.2 directs.
2. **MATRIX rank 8 vs. SAFECRTINV/MATRIX rank 3.** MATRIX says `bsearch_s`/`qsort_s` have `source: NO EVIDENCE`, while `docs/libc/safeclib_inventory.json` and `third_party/safeclib/src/misc/` show pinned, probe-OK sources; PLAN §6.3 calls a portable thunk "only a proposal". The on-disk pinned source is the stronger evidence for `IMPLEMENTABLE_FROM_PINNED_SOURCE`; the thunk proposal is not used here and would be a new algorithm without it.
3. **Different axes, different counts.** MATRIX (`total_apis: 714`, `by_status.UNSUPPORTED: 13`, `by_classification.MISSING: 13`) and INV (948 APIs, 27 MISSING, 49 UNSUPPORTED) are different inventories; do not mix their numbers or use one to "close" the other.
4. **Incorrect `_Float16` claim in `docs/libc/MATH_GAPS_EVIDENCE.md:67,159-161`** (already reported in MATHCLOSURE §7.3): the host does define `__FLT16_MANT_DIG__ = 11`, so the four `_Float16` gaps are blocked by the C++ closure/signature mismatch, not by a missing type.
5. **Adapter names are not ISO closures.** `cplus_fopen_s`/`cplus_fclose_s`/`cplus_fread_s` and the `cplus_*_s` format adapters exist but have non-Annex-K signatures (§4.4); `stdlib/safe/safe_io.h:14-24` even macro-maps the unprefixed Annex K names to them. This is why INV still records those APIs UNSUPPORTED.
6. **Pins used here:** LLVM libc `llvmorg-20.1.1` → commit `424c2d9b7e4de40d0804dd374721e6411c27d1d1`; musl `7813e9119eb5fe681b6750b8a033a0490bca0941`; safeclib `90b076e94c4b0fb202dddf948d5d620e5395c1b0` (Apache-2.0 WITH LLVM-exception / MIT / MIT respectively, per MATHCLOSURE §1 and PROVENANCE §1). No version, blob SHA or signature was invented for any HTTP-404 file.
