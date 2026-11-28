# Upstream study: Microsoft Secure CRT + vendored safeclib (buffer/string/file hardening)

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **Role:** one of the parallel upstream-study agents. File ownership: this document only.
- **Honesty scope:** Microsoft material was consulted **as documentation only**. No Microsoft source
  code was fetched, inspected, vendored, derived from, or copied into this repository. Every
  implementation claim below refers to the *vendored* `third_party/safeclib/` and
  `third_party/intel-safestringlib/` trees that are actually on disk. No test runner was executed
  for this study; all counts are re-derived from named evidence files.

---

## 0. Sources consulted (exact URLs and exact paths)

### 0.1 Microsoft Learn pages fetched (documentation reference only)

| # | Exact URL | Contract captured |
|---|---|---|
| M1 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/security-features-in-the-crt` | `_s` suffix, parameter validation, sized buffers, NUL termination, enhanced `errno` reporting, filesystem security, format-string syntax checking; debug `0xFE` fill / `_CrtSetDebugFillThreshold` |
| M2 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/parameter-validation` | invalid-parameter dispatch (`_invalid_parameter`, `_invalid_parameter_noinfo`, `_invalid_parameter_noinfo_noreturn`); default `_invoke_watson` (mini-dump + close); `EINVAL` / `EBADF`; debug-build assertion before dispatch |
| M3 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/set-invalid-parameter-handler-set-thread-local-invalid-parameter-handler` | `_set_invalid_parameter_handler` / `_set_thread_local_invalid_parameter_handler` prototypes; handler signature `(const wchar_t *expression, const wchar_t *function, const wchar_t *file, unsigned int line, uintptr_t pReserved)`; parameters NULL in retail builds; handler "should save any data it can and then abort" |
| M4 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/strcpy-s-wcscpy-s-mbscpy-s` | `strcpy_s(dest, dest_size, src)`; `dest_size` in char units incl. NUL, `> 0` and `<= RSIZE_MAX`; `EINVAL` vs `ERANGE` table; dest zeroed on src-NULL / too-small; always NUL-terminates on success |
| M5 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/strncpy-s-strncpy-s-l-wcsncpy-s-wcsncpy-s-l-mbsncpy-s-mbsncpy-s-l` | `strncpy_s(dest, numberOfElements, src, count)`; `D = min(count, strlen(src))`, copy only if D + NUL fits else `strDest[0] = 0` + handler; returns `STRUNCATE` on truncation; no null-padding up to `count` |
| M6 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/truncate` | `_TRUNCATE` opt-in; `STRUNCATE` for `strncpy_s`/`strncat_s`/`wcstombs_s`/`mbstowcs_s`; `-1` for `_snprintf_s`/`vsnprintf_s`; "copy as much as fits, always append NUL, return success" |
| M7 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fopen-s-wfopen-s` | `errno_t fopen_s(FILE **pFile, const char *filename, const char *mode)`; NULL pFile/filename/mode → `EINVAL`, pFile unchanged; sets `*pFile = NULL` on failure; cannot share (`_fsopen`); mode table (`r,w,a,r+,w+,a+,x`), `t`/`b`, `c,n,N,S,R,T,D`, `ccs=UNICODE/UTF-8/UTF-16LE` with BOM precedence; leading `t`/`b` prefix fails |
| M8 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/fprintf-s-fprintf-s-l-fwprintf-s-fwprintf-s-l` | validates `stream` and `format` non-NULL **and the format string itself**; unknown/badly formed specifiers → invalid-parameter exception; returns `-1`, sets `errno = EINVAL` if execution continues; "Ensure that `format` is not a user-defined string" |
| M9 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/reference/gets-s-getws-s` | `gets_s(buffer, sizeInCharacters)`; reads to `\n`, replaces newline with NUL; NULL buffer / size `<= 0` / too-small → handler, returns NULL, `errno = ERANGE`; `fgets_s` retains the newline |
| M10 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/secure-template-overloads` | `_CRT_SECURE_CPP_OVERLOAD_STANDARD_NAMES`, `..._COUNT`, `_CRT_SECURE_CPP_OVERLOAD_SECURE_NAMES`; template overloads infer array length but only for static arrays |

404 attempts recorded for transparency (resolved by the URLs above / not used):
`.../crt-secure-library functions`, `.../invalid-parameter-handler`,
`.../reference/strncpy-s-wcsncpy-s-mbsncpy-s`, `.../crt-secure-library-functions`.

### 0.2 Local evidence files read

**Integration / inventory (counts):**
- `docs/libc/safeclib_inventory.json` (`counts`, `apis[437]`)
- `docs/libc/SAFECRT_INTEGRATION_PLAN.md`
- `tests/stdlib/agent2_audit/known_gaps.md` — **path correction:** the task named
  `docs/stdlib/agent2_audit/known_gaps.md`; the actual file on this branch is under `tests/`.
  The task also named `docs/libc/SAFECRT_INTEGRATION_PLAN.md`, which exists and was read.
- `docs/stdlib/DEEP_IO_CONTRACT.md`, `docs/stdlib/DEEP_FILE_CONTRACT.md`,
  `docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md`
- Module audits: `tests/stdlib/agent2_audit/module_audits/io.hp.md`,
  `tests/stdlib/agent2_audit/module_audits/file.hp.md`,
  `tests/stdlib/agent2_audit/module_audits/error.hp.md`

**C+ surfaces / adapters:**
- `stdlib/io.hp`, `stdlib/file.hp`, `stdlib/error.hp`
- `stdlib/safe/src/safe_io.c`, `stdlib/safe/src/safe_format.c`,
  `stdlib/safe/src/safe_error.c`, `stdlib/safe/src/safeclib_ext_wrappers.c`
- `stdlib/safe/safe_io.h`, `stdlib/safe/safe_ext_mem.h`, `scripts/build_safecrt.sh`

**Vendored upstream source actually inspected (see §6 for the full list).**

---

## 1. Secure CRT buffer-size validation and invalid-parameter policy

### 1.1 Microsoft contract (docs only)

M1/M2/M3 establish a three-part policy:

1. **Callers pass the destination capacity.** M1: "You must pass the buffer size to any secure
   function that writes to a buffer. The secure versions validate that the buffer is large enough
   before writing to it." M4: `dest_size` must be `> 0`, `<= RSIZE_MAX`, and must account for the
   terminating NUL.
2. **A violation is detected, not repaired.** M1: "The secure functions don't prevent or correct
   security errors. Instead, they catch errors when they occur." M4 error table: `dest == NULL` →
   `EINVAL`, dest not modified; `src == NULL` → `EINVAL`, `dest[0] = 0`; `dest_size == 0` or too
   small → `ERANGE`, `dest[0] = 0`.
3. **A pluggable handler is invoked.** M2: invalid parameter → `_invalid_parameter*` dispatch →
   default `_invoke_watson` (app closes, mini-dump). M3: global and **thread-local** handlers;
   the handler gets expression/function/file/line (NULL in retail) and "should save any data it
   can and then abort". If it returns, the CRT "normally stop[s] function execution, set[s]
   `errno` ..., and return[s] an error code" (`EINVAL`, or `EBADF` for a bad file pointer).

Debug builds additionally assert/break before dispatch; some functions fill the destination with
`0xFE` (M1/M4/M5), tunable via `_CrtSetDebugFillThreshold`. C++ template overloads (M10) can
infer array sizes automatically but **only for static arrays**, not heap buffers.

### 1.2 What the vendored safeclib actually does

The same policy is implemented in `third_party/safeclib/` with a different error vocabulary:

| Concern | Vendored implementation (exact path) |
|---|---|
| Central "check" macros | `third_party/safeclib/src/safeclib_private.h:312-588`: `CHK_DEST_NULL`, `CHK_DEST_MEM_NULL`, `CHK_DMAX_ZERO`, `CHK_DMAX_MAX`, `CHK_DMAX_MEM_MAX`, `CHK_DEST_OVR_CLEAR`, `CHK_DEST_MEM_OVR`, `CHK_SRC_NULL_CLEAR`, `CHK_SRC_OVR_CLEAR`, `CHK_SLEN_MAX_CLEAR` |
| Bounded-object-size (BOS) rather than blind trust in `dmax` | `memset_s.c:113-129` receives `_chk(..., const size_t destbos)`; if `destbos == BOS_UNKNOWN` it validates `dmax` against `RSIZE_MAX_MEM` + `BND_CHK_PTR_BOUNDS`, **otherwise it overrides `dmax = destbos`**; `strcpy_s.c:113-121` / `strncpy_s.c:127-143` do the equivalent for strings, with `_BOS_KNOWN(src)` / `_BOS_OVR_N` / `_BOS_CHK_N` checks on the source too |
| Error taxonomy | `third_party/safeclib/include/safe_lib_errno.h:55-123`: `ESNULLP=400`, `ESZEROL=401`, `ESLEMAX=403`, `ESOVRLP=404`, `ESNOSPC=406`, `ESLEWRNG=410`, `EOK=0`, `EINVAL=22` |
| Size ceilings | `third_party/safeclib/include/safe_types.h:26-32`: `RSIZE_MAX_STR = (4UL<<10)`, `RSIZE_MAX_MEM = RSIZE_MAX_STR*4`, `RSIZE_MAX = ~(rsize_t)0` (note: M4/M5 say C11 uses `RSIZE_MAX`, not the string-specific ceilings) |
| Handler dispatch | `third_party/safeclib/src/str/safe_str_constraint.c:40-158`: global `str_handler` + thread-local `thrd_str_handler` (`_Thread_local`/`__thread` when available); `invoke_safe_str_constraint_handler` prefers thread-local, then global, then `sl_default_handler`. Memory side mirrors this in `third_party/safeclib/src/mem/safe_mem_constraint.c` |
| Default handlers | `third_party/safeclib/src/abort_handler_s.c` prints `ABORT CONSTRAINT HANDLER: (%u) %s` then `slabort()`; `third_party/safeclib/src/ignore_handler_s.c` logs in DEBUG and returns |
| Overlap detection | `strcpy_s.c:123-197` and `strncpy_s.c:149-257` use an `overlap_bumper` pointer in both copy directions, returning `ESOVRLP` (M5 says overlap is *undefined*; safeclib detects it) |
| Fail-closed clear | `handle_error()` / `handle_str_bos_overflow()` in `safe_str_constraint.c:168-212`; `SAFECLIB_STR_NULL_SLACK` zeroes the whole remaining destination (optional) |

`memset_s.c:139-147` is notable: on `n > dmax` it invokes the handler, then **still writes the
truncated `n = dmax` bytes**, then `MEMORY_FLUSH` + `MEMORY_BARRIER`. The comment at lines 70-92
explains the intent (ordered stores, not optimized away; explicitly *not* a cache flush and not a
cold-boot defence). There is also an `_memset_s_uchk` fast path (lines 156-166) for compile-time
proven constraints.

---

## 2. String truncation / `dmax` contract

### 2.1 Microsoft (M4/M5/M6)

- `dmax`/`numberOfElements` is **the full destination capacity including the terminator**.
- Default behaviour on insufficient space is an **error**, not a truncation: `strDest[0] = 0`,
  handler invoked, `ERANGE`/`EINVAL`.
- `count` is a *character count*, not the buffer size. `D = min(count, src_len)`; copy succeeds
  only if `D` characters **plus NUL** fit. Example from M5: `strncpy_s(dst, 5, "a long string", 5)`
  fails because 5 chars leave no room for NUL.
- Opt-in truncation uses `_TRUNCATE`; success is reported as `STRUNCATE` for the `strn*` family,
  and as `-1` for `_snprintf_s`/`vsnprintf_s` (M6).
- Unlike plain `strncpy`, no zero-padding up to `count`; overlap is UB.

### 2.2 Vendored safeclib

`third_party/safeclib/src/str/strcpy_s.c` and `strncpy_s.c` follow the Annex K spirit but differ
in three material ways:

1. **No `_TRUNCATE`/`STRUNCATE` token.** `strncpy_s.c:114-117` takes `slen`, and on "not enough
   space" it calls `handle_error(..., ESNOSPC)` and zeroes the destination
   (`strncpy_s.c:259-266`) — it never returns a distinct "truncated but OK" status. Truncation is
   expressed by the caller passing a smaller `slen` (the `_TRUNCATE` equivalent in M6 is `size-1`).
2. **`dmax` can be corrected from object size.** With a known `destbos`, `dmax` is replaced by the
   true object size (`strcpy_s.c:118-120`), and a mismatched `dmax` yields `ESLEWRNG` only under
   `--enable-warn-dmax/--enable-error-dmax` (`safeclib_private.h:350-381, 448-458`).
3. **Source bound is checked too.** `strncpy_s.c:135-143` validates both source NULL and
   `slen > srcbos`, closing the "unterminated source" class that the C+ audit still lists as OPEN
   (see §7.3, `MEMSTR-27/29/30`).

`third_party/safeclib/src/io/gets_s.c:35-180` is the closest vendored analogue to `read_line`:
it validates `dest == NULL`, `dmax == 0`, `dmax > RSIZE_MAX_STR`/`> destbos`, then uses
`fgets(dest, dmax + 1, stdin)`, strips a trailing `\n`, and — crucially — **drains the rest of the
line before reporting `ESNOSPC`** when the line did not fit (comment lines 54-56). This is the same
"consume to delimiter so the next read stays aligned" rule that `stdlib/io.hp` implements.

---

## 3. File operations and mode validation

### 3.1 Microsoft (M7)

`errno_t fopen_s(FILE **pFile, const char *filename, const char *mode)`:

- NULL `pFile`, `filename`, or `mode` → `EINVAL`, `pFile` unchanged; the invalid-parameter handler
  is invoked for any of the three.
- On any failure `*pFile` is set to NULL; callers must check the return value.
- Cannot open for sharing (use `_fsopen`/`_wfsopen` with `_SH_DENYNO`).
- Mode grammar: base `r`, `w`, `a`, `r+`, `w+`, `a+`; C11 `x` suffix on `w`/`w+` = fail-if-exists;
  translation `t`/`b` (default from `_fmode`); a **leading** `t`/`b` makes the call fail;
  modifiers `c`, `n`, `N`, `S`, `R`, `T`, `D`; encoding `ccs=UNICODE|UTF-8|UTF-16LE` where a file
  BOM takes precedence over the `ccs` flag.
- `a`/`a+` always seek to end before writing; `a` does not remove EOF marker, `a+` does.

### 3.2 Vendored safeclib

`third_party/safeclib/src/io/fopen_s.c:83-120` implements `errno_t fopen_s(FILE **streamptr,
const char *filename, const char *mode)`:

- Explicit NULL checks on all three arguments → `invoke_safe_str_constraint_handler(..., ESNULLP)`
  and return `ESNULLP` (lines 87-103). On failure `*streamptr` is set to NULL by `fopen` itself
  (line 106), satisfying the M7 "NULL on failure" rule.
- On `fopen` failure it builds `errstr[128]` with `strerror`, **saves and restores `errno` around
  `strerror`** (macOS note at lines 110-114), invokes the handler, and returns the original
  `errno`.
- **It does not parse or validate the `mode` string.** There is no `x`/`ccs`/BOM/`t`-prefix logic;
  `mode` is passed straight to `fopen` (line 106). MinGW collisions are guarded at lines 42-45.

### 3.3 C+ `file.hp` and `safe_io.c`

- `stdlib/file.hp:15-25` constructs with `fopen(path, mode)` after only `path != NULL && mode !=
  NULL` checks; no mode-string validation, no `errno_t` return, no fail-if-exists/BOM/encoding
  handling, and no stream-registry interaction at construction.
- `stdlib/safe/src/safe_io.c:124-151` exposes `FILE *cplus_fopen_s(const char *name, const char
  *mode)` — a **different signature from both M7 and Annex K** (no `FILE **` out-parameter, no
  `errno_t`); it returns NULL and records stats/logs on failure. `cplus_fclose_s`,
  `cplus_fread_s`, `cplus_fwrite_s` are at lines 153-222; `stdlib/safe/safe_io.h:18-25` macro-maps
  the short `_s` names to these `cplus_*` symbols.

---

## 4. Formatted I/O hardening

### 4.1 Microsoft (M8)

- Validates `stream` and `format` for NULL and **validates the format string itself**: "If there
  are any unknown or badly formed formatting specifiers, these functions generate the invalid
  parameter exception." Continued execution → `-1` and `errno = EINVAL`.
- Warning: "Ensure that `format` is not a user-defined string" (CWE-134 remains the caller's job).
- M6 adds the return-value rule for truncated `_snprintf_s`/`vsnprintf_s` (`-1`).

### 4.2 Vendored safeclib

`third_party/safeclib/src/io/fprintf_s.c:70-113`:

- NULL `stream` / `fmt` → handler with `ESNULLP`, returns `-ESNULLP` (lines 76-85).
- **Explicit `%n` rejection**: scans with `strnstr(fmt, "%n", RSIZE_MAX_STR)` and ignores the
  `%%n` escape case by checking `*(p-1) != '%'` (lines 86-93) → handler with `EINVAL`, returns
  `-EINVAL`.
- Delegates to `safec_vsnprintf_s(...)` with `(rsize_t)-1` and an output-function wrapper
  (`safec_out_fchar`), then logs/forwards the underlying `errno` (lines 95-112).
- Header contract at lines 57-67 adds "None of the arguments corresponding to `%s` is a null
  pointer" and "No encoding error shall occur" as preconditions.
- Companion sources inspected: `third_party/safeclib/src/str/snprintf_s.c`,
  `third_party/safeclib/src/str/vsnprintf_s.c`, `third_party/safeclib/src/io/vfprintf_s.c`, and the
  `safec_scan.h` / `safec_vfscanf_s.c` scanner helpers.

Note the status: these format entry points are **ADAPTER_ONLY, not compiled from upstream** in
this repo (see §5); the `cplus_*` versions live in `stdlib/safe/src/safe_format.c`.

---

## 5. Which safeclib implementations are already integrated in this repo

### 5.1 Headline counts (from `docs/libc/safeclib_inventory.json` `counts`, corroborated by both named docs)

| Classification | Count | Meaning |
|---|---:|---|
| INTEGRATED | **40** | upstream `.c` enumerated in `scripts/build_safecrt.sh`, compiled with `-D` prefixes, archived in `build/safe/libcplus_safecrt.a`, declared in a header, with test evidence |
| ADAPTER_ONLY | **10** | `cplus_*` API exists in `stdlib/safe` but is an independent implementation; no upstream source compiled |
| NOT_INTEGRATED | **387** | upstream source vendored but not in the build-script allow-list (and/or fails the compile probe), no adapter calls it |
| UNAVAILABLE | **0** | header-declared name with no definition in vendored `src/` |
| **Total** | **437** | `40 + 10 + 387 + 0` |

Cross-check fields from the same JSON: `compiled_in_build_script: 40`, `with_cplus_adapter: 27`,
`unit_tested: 29`, `fuzz_tested: 11`. Family totals: `str=158, u8=110, wchar=83, mem=51, io=17,
os=8, handler=6, misc=4`. These are *distinct public names in vendored headers*, not an ISO C
conformance target.

`tests/stdlib/agent2_audit/known_gaps.md` §0 repeats the same SafeCRT headline:
> "SafeCRT: 387 of 437 distinct vendored public APIs NOT_INTEGRATED (40 INTEGRATED, 10
> ADAPTER_ONLY, 0 UNAVAILABLE); 188 upstream `.c` not compiled"
> — `docs/libc/IMPLEMENTATION_PLAN.md` §1.8; `docs/libc/COVERAGE.md`
>
> §(d) of the same audit: "SafeCRT vendored APIs not integrated | **387 of 437** | 40 INTEGRATED,
> 10 ADAPTER_ONLY, 387 NOT_INTEGRATED, 0 UNAVAILABLE; 18 upstream + 1 adapter sources compiled;
> 188 upstream `.c` not compiled".

`docs/libc/SAFECRT_INTEGRATION_PLAN.md` §1 reconciles the 40 INTEGRATED names down to **17 distinct
upstream implementation files**:

`abort_handler_s.c`, `ignore_handler_s.c`, `mem/safe_mem_constraint.c`,
`str/safe_str_constraint.c`, `mem/memcpy_s.c`, `mem/memmove_s.c`, `mem/memset_s.c`,
`extmem/memcmp_s.c`, `extmem/memzero_s.c`, `str/strcpy_s.c`, `str/strncpy_s.c`,
`str/strcat_s.c`, `str/strncat_s.c`, `str/strnlen_s.c`, `str/strerror_s.c`, `str/strtok_s.c`,
`extstr/strcmp_s.c`.

The main build-script loop also compiles `mem/mem_primitives_lib.c` (dependency-only, no public
API), giving 18 main-loop upstream sources; plus `extmem/timingsafe_bcmp.c` and
`extmem/timingsafe_memcmp.c` in a separate loop and `src/cplus_sc_wrappers.c` as the local adapter
— **21 files from `third_party/safeclib/src`** in total. The plan (§6.2) records that the
documented "188 uncompiled" is 2–3 high (precise figure 185–186).

The 10 ADAPTER_ONLY names are exactly: `fopen_s`, `fprintf_s`, `fscanf_s`, `printf_s`, `scanf_s`,
`snprintf_s`, `sprintf_s`, `sscanf_s`, `vsnprintf_s`, `vsprintf_s`.

### 5.2 Compile-probe / batch facts from the plan (§2)

- 206 upstream `.c` files on disk; **149 compile OK, 57 fail** under the generated config.
- Restricted to the 387 NOT_INTEGRATED API/source pairs: **261 OK / 126 FAIL**.
- **230 of 387** have an external dependency closure fully satisfied by already-compiled sources;
  **223** of those also have every source probe-OK.
- Hard-coded allow-list gate: add paths to `scripts/build_safecrt.sh` lines 102-124 (or lines 95-100
  for the unprefixed timingsafe pair), add `-D<name>=cplus_sc_<name>` entries to `SC_DEFS` (line
  38), close the dependency closure, add a `cplus_sc_*` wrapper + header declaration, then rebuild
  the archive. `stdlib/safe/CMakeLists.txt` does **not** consume the archive.
- Top-ranked low-risk candidates: `bsearch_s`, `qsort_s` (Annex K blockers),
  `memchr_s`/`memrchr_s`/`memccpy_s`, `strchr_s`/`strstr_s`/`stpcpy_s`, `vprintf_s`.
- Explicitly excluded: `strlcpy`/`strlcat` (Darwin fortify macro conflict), `tmpnam_s`
  (`SAFECLIB_ENABLE_UNSAFE`), and the four mis-classified timingsafe names.

### 5.3 Contradictions reported, not reconciled

`SAFECRT_INTEGRATION_PLAN.md` §6 notes: four timingsafe names carry a stray `status: INTEGRATED`
in the JSON while their `classification` is `NOT_INTEGRATED` (reclassifying would give 44/383);
the "188 uncompiled" figure omits the two compiled timingsafe files; compile-probe denominators
differ (149/206 vs 261/126 because one is per-source, the other per API/source pair).

---

## 6. Upstream safeclib source files actually inspected for this study

All paths are relative to the repository root; all exist on disk at the pinned commit
`90b076e94c4b0fb202dddf948d5d620e5395c1b0` (`third_party/safeclib/UPSTREAM_COMMIT`).

**safeclib implementation sources read in full or in relevant part:**

| Path | Why inspected |
|---|---|
| `third_party/safeclib/src/mem/memset_s.c` | `dmax`/`n` policy, memory barrier, `_uchk` fast path |
| `third_party/safeclib/src/str/strcpy_s.c` | overlap detection, null-slack, BOS-derived `dmax` |
| `third_party/safeclib/src/str/strncpy_s.c` | count/`slen` contract, source-bound check, `ESNOSPC` |
| `third_party/safeclib/src/io/fopen_s.c` | NULL triple-check, `errno` preservation, no mode grammar |
| `third_party/safeclib/src/io/fprintf_s.c` | `%n` rejection, `safec_vsnprintf_s` delegation |
| `third_party/safeclib/src/io/gets_s.c` | bounded line read, drain-before-error, `ESUNTERM`/`ESNOSPC` |
| `third_party/safeclib/src/str/safe_str_constraint.c` | global + thread-local handlers, BOS overflow helpers |
| `third_party/safeclib/src/abort_handler_s.c` | default abort policy |
| `third_party/safeclib/src/ignore_handler_s.c` | default ignore policy |
| `third_party/safeclib/src/safeclib_private.h` | `CHK_*` / `BOS_UNKNOWN` / `RCNEGATE` / `BND_CHK_PTR_BOUNDS` macros |
| `third_party/safeclib/include/safe_lib_errno.h` | `ES*` error-code values |
| `third_party/safeclib/include/safe_types.h` | `RSIZE_MAX_STR`, `RSIZE_MAX_MEM`, `RSIZE_MAX` |
| `third_party/safeclib/src/str/snprintf_s.c`, `.../vsnprintf_s.c`, `.../io/vfprintf_s.c` | formatted-output family (seen in tree; format behaviour cross-checked with `fprintf_s.c`) |
| `third_party/safeclib/src/mem/safe_mem_constraint.c`, `.../mem/mem_primitives_lib.c` | memory-side handler + primitives (seen in tree, referenced by `memset_s.c`/build script) |
| `third_party/safeclib/src/cplus_sc_wrappers.c`, `third_party/safeclib/src/str/strtok_s.c` | local adaptations recorded in `PROVENANCE.md` |

**Intel library inspected (present and catalogued, not compiled/linked):**
- `third_party/intel-safestringlib/safeclib/strcpy_s.c` — reads `src` with `strnlen_s(src, dmax)`
  precondition, `overlap_bumper` loop, `SAFECLIB_STR_NULL_SLACK`, returns the `ES*` taxonomy.
- `third_party/intel-safestringlib/safeclib/memset_s.c` — older/simpler shape: `errno_t
  memset_s(void *dest, rsize_t len, uint8_t value)`; `len == 0` → `ESZEROL` and **no stores**,
  no `destbos`, no memory barrier (contrast safeclib's `n == 0 → EOK` + barrier).
- Directory listing of all of `third_party/intel-safestringlib/{safeclib,include,unittests}/`;
  `CMakeLists.txt`, `README.md`, `PROVENANCE.md`, `LICENSE`, `rename_symbols.h`.
- `scripts/build_safecrt.sh` line 130 states verbatim: *"Intel safestringlib retained in
  third_party for provenance/reference only; not compiled/linked."*

---

## 7. Mapping to C+ `stdlib/io.hp`, `stdlib/file.hp`, `stdlib/error.hp` (with exact cited gaps)

### 7.1 `stdlib/io.hp::read_line` — mostly already hardened

`stdlib/io.hp:52-120` already implements the M9/safeclib `gets_s` behaviour in C+ terms:

- `buf == NULL || size == 0` → `STDLIB_FAIL` + `STDLIB_CATEGORY_INVALID` / `CPLUS_EXIT_IO`
  (cf. M4/M9 NULL guard, `ESNULLP`).
- `size > INT_MAX` → `STDLIB_CATEGORY_BOUNDS` before any `fgets()` (because `fgets` takes an `int`).
- `fgets(buf, (int)size, stdin)`, then a **peek at the next byte** to prove truncation; overlong
  lines are drained to the delimiter and reported `CPLUS_EXIT_BOUNDS` — the same rule as
  `gets_s.c:54-56` ("finishes reading and discarding ... before calling the constraint handler").
- `stdlib_io_token()` (lines 27-48) stores at most `size-1` bytes, always NUL-terminates, drains
  the token, and returns `2` on truncation (callers translate that to a recoverable failure).

Exact module-audit findings (`docs/stdlib/DEEP_IO_CONTRACT.md` §2):
- Scenario 1 (1-byte destination): **"no defect"** — only the terminating NUL is written inside the
  buffer; sentinel intact; non-empty line → `STDLIB_FAIL` / code 101 / `BOUNDS`.
- Scenario 2 (`size > INT_MAX`): **"no defect"** — guard runs before `fgets`, no input consumed.
- Scenario 3 (overlong token): **"no defect / not silent"**; "a direct caller that ignores the
  return code would see a truncated token, so the return value is part of the API contract".
- Scenario 4 (exact fit / CRLF): two defects **found and fixed** — exact-fit line falsely reported
  BOUNDS, and CRLF left an LF for the next read.

Residual gaps relevant here come from `tests/stdlib/agent2_audit/known_gaps.md` §(a)/§(b):
- `STDIO-04` MEDIUM — "stdio2.c/stdio3.c unvalidated delegates (NULL stream UB)".
- `STDIO-05` MEDIUM — "`fread_s`/`fwrite_s` trust `bmax` and silently clamp without `errno`"
  (contrast safeclib's explicit handler on constraint violation).
- `STDIO-07` GAP-COMPILER — "Checked-format literal gate reliability; v*/direct-call bypass and
  toolchain-dependent detection".
- `STDIO-08` GAP-COMPILER — "scanf_s Annex-K divergence (no rsize_t argument, width must be
  literal)".
- `STDIO-14` GAP-COMPILER — "`%n` parser does not recognise non-standard length prefixes (e.g.
  MS I64)" — note the vendored `fprintf_s.c` `%n` check is a plain `strnstr` and has the same
  class of limitation for I64-length specifiers.
- `STDIO-15` LOW — "`fmt_check_dest_bounds` pointer-into-object arithmetic can overstate
  capacity" (`stdlib/safe/src/safe_format.c:114`).
- `gets_s` itself is Annex K and NOT_INTEGRATED; the integration plan §3.4 marks it "red for
  `tmpnam_s` (deliberately avoided ...)" and notes base `gets` is UNSUPPORTED in the ISO inventory
  — so do **not** propose mass-importing `io/gets_s.c`.

### 7.2 `stdlib/file.hp` — mode validation and handle state are the gaps

`stdlib/file.hp:15-25` (constructor), `:29-52` (`read`), `:55-125` (`read_line`):

- The bounded `read_line` mirrors `io.hp` (NUL guard, `INT_MAX` guard, null-handle guard,
  truncation detection with a peek) and is exercised cross-family (CF4 `file/io read_line` per
  `known_gaps.md` §(f)).
- **Gap:** the constructor accepts any non-NULL `mode` string and calls raw `fopen`; there is no
  M7-style mode grammar (`r/w/a/+/x`, `t/b`, `ccs=`), no `errno_t`/out-param, and no BOM handling.
  The vendored `io/fopen_s.c` would fix only the three NULL checks and the `errno` discipline — **not**
  the mode grammar — because it too delegates to `fopen` unvalidated (§3.2).
- **Interface gap:** `cplus_fopen_s` in `stdlib/safe/src/safe_io.c:124` returns `FILE *` rather than
  `errno_t` with `FILE **`, so even the 10 ADAPTER_ONLY `fopen_s` name diverges from M7/Annex K.
  This is a wrapper-semantics decision, not new coverage (plan §3.3).
- Exact audit findings (`docs/stdlib/DEEP_FILE_CONTRACT.md:142`): *"close() clears the handle even
  when fclose fails..."*; `known_gaps.md` §(a): `STDIO-09` MEDIUM "freopen/fdopen do not reconcile
  the stream registry", `STDIO-10` MEDIUM "Default `_exit(70)` trap on a literal-gate false
  positive; temp-file cleanup not guaranteed". §(c) notes "no Windows ABI evidence" and macOS
  Annex K absent. §(d) records 2 `<stdlib.h>` APIs `bsearch_s`/`qsort_s` UNSUPPORTED and 13
  `<stdio.h>` UNSUPPORTED incl. Annex K macro names, `gets_s`, `tmpnam_s`.

### 7.3 `stdlib/error.hp` — handler policy mostly present, recursion/errno gaps remain

`stdlib/error.hp:35-58` defines `STDLIB_OK/EOF/FAIL`, stable categories `OK/INVALID/RANGE/IO/
MEMORY/BOUNDS/OWNERSHIP/LIFECYCLE/UTF8`, `STDLIB_ERROR_SIZE 256`, and the checked allocator limits.
It explicitly states "Nothing in this header terminates the process; fatal safety violations stay
the runtime's job (`cplus_fatal`)" — a deliberate inversion of M2/M3's default `_invoke_watson`,
and consistent with safeclib's `ignore_handler_s` default mode.

Already wired: `stdlib/safe/src/safe_error.c:6-24` selects `cplus_sc_abort_handler_s` or
`cplus_sc_ignore_handler_s` via `cplus_safe_set_constraint_mode()` and calls
`cplus_sc_set_mem_constraint_handler_s_wrapper` / `cplus_sc_set_str_constraint_handler_s_wrapper`
— i.e. safeclib's global + thread-local handler model is **already reused** (§8).

Exact audit findings:
- `known_gaps.md` §(a): `LIFE-10` MEDIUM — "Unbounded recursion between report/violation and the
  user handler"; `LIFE-11` MEDIUM — "safe_log/safe_stats not async-signal-safe; logger recursion can
  deadlock"; `LIFE-14` LOW — "errno plumbing incomplete/dead (`__cplus_errno_location` macro
  absent)".
- `docs/stdlib/DEEP_ERROR_VECTOR_CONTRACT.md §5`: "No fatal scenario is executed in this test"
  — fatal exact-exit-code verification is explicitly out of scope; the audit §0 counts 53
  `DOCUMENTED-CONTRACT` rows as inherent/caller-owned limitations, not fixes.
- Cross-check from `known_gaps.md` §(a): 46 OPEN wrapper-safety findings, 5 HIGH
  (`ALLOC-01/02/03`, `MEMSTR-07`, `MEMSTR-09`). The last two are directly relevant to this study:
  "cplus_strcpy has no capacity at all and writes no NUL on refusal" and "cplus_strcat has no
  capacity; scans dest then copies unbounded"; also `MEMSTR-27/29/30` (strlen on possibly
  unterminated buffers) — precisely the class safeclib's `CHK_SRC_OVR_CLEAR` closes.

### 7.4 Exact gaps from the per-module audits

`tests/stdlib/agent2_audit/module_audits/` contains a dedicated audit per `.hp` module. The
relevant "remaining untested paths and risks" sections state:

| Module audit (exact path) | Exact gap text / status | Relevance to this study |
|---|---|---|
| `tests/stdlib/agent2_audit/module_audits/io.hp.md` §7 item 1 | "the only IO-failure evidence is `file.hp`'s write-only-stream test ... an error/EOF misclassification on a real failed stream is not exercised for this module" | safeclib `fopen_s.c:105-117` explicitly separates `errno` from handler dispatch; C+ has no equivalent committed IO-failure fixture |
| `.../io.hp.md` §7 item 3 | "**Direct `stdlib_io_token` truncation misuse.** ... no fixture exercises a caller that ignores [return `2`] ... *Risk:* silent truncated-number consumption is a documented caller rule, not a tested library behaviour" | same opt-in-status principle as M6 `STRUNCATE` |
| `.../io.hp.md` §7 item 4 | "`size==1` CR followed by a non-LF byte ... no fixture uses a lone `CR` at the boundary" | stream-alignment edge that `gets_s.c`-style drain logic must preserve |
| `tests/stdlib/agent2_audit/module_audits/file.hp.md` table row `close` | "`fclose` returns nonzero → handle cleared regardless; `STDLIB_FAIL` ... [UNTESTED] ... known limitation `DEEP_FILE_CONTRACT.md:142-143`" | no `errno_t`-style failure contract at close (cf. M7) |
| `.../file.hp.md` table rows ctor | "`path==NULL || mode==NULL` → [DOCUMENTED-ONLY]"; invalid path/mode verified only via `badpath-open`,`badmode-open` (`DEEP_FILE_TASK2_CONTRACT.md:40`) | no Microsoft-style mode *grammar* validation; only `fopen` acceptance |
| `.../file.hp.md` §7 | "**[UNTESTED / DOCUMENTED-ONLY] — 7 items**" incl. `file_temp` failure and write/read invalid-arg branches | bounded-file error paths remain evidence-thin |
| `tests/stdlib/agent2_audit/module_audits/error.hp.md` §7 item 5 | "`reporting` is `static __thread` ... cross-thread callback behaviour and contention on `meta_lock` are not exercised by any committed fixture" | safeclib's thread-local handler (`safe_str_constraint.c:43-48`) is analogous; its thread-safety is not proven by these fixtures |
| `.../error.hp.md` §7 item 6 | "Compile-time gaps ... `error_use_after_reset` (ACCEPT), plus ... method calls on NULL/released `error *` ... are unchecked" | handler/category state lifetime not compiler-enforced |
| `.../error.hp.md` §7 item 7 | "**Fatal paths deliberately excluded**: ... passing a non-NUL-terminated buffer to `error_fail`" | same boundary as `DEEP_ERROR_VECTOR_CONTRACT.md §5` |

All of these are coverage gaps, not proven defects — the audits themselves say so ("each is a
coverage gap, not a proven defect"; "No item above is claimed safe by virtue of passing tests").

---

## 8. safeclib techniques: already reused vs. adoptable

### Already reused in this repo (evidence)
1. **Constraint-handler policy with global + thread-local handlers and abort/ignore defaults.**
   `stdlib/safe/src/safe_error.c` wires the vendored `abort_handler_s.c` / `ignore_handler_s.c` and
   both `set_*_constraint_handler_s` entry points; `safe_str_constraint.c` implements the
   thread-local preference.
2. **Symbol prefixing / namespacing of vendored code.** `scripts/build_safecrt.sh:38` `SC_DEFS`
   maps every public `_s` name to `cplus_sc_*`; the timingsafe pair is an intentional documented
   exception wrapped by `stdlib/safe/src/safeclib_ext_wrappers.c` because `BOS_OVR2` macros
   reference the same token.
3. **Drain-to-delimiter on bounded line input.** `io.hp`/`file.hp::read_line` reproduce the
   `gets_s.c` "finish reading, then report" contract.
4. **Explicit `%n` rejection and literal-format gating.** `stdlib/safe/src/safe_format.c:12-17,
   76-133,136-220` mirrors the vendored `fprintf_s.c` `%n` rule and adds CWE-134 literal checks.
5. **Compile-probe-before-integration discipline.** The inventory/plan document which sources
   compile under the generated config before any batch lands.

### Adoptable (techniques, not copied Microsoft code)
1. **Compile-time + runtime dual validation via bounded object size (`destbos`).** The `_chk`
   entry points override a caller-supplied `dmax` with the compiler-known object size and fall back
   to `BND_CHK_PTR_BOUNDS` when unknown (`memset_s.c:113-129`, `strcpy_s.c:113-121`). This directly
   addresses `MEMSTR-27/29/30` and `STDIO-15`.
2. **Fail-closed destination clearing + a distinct error taxonomy on every violation.** `ESNULLP`,
   `ESZEROL`, `ESLEMAX`, `ESNOSPC`, `EOVERFLOW`, `ESOVRLP`, `ESLEWRNG` with `handle_error()` zeroing
   the destination; `memset_s.c:139-147` still writes the safe prefix. Contrast C+'s
   "silent clamp" (`STDIO-05`) and `cplus_strcpy` writing no NUL on refusal (`MEMSTR-07`).
3. **Overlap detection in both copy directions** (`overlap_bumper` in `strcpy_s.c`/`strncpy_s.c`)
   for the bounded copy adapters, closing `MEMSTR-24`/`MEMSTR-25` and `ALLOC-12`.
4. **Robust `errno` discipline around diagnostic calls** (`fopen_s.c:110-114` saves/restores
   `errno` around `strerror`) — relevant to `LIFE-14`.
5. **Opt-in truncation with an explicit status** (`_TRUNCATE`/`STRUNCATE` from M6) if the C+
   contract ever wants a "truncated but successful" outcome; currently C+ uses a failure return,
   which `DEEP_IO_CONTRACT.md` §2 explicitly treats as required ("accepting the prefix would be a
   silent behaviour change").

### Top 3 reusable techniques (ranked)
1. **BOS-aware, fail-closed capacity validation** (`destbos` + `CHK_*` + destination clear + `ES*`
   codes) — highest leverage against the OPEN `MEMSTR-*` findings.
2. **Global + thread-local constraint-handler dispatch with a safe default** — already partially
   reused; extend to recursive-handler guard (`LIFE-10`) and errno discipline (`LIFE-14`).
3. **Bounded line/copy loops that drain or clear on overflow and always terminate** (`gets_s.c`,
   `strncpy_s.c`) — already the shape of `read_line`; reuse for bounded adapters rather than
   importing the Annex K names.

---

## 9. Licensing (quoted from the on-disk LICENSE files)

### 9.1 safeclib — `third_party/safeclib/COPYING` (MIT-style)

> "Safe C Library
>
> Copyright (C) 2012, 2013 Cisco Systems
> Copyright (C) 2017 Reini Urban
> All rights reserved.
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software
> and associated documentation files (the "Software"), to deal in the Software without
> restriction, including without limitation the rights to use, copy, modify, merge, publish,
> distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or
> substantial portions of the Software.
>
> THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING
> BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
> NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
> DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
> OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE."

`third_party/safeclib/PROVENANCE.md` adds: Upstream `https://github.com/rurban/safeclib`, commit
`90b076e94c4b0fb202dddf948d5d620e5395c1b0`, "License: MIT (see COPYING; Cisco 2012-2013, Reini
Urban 2017)". It also records three local adaptations: the `strtok_s.c` state fix + 5-arg adapter,
the **security patch** in `strnlen_s.c` (`while (*str && smax)` → `while (smax && *str)` after a
guard-page test found a one-byte over-read), and the verbatim timingsafe integration.

### 9.2 Intel safestringlib — `third_party/intel-safestringlib/LICENSE`

> "MIT License
>
> Copyright (c) 2014-2018 Intel Corporation
>
> Permission is hereby granted, free of charge, to any person obtaining a copy of this software
> and associated documentation files (the "Software"), to deal in the Software without
> restriction, including without limitation the rights to use, copy, modify, merge, publish,
> distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the
> Software is furnished to do so, subject to the following conditions:
>
> The above copyright notice and this permission notice shall be included in all copies or
> substantial portions of the Software. ..."

followed by the upstream Cisco notice:

> "Copyright (C) 2012, 2013 Cisco Systems
> All rights reserved.
>
> Permission is hereby granted, free of charge, ... THE SOFTWARE IS PROVIDED "AS IS", WITHOUT
> WARRANTY OF ANY KIND ..."

`third_party/intel-safestringlib/PROVENANCE.md`: Upstream `https://github.com/intel/safestringlib`,
commit `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`, "License: MIT (see LICENSE; includes
Cisco/MIT notice from upstream)", retained for provenance/reference only.

**Practical consequence:** both libraries are permissive MIT-style with attribution obligations
(copyright + permission notice in copies/substantial portions) and "AS IS" disclaimers. No copyleft
obligations. Microsoft Learn documentation is reference material only; no Microsoft code or
license terms were imported.

---

## 10. Non-claims

- No claim that Microsoft CRT code was copied — all Microsoft material is documentation reference.
- No claim that all 437 vendored APIs are needed for ISO C conformance (most are non-standard
  extensions; ISO C is tracked elsewhere in `docs/libc/iso_c_inventory.json`).
- No claim that integrating more `_s` sources yields Rust-grade memory safety; raw pointers,
  capacities and format strings remain caller responsibilities (53 `DOCUMENTED-CONTRACT` rows).
- No test execution, build result, ABI evidence, or live-game/Wine claim is made in this document.
- Counts are quoted from named inventories and audits; internal contradictions (§5.3) are reported,
  not silently reconciled.
