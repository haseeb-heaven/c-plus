# Normal C APIs vs checked (`_s` / hardened) APIs

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23
**Scope:** the C surface only — the bundled hardened libc (`libc/`), the
optional SafeCRT layer (`stdlib/safe/`), and the compiler-side contract work
(Agent 1). This document is descriptive: it records what the code and tests
actually do, including the limits. It is **not** a safety certificate.

## 0. Terms — there are three layers, not two

| Layer | Examples | What it is |
|---|---|---|
| **Ordinary spelling** | `malloc`, `memcpy`, `strcpy`, `fread`, `printf`, and the namespaced `cplus_*` wrappers in `libc/install-include/cplus_libc.h` (e.g. `cplus_strcpy`, `cplus_swprintf`) | Exact ISO C signatures and host semantics. No `dmax`, no extra return code. Raw pointer validity and caller-supplied lengths remain the caller's problem. The `cplus_*` names are thin, selective-link delegates to the host runtime (`cplus_libc.h:216-225`, `docs/libc/WIDE_FORMAT_CONTRACT.md:10-28`). |
| **Checked layer (explicit `_s`)** | `cplus_strcpy_s`, `cplus_memcpy_s`, `malloc_s`, `cplus_snprintf_s`, `cplus_fread_s`, `cplus_swprintf_checked` | Standard or Annex-K-style signatures with an **explicit capacity argument** (`dmax`/`smax`/`bmax`) or an explicit checked sibling, a defined error return, and runtime constraint checks. Headers: `stdlib/safe/safe_memory.h`, `safe_string.h`, `safe_alloc.h`, `safe_io.h`, `safe_format.h`. |
| **Ordinary-name mapping layers** | `stdlib/safe/safe_c.h`, `libc/install-include/cplus_libc_safe.h` | Macros that keep the *ordinary* spelling and infer capacity from tracked allocation metadata plus `__builtin_object_size`. They are opt-in, explicit, and do **not** interpose host libc symbols (`docs/libc/ORDINARY_C_SAFETY.md:22-26`, `libc/src/string/bounded.c:1-15`). |

The two ordinary-name layers differ only in how they report a violation:

| Mapping layer | Header | Link | Violation reporting |
|---|---|---|---|
| Plain C+ libc | `libc/install-include/cplus_libc_safe.h` | `build/libc/libcplus_hardened.a` | `cplus_libc_log` + `errno = ERANGE` / `EINVAL` / `EOVERFLOW` |
| C+ SafeCRT | `stdlib/safe/safe_c.h` | + `build/safe/libcplus_safecrt.a` | safeclib constraint handler, e.g. `ESNOSPC` (406) |

Both resolve capacity for **interior pointers** and for the **one-past-the-end
address** (`docs/libc/ORDINARY_C_SAFETY.md:38-47`, `stdlib/safe/safe_alloc.h:76-81`).
Evidence: `tests/libc/test_libc_safe_api.c` (plain libc only) and
`tests/crt/unit/test_safe_c_api.c` (SafeCRT), both under UBSan.

---

## 1. Comparison by family

Each family uses a two-column table: **ordinary spelling → what it does** and
**checked layer → what it adds**. The "neither" and "compiler-side" lines that
follow apply to both columns.

### 1.1 Allocation

| Ordinary spelling (what it does) | Checked layer (what it adds) |
|---|---|
| `malloc`/`calloc`/`realloc`/`free` keep host semantics and return types. `cplus_malloc`/`cplus_calloc`/`cplus_realloc`/`cplus_free` in `libc/install-include/cplus_libc.h:23-27` have the *same* signatures and return values, but additionally record live-block metadata. A raw `malloc` block is not in that table. | `malloc_s`/`calloc_s`/`realloc_s`/`free_s`/`free_checked_s`/`aligned_alloc_s` (`stdlib/safe/safe_alloc.h`) add overflow checks against `RSIZE_MAX_MEM`, `errno = ENOMEM` on OOM / `EINVAL` on invalid size, defined zero-size behaviour (a tracked 1-byte block), power-of-two alignment validation, **live-allocation metadata** (`byte_size`, `elem_size`, `elem_count`, `generation`, `is_live`), double-free and invalid-free refusal, leak counters, length queries and explicit ownership transfer. `realloc_s` preserves the original pointer and bytes on failure and bumps the generation only on success. |

- **Neither guarantees:** raw pointer provenance for blocks from another allocator, arbitrary alias/lifetime safety, or detection of a stale alias whose address was re-issued (the recent-free cache is bounded — `safe_alloc.h:33-37`). A raw `free(p)` twice is still UB; `tests/adversarial/provenance_attack_NOTES.md:42-58` documents a diagnostic-attribution nuance, not a provenance break.
- **Compiler-side (Agent 1):** compile-time double-free/moved-value/use-after-free detection and ownership-transfer checking (`compiler/Analysis/OwnershipAnalysis.cpp`, `compiler/Sema/StaticBoundsCheck.cpp`); allocation-size/return OOB diagnostics (`tests/compile_fail/alloc_size_oob.c`, `alloc_return_oob.c`). The library's metadata is *runtime* diagnosis only.
- **Examples/tests:** `examples/crt/malloc_s.c`, `free_s.c` (double free safely refused at line 7); `examples/crt/calloc_s.c`, `realloc_s.c`; `tests/libc/test_alloc_metadata.c`, `tests/libc/test_provenance_attack.c`, `tests/libc/test_libc_safe_api.c`.

### 1.2 Memory

| Ordinary spelling (what it does) | Checked layer (what it adds) |
|---|---|
| `memcpy(dst, src, n)`, `memmove`, `memset`, `memcmp` preserve host semantics: `n` is caller-supplied, there is no destination-capacity argument, and overlapping raw `memcpy` is UB. The plain libc delegates (`cplus_memcpy` et al.) are differential-tested against the host (`docs/CRT_MATRIX.md`). | `cplus_memcpy_s(dest, dmax, src, smax)`, `memmove_s`, `memset_s`, `memcmp_s`, `memzero_s` (`safe_memory.h:41-45`) add explicit `dmax`/`smax`, an `errno_t` result, overlap rejection, NULL/zero-length/max-size constraints, and a defined zero-length `memzero_s`. The ordinary-name wrappers infer capacity instead: `cplus_memcpy_s_auto(..., known_dmax)` / `cplus_*_bounded(..., known)` (`libc/src/string/bounded.c:30-120`). |

- **Neither guarantees:** a caller-supplied wrong `n` passed to the raw spelling, provenance of a foreign pointer, alias/lifetime safety, or protection after memory has already been corrupted. `CPLUS_SAFE_OBJ_SIZE` is `(size_t)-1` for genuinely unknown capacity; default mode then preserves raw-C behaviour with a warning and makes **no** safety claim (`safe_memory.h:55-67`, `docs/libc/ORDINARY_C_SAFETY.md:49-51`).
- **Compiler-side (Agent 1):** constant/loop OOB rejection and `CPLUS-SAFE-BOUNDS` class diagnostics (`tests/compile_fail/constant_oob.c`, `loop_oob.c`, `multidim_bounds.c`, `compiler/Analysis/BoundsAnalysis.cpp`); strict-aliasing/type-confusion and VLA-`sizeof` rules are language/compiler responsibilities (`docs/libc/C_BUG_CLASS_MATRIX.md:40,42`).
- **Examples/tests:** `examples/crt/memcpy_s.c` (line 10: `cplus_memcpy_s(dst, 3, src, sizeof(src))` must fail), `memset_s.c`, `memmove_s.c`, `memcmp_s.c`, `memzero_s.c`; `tests/crt/memory_ops.c`, `tests/libc/test_memory_attack.c`, `tests/libc/test_libc_safe_api.c:21-35` (interior pointer and one-past-end refusal with `errno == ERANGE`).

### 1.3 String

| Ordinary spelling (what it does) | Checked layer (what it adds) |
|---|---|
| `strcpy(d, s)`, `strcat`, `strncpy`, `strncat` have **no size argument** for the destination; they trust the caller and NUL-termination of the source. `strlen(NULL)` is standard UB. The ordinary-name wrappers add no parameter — they only call `cplus_strlen(s) + 1` to compute what is needed. | `cplus_strcpy_s(dest, dmax, src)`, `strncpy_s`, `strcat_s`, `strncat_s`, `strcmp_s`, `strnlen_s`, `strtok_s`, `strerror_s` (`safe_string.h:41-48`) add explicit `dmax`/`slen`, an `errno_t` (or bounded count / token pointer) result, termination and constraint checks: `ESNOSPC` (destination too small), `ESUNTERM` (unterminated source), overlap, max size. The 2-argument unprefixed `strcpy_s(d, s)` macro infers `dmax` from `CPLUS_SAFE_OBJ_SIZE(d)` (`safe_string.h:68-75`); the plain libc equivalent is `cplus_strcpy_bounded(..., known)` with `errno = ERANGE` (`bounded.c:96-103`). |

- **Neither guarantees:** an unterminated buffer passed to the ordinary `strcpy`/`strlen`, provenance/capacity of a pointer that is neither a tracked block nor a stack/global object of known size, or alias/lifetime safety. `cplus_strcpy_bounded` explicitly leaves an unterminated source as caller UB (`libc/src/string/bounded.c:99-101`).
- **Compiler-side (Agent 1):** constant-length/terminator checks (`tests/cases/classic_bug8_terminator.c`, `tests/known_gaps/missing_terminator.c`), static bounds diagnostics, and format-literal enforcement. `cplus_safe_len(ptr)` is a metadata query, not a compiler proof.
- **Examples/tests:** `examples/crt/strcpy_s.c` (line 9: copy into `char small[4]` must fail), `strncpy_s.c`, `strcat_s.c`, `strncat_s.c`, `strnlen_s.c` (bounded length in `[0, smax]`, `NULL` → 0); `tests/crt/string_ops.c`, `tests/crt/strncpy_ops.c`, `tests/libc/test_string_extra*.c`.

### 1.4 stdio / format

| Ordinary spelling (what it does) | Checked layer (what it adds) |
|---|---|
| `fread`/`fwrite` preserve host semantics: no buffer-capacity parameter; `size * count` overflow is UB at the call site. `printf(fmt, ...)` trusts the caller's format and arguments; a non-literal/untrusted format and `%n` are dangerous. The namespaced `cplus_printf`/`cplus_fprintf`/`cplus_snprintf` carry narrow `format(printf, m, n)` attributes (`cplus_libc.h:120-132`) and the plain libc wrappers guard `size * count` overflow and NULL arguments. | `cplus_fread_s(buf, bmax, size, count, stream)` / `fwrite_s` add an Annex-K `bmax` and clamp `count` to `bmax / size` (`safe_io.h:13-15`, `docs/CRT_MATRIX.md`). `cplus_fopen_s`/`cplus_fclose_s` keep standard signatures but add a live/closed stream cache that rejects double close. `printf_s`/`fprintf_s`/`sprintf_s`/`snprintf_s`/`scanf_s`/`fscanf_s`/`sscanf_s` (and the `*_checked` siblings in `safe_format.h:70-76`) reject non-literal formats and all `%n` length/positional forms, enforce explicit widths on `%s`/`%[...]` scans, check `dest` against tracked bounds and `__builtin_object_size`, and return `int` (`>= 0` success, `< 0` with `errno = EINVAL` on violation). |

- **Neither guarantees:** format validity (argument count/type agreement) for an arbitrary runtime format, the actual capacity of a raw `FILE*` buffer, concurrent access to a shared `FILE*`, or alias/lifetime safety. On Apple Clang 16 the **wide** `format(wprintf/wscanf, ...)` attributes are ignored (`-Wignored-attributes`), so there is no static wide-format checking on this host.
- **Compiler-side (Agent 1):** narrow-format `format(printf/scanf, m, n)` diagnostics for literal formats, full static taint/literal enforcement for `_s` formats (`safe_format.h:19-20`), and compile-fail evidence such as `tests/compile_fail/classic_bug16_format_string.c` and `classic_bug32_fread.c`.
- **Examples/tests:** `examples/crt/fopen_s.c`, `fclose_s.c` (double close detected), `fread_s.c` (line 11 passes `sizeof(buf)` as `bmax`), `fwrite_s.c`, `printf_s.c`, `snprintf_s.c` (line 8 truncation reports required length), `sprintf_s.c`, `vsnprintf_s.c`, `fprintf_s.c`, `scanf_s.c`, `fscanf_s.c`; `tests/crt/format_ops.c`, `tests/crt/test_safe_format.c`, `tests/adversarial/format_attack_NOTES.md`.

### 1.5 Conversion

| Ordinary spelling (what it does) | Checked layer (what it adds) |
|---|---|
| `strtol`/`strtoll`/`strtod`/`strtoimax`, `wcstol`/`wcstod`, `atoi`/`atol` keep host value/errno/`endptr` semantics; `cplus_*` versions delegate to the host runtime (`cplus_libc.h:107,111,191-192,239-242`). Overflow saturates with `ERANGE`, no extra capacity argument exists. | **There is no dedicated `strtol_s`/`strtod_s` in this project.** The checked surface here is: guarded NULL entry points returning a defined failure with `errno = EINVAL`, bounded helpers `cplus_strnlen_s(str, smax)` and `cplus_strerror_s(dest, dmax, errnum)` (`safe_string.h:46,48`), and width-enforced `scanf_s` conversions. `tests/methods/hundred_conversion.c` compares each wrapper against the host on value, `endptr` and `errno`, including base 1/37 and 40-digit overflow. |

- **Neither guarantees:** that a caller's `endptr` or base is meaningful, that a string is NUL-terminated, provenance/capacity for arbitrary pointers, or alias/lifetime safety. Host-specific `errno` details are delegated, not reimplemented (`docs/libc/C_BUG_CLASS_MATRIX.md:30`).
- **Compiler-side (Agent 1):** constant-overflow rejection (`tests/runtime_fail/classic_bug9_overflow.c`), signed-overflow/divide-by-zero treatment (`C_BUG_CLASS_MATRIX.md:31,39`), and `format(scanf, m, n)` diagnostics for scan conversions.
- **Examples/tests:** no conversion example in `examples/crt/` (the checked entries there are `strnlen_s.c`); `tests/methods/hundred_conversion.c`, `tests/methods/methods_conversion.c`, `tests/rust_vs_cplus/conversion_c.c`, `tests/conformance/case_*.rs`, `tests/family_gaps/string_checked.c`.

### 1.6 Time / wide

| Ordinary spelling (what it does) | Checked layer (what it adds) |
|---|---|
| `time`/`difftime`/`gmtime(_r)`/`localtime(_r)`/`mktime`/`strftime`/`clock_gettime` delegate to the host with unchanged semantics and return values (`cplus_libc.h:156-161,188-189`). Wide I/O (`cplus_swprintf`, `cplus_vswprintf`, `cplus_wscanf`, …) is a thin delegate; **Tier 1 does not detect `%n`**, and format validity and destination capacity are entirely the caller's responsibility (`docs/libc/WIDE_FORMAT_CONTRACT.md:32-40`). | **No `_s` time variant exists.** Time wrappers add NULL guards where explicitly defined, deterministic `strftime` truncation behaviour and host-differential results, with logging ON/OFF parity (`tests/family_gaps/time_checked.c`). The one checked sibling is `cplus_swprintf_checked(s, n, fmt, ...)`: it scans for `%n` including length/positional/escaped variants, returns `-1` with `errno = EINVAL` **before** `va_start` and without writing to `s` or consuming the `va_list`, then delegates to host `vswprintf` (`docs/libc/WIDE_FORMAT_CONTRACT.md:42-77`). |

- **Neither guarantees:** general format validity (Tier 2 is a `%n` deny-list, not a validator), buffer capacity, wall-clock/timezone values (host is the source of truth), shared `struct tm`/`mbstate_t` synchronisation, raw pointer provenance or alias/lifetime safety.
- **Compiler-side (Agent 1):** none is available for wide formats on this host — Apple Clang ignores the wide `format(wprintf/wscanf, ...)` attributes and the narrow kinds cannot apply to `const wchar_t *`, so the annotations are documentary only (`WIDE_FORMAT_CONTRACT.md:84-104`). Format-literal/static taint checking for narrow formats is the only compiler-side format guarantee.
- **Examples/tests:** no time/wide example in `examples/crt/`; `tests/libc/test_wchar_format.c` (Tier-1 `L"ab%n"` writes through the sentinel as the host does; Tier-2 refusals assert `-1`, `errno == EINVAL`, unchanged sentinel/destination, and logging parity), `tests/family_gaps/time_checked.c`, `tests/family_gaps/wchar_checked.c`, `tests/methods/hundred_time_fenv.c`, `tests/methods/hundred_wchar.c`.

---

## 2. What the checked layer actually adds (cross-cutting)

1. **Explicit capacity parameters.** `dmax`/`smax`/`bmax` are *caller-supplied*
   by the explicit `_s` API; the ordinary-name mapping layers instead infer a
   capacity from (a) live allocation metadata for this allocator, including
   interior pointers and the one-past-end address, and (b)
   `__builtin_object_size(p, 1)` for stack/global objects
   (`safe_c.h:17-27`, `safe_memory.h:47-67`, `libc/src/string/bounded.c:28-48`).
2. **Defined error codes.** Two distinct domains, do not confuse them:
   - Plain libc mapping layer: `errno = EINVAL` (NULL / unknown-capacity strict
     refusal), `ERANGE` (capacity exceeded), `EOVERFLOW` (`n > RSIZE_MAX_MEM`)
     — `libc/src/string/bounded.c:38-60,110-120`.
   - SafeCRT `errno_t` returns are **positive** safeclib constraint codes:
     `ESNULLP` 400, `ESZEROL` 401, `ESLEMIN` 402, `ESLEMAX` 403, `ESOVRLP` 404,
     `ESNOSPC` 406, `ESUNTERM` 407 (`third_party/safeclib/include/safe_lib_errno.h`,
     `safe_memory.h:24-31`, `safe_string.h:24-31`). Test with `!= 0`, **not**
     `< 0`. Allocator failures use `CPLUS_SAFE_EINVAL` 22 / `ENOMEM` 12 /
     `EOVERFLOW` 75 (`safe_error.h:7-12`). Format adapters return `int`, with
     `< 0` and `errno = EINVAL` on violation (`safe_format.h:31-34`).
3. **Runtime violation trap.** `cplus_runtime_violation(api, detail)` prints
   `[CPLUS-SAFE] <api>: <detail>` to stderr and, with the violation trap enabled
   (default 1), terminates with `_exit(70)`; inside `cplus_runtime_guard()` it
   unwinds unless `cplus_runtime_set_failstop(1)` is set
   (`stdlib/safe/src/safe_runtime.c:52-59,148`, `safe_runtime.h:42-54`,
   `docs/libc/RUNTIME_RECOVERY.md:14-18`). `docs/libc/ORDINARY_C_SAFETY.md:53-64`
   records exit status 70 rather than SIGSEGV for the original overflow demo.
4. **Metadata.** `cplus_alloc_meta` / `cplus_safe_alloc_meta` carry
   `byte_size`, `elem_size`, `elem_count`, `generation`, `is_live`; queries
   (`cplus_safe_remaining_capacity`, `get_meta`, `ptr_check_generation`,
   `cplus_safe_len`, `cplus_safe_transfer_ownership`) make size, staleness and
   ownership explicit **when the caller uses them**
   (`cplus_libc.h:30-40`, `safe_alloc.h:63-94`).
5. **Unknown-capacity policy.** Default warns and keeps raw-C behaviour;
   strict mode (`cplus_libc_set_unknown_capacity_mode(1)` /
   `cplus_safe_set_unknown_capacity_mode(1)`) refuses with `EINVAL`
   (`docs/libc/ORDINARY_C_SAFETY.md:85-95`, `bounded.c:38-48`).

---

## 3. What neither layer can guarantee

These are stated as limits in the source and docs, not as inferred gaps:

- **Raw pointer provenance.** A wrapper over a raw C pointer cannot discover
  that pointer's provenance or capacity. The live table covers only blocks from
  this allocator; host `malloc`, stack, static and foreign pointers are not
  tracked (`docs/libc/C_BUG_CLASS_MATRIX.md:1-6`, `safe_alloc.h:15-37`,
  `provenance_attack_NOTES.md:15-35`). Cross-allocator isolation is deliberate
  (`docs/libc/SAFE_CRT.md:3-10`).
- **Unknown capacity.** `CPLUS_SAFE_OBJ_SIZE(p) == (size_t)-1` means "unknown",
  never "verified"; default mode does not claim safety
  (`safe_memory.h:55-67`, `docs/libc/ORDINARY_C_SAFETY.md:74-78`).
- **Format validity.** Tier-1 wide formats preserve `%n` and all malformed
  formats; Tier 2 is a `%n` deny-list only, and no wide format checking exists
  on Apple Clang (`WIDE_FORMAT_CONTRACT.md:32-40,72-104`). Narrow raw
  `printf`-family calls with untrusted formats remain unsafe
  (`safe_c.h:38-40`).
- **Alias / lifetime / ownership of arbitrary pointers.** Detection is runtime,
  heuristic and bounded (recent-free caches, 256-entry stdio table, generation
  counters). Once memory is back to the host allocator, a stale raw dereference
  is UB and cannot be portably trapped (`C_BUG_CLASS_MATRIX.md:25,27,35`,
  `safe_alloc.h:33-37`). No second borrow checker is implemented — that is
  explicitly out of scope (`C_BUG_CLASS_MATRIX.md:55`).
- **Language-level UB.** Signed overflow, divide-by-zero, invalid shift,
  strict-aliasing violations, flexible-array/struct copy and `longjmp` bypassing
  cleanup are marked `NOT FIXABLE BY LIBC` (`C_BUG_CLASS_MATRIX.md:31,39,40,42,52,54`).

---

## 4. Which guarantees are compiler-side (Agent 1)

The library consumes compiler-provided *data* (`__builtin_object_size`,
`format` attributes) but does not itself perform static proof. Static rejection
belongs to Agent 1's compiler and its contract files:

- **Static bounds / OOB:** `compiler/Analysis/BoundsAnalysis.cpp`,
  `compiler/Sema/StaticBoundsCheck.cpp`, `CPLUS-SAFE-BOUNDS` / `CPLUS-BOUNDS`
  diagnostics; compile-fail fixtures `tests/compile_fail/constant_oob.c`,
  `loop_oob.c`, `alloc_size_oob.c`, `classic_bug1_oob.c`, `multidim_bounds.c`.
- **Ownership, lifetime, double-free, moves:** `compiler/Analysis/OwnershipAnalysis.cpp`,
  `compiler/Sema/RaiiAnalyzer.cpp`, `RaiiLowering.cpp`; fixtures
  `double_release.c`, `use_after_move.c`, `borrow_escape.c`, `stack_return.c`,
  `resource_copy.c`.
- **Format-literal / static taint enforcement:** `safe_format.h:19-20` states
  full static literal enforcement is performed by the C+ compiler pass; narrow
  `format(printf/scanf, m, n)` attributes are compiler-emitted diagnostics
  (`cplus_libc.h:120-132`, `safe_format.h:35-43`). Wide formats have **no**
  such guarantee on this host (`WIDE_FORMAT_CONTRACT.md:84-104`).
- **Language-level UB classes:** signed overflow, `/0`, invalid shift, strict
  aliasing, VLA `sizeof`, macro double evaluation — routed to the compiler layer
  (`docs/libc/C_BUG_CLASS_MATRIX.md:31,39,40,42,53`, `docs/libc/KNOWN_COMPILER_GAPS.md`).
- **Compiler-readable contracts:** `libc/contracts/compiler_libc_contracts.json`
  (32 APIs, nine keys each) mirrors Agent 1's requested schema; the five
  `CPLUS-SAFE-*` category strings are names the compiler may emit, while the
  library itself emits generic `[CPLUS-SAFE] <api>: ...` lines
  (`docs/libc/COMPILER_CONTRACT_ALIGNMENT.md:43-75`). Caveat: Agent 1's
  `compiler/Analysis/libc_contracts.json` is **not present on this branch**, so
  the alignment file must be re-validated when that work merges
  (`COMPILER_CONTRACT_ALIGNMENT.md:22-29,114-126`).

---

## 5. Three statements that must not be misread

### 5.1 No Rust-equivalence claim

The checked layer is runtime diagnosis, not Rust's compile-time ownership,
borrow and lifetime guarantees. The headers say so verbatim — "this is not
Rust-grade safety" (`safe_memory.h:36-39`, `safe_string.h:36-39`); the measured
`rustc 1.100.0` vs UBSan comparison shows rustc rejects use-after-free,
double-free, unsynchronised data races, uninitialized reads and constant
arithmetic overflow at compile time, while UBSan detects only the two
array-index cases and misses those five
(`docs/libc/RUSTC_DIRECT_COMPARISON.md:9-26`). That document closes with "No
Rust-equivalence claim is made" (`RUSTC_DIRECT_COMPARISON.md:36-38`). The paired
`tests/conformance/` suite automates the comparison; results are recorded in
`docs/libc/CPLUS_RUST_CONFORMANCE.md` when available.

### 5.2 The `_s` suffix alone is not safety

- An `_s` call with a **wrong caller-supplied `dmax`** is not protected: the
  checked function validates against the number the caller passed, not the
  true allocation (`safe_memory.h:41-45`, `safe_string.h:41-48`).
- `cplus_snprintf_s`/`sprintf_s` check `dest` against tracked bounds, but a
  foreign/stale pointer whose capacity is unknown is outside that check
  (`safe_format.h:27-30`).
- `cplus_printf_s` only rejects non-literal formats and `%n`; a literal format
  with mismatched arguments is still diagnosed by the compiler attribute, not
  made safe by the suffix (`safe_format.h:15-20`).
- Tier-1 wide `cplus_swprintf` keeps `%n` and is explicitly **not** the checked
  sibling (`WIDE_FORMAT_CONTRACT.md:32-40`).
- The unprefixed `strcpy_s(d, s)` / `memcpy_s(d, s, n)` macros silently drop
  `dmax` and infer it; with unknown capacity they fall back to the documented
  default-mode behaviour (`safe_string.h:68-75`, `safe_memory.h:86-93`).
- `strnlen_s` is a bounded *query*, not a guarantee that the string is
  terminated; `free_s` refusal is bounded by the recent-free cache
  (`safe_string.h:46`, `safe_alloc.h:33-37`).

### 5.3 Logging never changes semantics

Logging is opt-in, off by default, metadata-only, and diagnostic — it is not a
check and never grants safety:

- `cplus_libc_log` saves and restores `errno` on every path, including failed
  `write(2)`; it emits only `[CPLUS-LIBC] <api>: <msg>` and never buffer
  contents or secrets (`docs/LOGGING.md:31-61`).
- In `cplus_swprintf_checked`, `errno` is set **after** the log call precisely
  so an enabled logger cannot change the return value or `errno` — "logging ON
  and OFF are parity-equivalent" (`WIDE_FORMAT_CONTRACT.md:79-82`).
- Return values and allocation counts are asserted identical with logging ON
  and OFF in `tests/libc/test_libc_safe_api.c:53-59`,
  `tests/adversarial/logging_attack_NOTES.md:59-79` (A3, A7, A8), and the
  conversion/time method suites run every scenario twice with the logger OFF
  and ON (`tests/methods/hundred_conversion.c`, `tests/family_gaps/time_checked.c`).
- Honest caveat: the logging *path* itself is not async-signal-safe, and a
  closed-stderr episode can make it silently stop emitting
  (`logging_attack_NOTES.md:15-57`, `docs/LOGGING.md:63-73`). Neither behaviour
  alters a checked API's return value or `errno` contract; they are logging-path
  findings, not semantic changes to the checked calls.

---

## 6. Where the concrete evidence lives

| Family | Examples | Tests |
|---|---|---|
| Allocation | `examples/crt/malloc_s.c`, `calloc_s.c`, `realloc_s.c`, `free_s.c` | `tests/libc/test_alloc_metadata.c`, `test_provenance_attack.c`, `tests/crt/alloc.c` |
| Memory | `examples/crt/memcpy_s.c`, `memmove_s.c`, `memset_s.c`, `memcmp_s.c`, `memzero_s.c` | `tests/crt/memory_ops.c`, `memset_ops.c`, `tests/libc/test_memory_attack.c`, `test_libc_safe_api.c` |
| String | `examples/crt/strcpy_s.c`, `strncpy_s.c`, `strcat_s.c`, `strncat_s.c`, `strnlen_s.c` | `tests/crt/string_ops.c`, `strncpy_ops.c`, `tests/libc/test_string_extra*.c` |
| stdio/format | `examples/crt/fopen_s.c`, `fclose_s.c`, `fread_s.c`, `fwrite_s.c`, `printf_s.c`, `fprintf_s.c`, `sprintf_s.c`, `snprintf_s.c`, `vsnprintf_s.c`, `scanf_s.c`, `fscanf_s.c` | `tests/crt/format_ops.c`, `stdio_ops.c`, `printf_ops.c`, `test_safe_format.c`, `tests/adversarial/format_attack_NOTES.md`, `tests/adversarial/stdio_attack_NOTES.md` |
| Conversion | `examples/crt/strnlen_s.c` (bounded helper) | `tests/methods/hundred_conversion.c`, `tests/family_gaps/string_checked.c`, `tests/rust_vs_cplus/conversion_c.c` |
| Time/wide | — (no checked example; see tests) | `tests/libc/test_wchar_format.c`, `tests/family_gaps/time_checked.c`, `wchar_checked.c`, `tests/methods/hundred_time_fenv.c` |

Build for the examples: `examples/crt/README.md` and `scripts/build_safecrt.sh`;
run the checked suites with `tests/crt/run_safecrt_tests.sh` and
`tests/libc/run_libc_tests.sh`.

## 7. Primary source index

- Public ordinary surface: `libc/install-include/cplus_libc.h`
- Ordinary-name hardening: `libc/install-include/cplus_libc_safe.h`,
  `stdlib/safe/safe_c.h`, implementations `libc/src/string/bounded.c`
- Checked headers: `stdlib/safe/safe_alloc.h`, `safe_memory.h`, `safe_string.h`,
  `safe_io.h`, `safe_format.h`, `safe_error.h`, `safe_runtime.h`
- Checked implementations: `stdlib/safe/src/safe_alloc.c`, `safe_memory.c`,
  `safe_string.c`, `safe_format.c`, `safe_io.c`, `safe_runtime.c`;
  constraint codes `third_party/safeclib/include/safe_lib_errno.h`
- Contracts and limits: `docs/libc/ORDINARY_C_SAFETY.md`,
  `docs/libc/C_BUG_CLASS_MATRIX.md`, `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md`,
  `docs/libc/WIDE_FORMAT_CONTRACT.md`, `docs/libc/RUNTIME_RECOVERY.md`,
  `docs/SAFE_CRT.md`, `docs/CRT_MATRIX.md`, `docs/LOGGING.md`,
  `docs/libc/RUSTC_DIRECT_COMPARISON.md`, `tests/adversarial/*_NOTES.md`
- Compiler side (Agent 1): `compiler/Analysis/BoundsAnalysis.cpp`,
  `compiler/Analysis/OwnershipAnalysis.cpp`, `compiler/Sema/StaticBoundsCheck.cpp`,
  `compiler/Sema/RaiiAnalyzer.cpp`, `libc/contracts/compiler_libc_contracts.json`,
  `tests/compile_fail/`
