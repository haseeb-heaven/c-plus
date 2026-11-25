# Security implementation review — upstream techniques mapped to C+ stdlib

- **Branch / date:** `agent/modern-stdlib`, 2026-11-23.
- **Task:** Task 6 Phase 7 — security implementation review.
- **Ownership:** this document only. No header, source, build file, test, `third_party/`
  file, or git index entry was created or modified. No dependency was added.
- **Method:** read the *actual* upstream source/doc where reachable, then map each
  technique to one of three dispositions against the checked-in C+ stdlib.

Evidence labels:

- **[RE-FETCHED]** — fetched with `webfetch` during this task; URL and outcome recorded in §2.
- **[INHERITED]** — quoted from an already-committed upstream study in this repository
  (which records its own exact URLs/commits). Not re-fetched in this task; explicitly marked.
- **[OBSERVED-LOCAL]** — read from a checked-in file at `branch:path:line`.
- **[INFERENCE]** — my design reading, not an upstream claim.
- **[FETCH FAILED]** — recorded as a failure, never used as evidence.

Dispositions (the mapping requested by the task):

- **PRESENT** — already implemented in this C++ stdlib; exact `file:line` given.
- **ADAPTABLE** — can be added/changed without changing any ABI, pointer representation,
  function signature, or ownership contract; concrete note given.
- **OUT OF SCOPE (Agent 1)** — compiler/loader/runtime/sanitizer property; must not be
  reimplemented in `.hp` headers (see `docs/libc/AGENT1_COMPILER_GAPS.md`,
  `docs/stdlib/upstream_study/llvm_clang.md:674-742`).

---

## 1. Executive summary

Nothing in the upstream material justifies a pointer-layout, signature, or ABI change in
this repository. The strongest *adaptable* findings are contract/annotation work:

1. **ADAPTABLE — Clang Static Analyzer ownership annotations** (`ownership_returns`,
   `ownership_takes`, `ownership_holds`, `nonnull`, `analyzer_noreturn`) on the existing
   `cplus_*` allocation/free wrappers and the checked `_s` entry points. Declaration-only,
   compiled-out by `__has_feature`, zero ABI effect.
2. **ADAPTABLE — a single overridable termination/assertion seam with a pinned message
   format**, already functionally present as `stdlib_note` + `STDLIB_CATEGORY_*` +
   `stdlib_set_error_callback`; the missing piece is a *tier/format contract* tests can match.
3. **ADAPTABLE — `strlcpy`/`strlcat`-style "full destination size + truncation reported by
   return value, always NUL-terminate" discipline**, as a documented contract for the
   existing `_auto` bounded copy adapters (not a new libc dependency).
4. **ADAPTABLE — fail-closed secure erase with an anti-DSE barrier**, already present as
   `cplus_memset_explicit` / `cplus_memzero_s`; the gap is documenting which one is the
   secret-clearing API and wiring it where secrets are handled.
5. **ADAPTABLE — filesystem hardening already half-present:** `fs_mkdtemp` uses a bounded
   template check, but mode is `0777` for `fs_mkdir` and no `O_NOFOLLOW`/`O_CLOEXEC`/
   `mkstemp` equivalent exists. Adding a `0600`/atomic-create helper is signature-safe.

---

## 2. Source log — exact URLs, versions/commits, outcomes

### 2.1 Microsoft Secure CRT (documentation only — no Microsoft code fetched) [RE-FETCHED]

| # | Exact URL | Version / commit proof | Outcome |
|---|-----------|------------------------|---------|
| M1 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/security-features-in-the-crt` | front matter `git_commit_id: 298b991edd2f988905048523475cc67cbe017a5f`; source `docs/c-runtime-library/security-features-in-the-crt.md` | SUCCESS (text). `_s` suffix; parameter validation; sized buffers; NUL termination; enhanced `errno`; "format string syntax checking"; debug `0xFE` fill + `_CrtSetDebugFillThreshold` |
| M2 | `https://learn.microsoft.com/en-us/cpp/c-runtime-library/parameter-validation` | front matter `git_commit_id: b96b32beb7ec33569a43045c93e14e267b791c15`; source `docs/c-runtime-library/parameter-validation.md`; `original_content_git_url: https://github.com/MicrosoftDocs/cpp-docs-pr/blob/live/docs/c-runtime-library/parameter-validation.md` | SUCCESS (text). `_invalid_parameter` / `_invalid_parameter_noinfo` / `_invalid_parameter_noinfo_noreturn`; default `_invoke_watson`; handler may be replaced globally or thread-locally; continued execution → `errno` + `EINVAL`/`EBADF` |

Exact quotes captured (M1/M2):

> "The secure functions don't prevent or correct security errors. Instead, they catch errors when they occur."
> — M1

> "You must pass the buffer size to any secure function that writes to a buffer. The secure versions validate that the buffer is large enough before writing to it."
> — M1

> "If an invalid parameter is found, the invalid parameter handler is called."
> — M2

> "In debug builds, the invalid parameter macro usually raises a failed assertion and a debugger breakpoint before the dispatch function is called."
> — M2

Additional Microsoft pages consulted *in the committed study* [INHERITED]: `strcpy_s`,
`strncpy_s`, `truncate`, `fopen_s`, `fprintf_s`, `gets_s`, `secure-template-overloads`
— exact URLs and contracts in `docs/stdlib/upstream_study/secure_crt.md:17-32`.

### 2.2 LLVM sanitizers and Clang Static Analyzer [RE-FETCHED]

Pinned commit reused from the repository's existing LLVM provenance:
`424c2d9b7e4de40d0804dd374721e6411c27d1d1` (= `llvmorg-20.1.1`), resolved in
`docs/libc/C23_MATH_DEPENDENCY_CLOSURE.md:17-33`.

| # | Exact URL | Outcome |
|---|-----------|---------|
| L1 | `https://raw.githubusercontent.com/llvm/llvm-project/424c2d9b7e4de40d0804dd374721e6411c27d1d1/compiler-rt/include/sanitizer/common_interface_defs.h` | SUCCESS (HTTP 200). SPDX header quoted in §3. `__sanitizer_acquire_crash_state` ("Returns 1 on the first call, then returns 0 thereafter... ensure only one report is printed"), `__sanitizer_annotate_contiguous_container`, `...double_ended...`, `...copy_contiguous_container_annotations`, `...verify_*`, `...find_bad_address`, weak hook family |
| L2 | `https://clang.llvm.org/docs/analyzer/user-docs/Annotations.html` | SUCCESS. Source annotations: `nonnull`, `noreturn`, `analyzer_noreturn`, `ownership_returns`, `ownership_takes`, `ownership_holds`, plus Cocoa/Libkern retain-count attributes. Footer: "Copyright © 2007-2026, The Clang Team" |
| L3 | `https://clang.llvm.org/docs/analyzer/AnalyzerAnnotations.html` | **[FETCH FAILED]** HTTP 404. Correct path is L2. |

Other sanitizer interface headers (ASan/UBSan/LSan/TSan/HWASan/coverage) were studied in
`docs/stdlib/upstream_study/llvm_clang.md:41-50` [INHERITED]; key result: the public UBSan
header declares only `__ubsan_default_options` — UBSan checks are compiler-instrumented,
not callable by a stdlib.

### 2.3 OpenBSD security-oriented C APIs [RE-FETCHED]

Man pages are served as "OpenBSD-current" without a git commit; the page's own date is
recorded instead. Source files were read from the un-pinned `master` branch of
`openbsd/src` (RCS IDs quoted, so no commit is implied).

| # | Exact URL | Version proof | Outcome |
|---|-----------|---------------|---------|
| O1 | `https://man.openbsd.org/strlcpy.3` | page date "August 3, 2024" | SUCCESS. `strlcpy`/`strlcat` signatures, "guarantee NUL-termination if there is room", "`retval >= dstsize` → truncation", overlap UB, "conform to IEEE Std 1003.1-2024", first appeared OpenBSD 2.4, author Todd C. Miller |
| O2 | `https://raw.githubusercontent.com/openbsd/src/master/lib/libc/string/strlcpy.c` | `$OpenBSD: strlcpy.c,v 1.16 2019/01/25 00:19:25 millert Exp $` | SUCCESS. Full body + license notice (§3) |
| O3 | `https://man.openbsd.org/explicit_bzero.3` (served as BZERO(3)) | page date "October 12, 2017" | SUCCESS. "`explicit_bzero()` variant behaves the same, but will not be removed by a compiler's dead store optimization pass, making it useful for clearing sensitive memory such as a password"; OpenBSD extension; first appeared OpenBSD 5.5 |
| O4 | `https://raw.githubusercontent.com/openbsd/src/master/lib/libc/string/explicit_bzero.c` | `$OpenBSD: explicit_bzero.c,v 1.4 2015/08/31 02:53:57 guenther Exp $` | SUCCESS. Full body + "Public domain" notice (§3) |
| O5 | `https://man.openbsd.org/arc4random.3` | page date "September 29, 2019" | SUCCESS. ChaCha20 since OpenBSD 5.5; `arc4random_uniform` "avoids 'modulo bias'"; "always successful, and no return value is reserved to indicate an error" |
| O6 | `https://man.openbsd.org/mktemp.3` | page date "August 4, 2025" | SUCCESS. `mkstemp` creates mode `0600`, "avoids the race between testing for a file's existence and opening it"; `mkdtemp` mode `0700`; `mkostemp` supports `O_CLOEXEC`; `mktemp` explicitly unsafe (TOCTOU + guessable names) |
| O7 | `https://man.openbsd.org/open.2` | page date "October 5, 2026" | SUCCESS. `O_EXCL`, `O_NOFOLLOW` ("If last path element is a symlink, don't follow it"), `O_CLOEXEC`, `O_DIRECTORY`, `openat` with `AT_FDCWD` |
| O8 | `https://man.openbsd.org/pthread_mutexattr.3` | page date "June 7, 2025" | SUCCESS. `pthread_mutexattr_settype` / `gettype`, SUSv2; mutex attributes passed to `pthread_mutex_init` |

### 2.4 Fil-C (pointer validity / runtime safety) [RE-FETCHED + INHERITED]

| # | Exact URL | Outcome |
|---|-----------|---------|
| F1 | `https://fil-c.org/invisicaps.html` | SUCCESS this task. Confirms verbatim: "each pointer dynamically tracks what object in memory it should be allowed to access, and using that pointer to access any memory that is not that object is dynamically prohibited"; "preserving `sizeof(T*)` to be the same as what you'd expect — i.e. 8 bytes on 64-bit platforms"; lower/intval split; aux allocation; upper bound = lower on free with a free bit; "would allow for 32-bit pointers if Fil-C supported 32-bit systems" |
| F2 | full source log (14 entries, default branch `deluge`, `FILC_VERSION 687u`, HTTP 404 for root `LICENSE`) | **[INHERITED]** from `docs/stdlib/upstream_study/fil_c.md:18-41`; not re-fetched here |

### 2.5 Fetch failures (recorded, not used as evidence)

| Attempt | Result |
|---------|--------|
| `https://clang.llvm.org/docs/analyzer/AnalyzerAnnotations.html` | HTTP 404 — replaced by L2 |
| `https://raw.githubusercontent.com/MicrosoftDocs/cpp-docs-pr/live/LICENSE` | HTTP 404 — no Microsoft license file fetched; Microsoft pages treated as documentation only, no text copied into this repository |
| `https://raw.githubusercontent.com/pizlonator/fil-c/deluge/LICENSE` | HTTP 404 [INHERITED] |
| Fil-C recursive tree (>5 MB) and GitHub code search (HTTP 401) | [INHERITED] — see `fil_c.md:8,39,860-864` analogue in that document's §Failures |

---

## 3. License identifiers quoted from license files

No upstream source was copied into this repository for this task. These are the exact
identifiers/notices from the files actually fetched or from the committed license audit.

**LLVM (L1 and Clang docs) — `LICENSE.TXT` at the pinned commit** [INHERITED,
`docs/stdlib/upstream_study/llvm_clang.md:126-150`]:

> "The LLVM Project is under the Apache License v2.0 with LLVM Exceptions:"

and every fetched LLVM source header carries:

> `// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception`

This was re-confirmed directly on L1 in this task (first lines of
`common_interface_defs.h`). Identifier: **`Apache-2.0 WITH LLVM-exception`**.

**OpenBSD `strlcpy.c` (O2) — ISC-style permissive notice; no SPDX tag in the file** (verbatim):

> "Copyright (c) 1998, 2015 Todd C. Miller <millert@openbsd.org>
>
> Permission to use, copy, modify, and distribute this software for any
> purpose with or without fee is hereby granted, provided that the above
> copyright notice and this permission notice appear in all copies.
>
> THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES..."

Identifier: **ISC-style (notice quoted; no SPDX identifier present in file)**.

**OpenBSD `explicit_bzero.c` (O4) — public-domain dedication** (verbatim):

> "/* Public domain.
>  * Written by Matthew Dempsky.
>  */"

Identifier: **public domain** (as declared in the file).

**Fil-C [INHERITED]** — no root `LICENSE` (404); detected license file `LLVM-LICENSE.txt`
begins "The LLVM Project is under the Apache License v2.0 with LLVM Exceptions"; the
runtime/API sources `fugc.h`/`stdfil.h` carry a BSD-style 2-clause notice
"Copyright (c) 2024-2025 Epic Games, Inc. All Rights Reserved. / Copyright (c) 2026 Filip
Pizlo. All Rights Reserved." (`docs/stdlib/upstream_study/fil_c.md:328-359`).

**Repository's own licenses** [OBSERVED-LOCAL]: `LICENSE` (project), and vendored
`third_party/safeclib/COPYING` = MIT, `third_party/intel-safestringlib/LICENSE` = MIT
(quoted in `docs/stdlib/upstream_study/secure_crt.md:481-542` and `docs/UPSTREAM_LICENSES.md:5-17`).
No new dependency or license obligation is introduced by this review.

---

## 4. Technique map by family

### 4.1 Fil-C — pointer validity / runtime safety

| Technique (source) | Disposition | Basis |
|---|---|---|
| InvisiCaps capabilities, per-access bounds/liveness traps | **OUT OF SCOPE (Agent 1)** | F1: needs modified LLVM IR pass, runtime, loader, whole-program compile. `fil_c.md:252-291`; this repo's headers cannot tag pointers. |
| Deterministic UAF via upper=lower + free bit; free-singleton repointing | **OUT OF SCOPE (Agent 1)** | Runtime/GC policy; `fil_c.md:168-194`. |
| `zgc_*` / GC finalizers / weak refs | **OUT OF SCOPE (Agent 1)** | Whole-program GC; `fil_c.md:50-87,202-206`. |
| Named validity queries `zhasvalidcap` / `zlength` / `zvalinbounds` / `zcheck` | **ADAPTABLE (contract only)** | Already convergent with `stdlib_alloc_length` / `query_length` / `generation` / `check_generation` (`stdlib/error.hp:513-551`) and checked accessors (`stdlib/vector.hp:155-175` per prior study). Define a documented "check-before-use" protocol; no pointer change. |
| Overflow-checked allocation (`zchecked_add`/`zchecked_mul`) | **PRESENT** | `stdlib/error.hp:57-67` (`STDLIB_ALLOC_LIMIT`), `:394-412` (calloc guard), `vector.hp:111-145`. |
| Tombstone + generation liveness token | **PRESENT (weaker, diagnostic)** | `STDLIB_META_TOMB` `error.hp:254`, `stdlib_meta_forget` `:348-355`, `stdlib_transfer_ownership` `:558-571`. Cannot invalidate copied raw pointers — must be stated openly. |
| Uncatchable `zsafety_error` panic | **OUT OF SCOPE (Agent 1)** and contrary to local policy: `error.hp` deliberately delegates termination to `cplus_fatal` (prior study `fil_c.md:244-248`). | — |

### 4.2 Microsoft Secure CRT — parameter validation / bounded APIs

| Technique (source) | Disposition | Basis |
|---|---|---|
| `_s` suffix + destination capacity as an explicit argument | **PRESENT** | `stdlib/safe/safe_memory.h:41-52`, `safe_string.h:41-54`, `safe_format.h:60-76`; symbol prefixing to `cplus_sc_*` via `scripts/build_safecrt.sh:38` [INHERITED]. |
| Global + thread-local invalid-parameter handler, safe default | **PRESENT** | `stdlib/safe/src/safe_error.c:6-24` selects abort/ignore handlers and installs both constraint-handler families [INHERITED]; local `stdlib/safe/src/safe_except.c:10` / `safe_runtime.c:13` define `CPLUS_TLS` as `__thread`. |
| Debug `0xFE` destination fill before validation | **OUT OF SCOPE (Agent 1)** — debug-CRT build feature, not a header contract; adding it to release headers would change observable buffer contents. | — |
| Fail-closed destination clear + distinct `ES*` taxonomy on every violation | **PRESENT (vendored safeclib)** | `third_party/safeclib/src/safeclib_private.h:312-588`, `safe_lib_errno.h:55-123` [INHERITED]. |
| Bounded object size overriding caller `dmax` (`destbos`) | **PRESENT in wrappers** | `CPLUS_SAFE_OBJ_SIZE(dest)` macro in `stdlib/safe/safe_c.h:83-103`, `safe_string.h:70-102`, `safe_memory.h:88-108`. |
| `_TRUNCATE` / `STRUNCATE` "truncated but successful" status | **ADAPTABLE, but do not change existing return semantics** — C+ currently returns failure on truncation; `docs/stdlib/DEEP_IO_CONTRACT.md:§2` treats accepting a prefix as a silent behaviour change. If added, use a new named API. | — |
| Format-string validation + `%n` rejection + literal-only formats | **PRESENT** | `stdlib/safe/src/safe_format.c:117-145,160-162,175-177,189-192,205-223`; `CPLUS_SAFE_FMT_PRINTF/SCANF` at `safe_format.h:36-37`. |
| Mode-string grammar validation in `fopen_s` | **ADAPTABLE (docs/contract), not imported** — both M7 and vendored `fopen_s.c` [INHERITED] pass `mode` straight to `fopen`; `stdlib/file.hp:15-25` and `safe_io.c:124-151` accept any non-NULL mode. Document the grammar as a caller precondition; do not add a parser without a new API. | — |
| "Handler should save data then abort" default | **DELIBERATELY INVERTED** — `stdlib/error.hp:39-53` is recoverable; fatal safety violations belong to `cplus_fatal`. Keep. | — |

### 4.3 LLVM sanitizers (interfaces)

| Technique (source) | Disposition | Basis |
|---|---|---|
| `__sanitizer_annotate_contiguous_container` / `...copy_...` | **OUT OF SCOPE (Agent 1)** for this build; `docs/libc/ASAN_AUDIT.md:§2` explicitly decides not to poison. Pattern (library declares intent, runtime owns shadow, allocator opts out) is the right shape if suballocation is ever introduced — prior study `llvm_clang.md:847-855`. | — |
| `__sanitizer_acquire_crash_state()` single-report arbitration | **ADAPTABLE (telemetry discipline)** | `stdlib_note` (`error.hp:194`) + per-thread re-entrancy guard (`error.hp:96-100`, `196`) already drop recursive reports; a one-shot flag could be documented without new ABI. |
| `__ubsan_default_options` and all sanitizer runtime hooks | **OUT OF SCOPE (Agent 1)** | L1 + `docs/stdlib/upstream_study/llvm_clang.md:192-205,674-712`. |
| Overflow guards as subtraction from a precomputed max | **PRESENT** | `STDLIB_ALLOC_LIMIT` guards in `error.hp:57-67`, `vector.hp:116-145`. |
| `memset_explicit` (anti-DSE erase) | **PRESENT** | `libc/src/c23_upstream_small.c:72-104` (LLVM libc `llvmorg-20.1.1`, SPDX `Apache-2.0 WITH LLVM-exception`); host fallback `libc/src/c23_small_gaps.c:103-109`; declaration `libc/install-include/cplus_libc.h:221`. |

### 4.4 Clang Static Analyzer annotations [RE-FETCHED; NEW to this review]

Exact semantics from L2:

- "`nonnull` ... indicates that a function expects that a given function parameter is not a null pointer"; analyzer "extends this checking by using its deeper symbolic analysis".
- `analyzer_noreturn` is Clang-specific and "ignored by the compiler for the purposes of code generation"; usage gated by `__has_feature(attribute_analyzer_noreturn)`.
- `ownership_returns(malloc[, size_idx])` marks allocation-returning functions;
  `ownership_takes(malloc, idx)` marks deallocators; `ownership_holds(malloc, idx)` marks
  deferred owners — "releasing the held memory or passing it to another holding call is
  reported ... as an 'attempt to release non-owned memory'".

| Technique | Disposition | Basis / concrete adaptation |
|---|---|---|
| `__attribute__((format(printf/scanf,...)))` | **PRESENT** | `stdlib/safe/safe_format.h:36-37,60-76`; `libc/install-include/cplus_libc.h:128-129`. |
| `nonnull` on `_s` entry-point pointer parameters | **ADAPTABLE** | Declaration-only attribute macros guarded by `#if defined(__clang__)`; no signature/ABI change. Audit found **zero** first-party `nonnull`/`_Nonnull` uses (grep over `stdlib/`). |
| `ownership_returns` / `ownership_takes` / `ownership_holds` on `stdlib_alloc*` / `stdlib_release` / `stdlib_transfer_ownership` | **ADAPTABLE (highest value)** | These three attributes map one-to-one onto the existing liveness/ownership contract at `error.hp:348-362,450,513-571`. Annotation is inert when the analyzer is not run. |
| `analyzer_noreturn` / `noreturn` on `cplus_fatal` path | **OUT OF SCOPE (Agent 1)** for the fatal handler itself; **ADAPTABLE** only as an annotation on the existing declaration. | Termination policy is the runtime's (`error.hp` header comment). |
| Cocoa/Libkern retain-count attributes | **NOT APPLICABLE** | Objective-C / OSObject-specific (L2); no analogue in C+ stdlib. |
| Actually running `scan-build` / enabling analyzer checkers | **OUT OF SCOPE (Agent 1)** | Toolchain/build concern; this review executed no analyzer. |

### 4.5 OpenBSD security-oriented C APIs

| Technique (source) | Disposition | Basis / note |
|---|---|---|
| `strlcpy`/`strlcat`: full destination size, always NUL-terminate, return `strlen(src)` and expose truncation as `>= dstsize` | **ADAPTABLE as contract shape; PRESENT in spirit** | O1/O2. C+ has bounded `*_auto` adapters (`safe_string.h:51-54,70-98`) and a bounded `read_line` drain rule (`io.hp:27-48,52-120`), but its truncation is a failure return, not a length. **Do not add `strlcpy`/`strlcat` as a dependency**: `docs/libc/SAFECRT_INTEGRATION_PLAN.md:§5.2` explicitly excludes them (Darwin fortify macro conflict), and macOS already provides them. Document the invariant instead. |
| `explicit_bzero` — secure erase that survives dead-store elimination | **PRESENT** via `cplus_memset_explicit` (`libc/src/c23_upstream_small.c:72-104`) and `cplus_memzero_s` (`stdlib/safe/src/safe_memory.c:73-81`); **ADAPTABLE** naming/doc fix | O3/O4. Note the upstream implementation is just `memset` + a weak hook (O4), whereas the C+ port adds `noinline` + empty-asm memory barrier — the stronger of the two. Gap: `memzero_s` is catalogued as a normal checked-CRT member (`safe_memory.h:45`), not as the secret-clearing primitive. |
| `arc4random` / `arc4random_uniform` (modulo-bias-free, no error return) | **OUT OF SCOPE (platform dependency)** | O5. Adding it would add a platform dependency; the task forbids that. Keep as a future Agent-1/platform decision. |
| `mkstemp`/`mkdtemp`: atomic create, restrictive mode | **PARTIALLY PRESENT / ADAPTABLE** | O6. `fs_mkdtemp` already validates the `XXXXXX` template with `strnlen` and `size` (`stdlib/fs.hp:96-117`), but `fs_mkdir` uses `0777` (`:82-94`) and there is no `0600` atomic temp-file helper. A signature-safe `fs_mkstemp(char *tmpl, size_t size, error *err)` could mirror `fs_mkdtemp`. |
| `O_NOFOLLOW` / `O_CLOEXEC` / `O_EXCL` / `openat` | **ADAPTABLE (platform-gated)** | O7. No first-party use found in `stdlib/` (only `uv_fs_open(...,O_RDONLY,0)` at `async.hp:199`). Any addition must be `#ifdef`-gated and must not change existing signatures. |
| `pthread_mutexattr_settype` for error-checking/robust mutexes | **ADAPTABLE** | O8 + `stdlib/thread.hp:24,29-60` (`mutex` currently wraps `cplus_mutex_new()` with default attrs). Adding an attr-configured constructor overload would change the API; prefer documenting current default behaviour and exposing a *new* named helper if needed. |

---

## 5. Consolidated mapping table

| # | Technique | Disposition | Evidence / location |
|---|-----------|-------------|---------------------|
| 1 | Capability-based pointer validity (InvisiCaps) | OUT OF SCOPE (Agent 1) | F1; `fil_c.md:252-291` |
| 2 | Named liveness/size queries + checked accessors | PRESENT (strengthen docs) | `error.hp:513-551`; `vector.hp:155-175` [prior study] |
| 3 | Generation/tombstone liveness token | PRESENT (diagnostic only) | `error.hp:254,348-362,450,513-571` |
| 4 | Overflow-safe size arithmetic | PRESENT | `error.hp:57-67`; `vector.hp:111-145` |
| 5 | Checked bounded `_s` memory/string/format APIs | PRESENT | `safe_memory.h:41-52`; `safe_string.h:41-54`; `safe_format.h:36-37,60-76` |
| 6 | Global + thread-local constraint handler | PRESENT | `safe_error.c:6-24` [prior study]; `safe_except.c:10` |
| 7 | Bounded-object-size (`destbos`) guard with fail-closed clear | PRESENT | `safe_c.h:83-103`; vendored safeclib [prior study] |
| 8 | Format-literal gate + `%n` rejection | PRESENT | `safe_format.c:117-145,160-223` |
| 9 | Secure erase surviving DSE | PRESENT (+ doc fix) | `c23_upstream_small.c:72-104`; `safe_memory.c:73-81` |
| 10 | Drain-to-delimiter on bounded line input | PRESENT | `io.hp:27-48,52-120`; `file.hp:55-125` |
| 11 | Clang `format` attributes | PRESENT | `safe_format.h:36-37`; `cplus_libc.h:128-129` |
| 12 | Clang analyzer `ownership_*` / `nonnull` annotations | ADAPTABLE (top) | L2; no first-party `nonnull` found by grep |
| 13 | One overridable termination seam + pinned message format | PARTIALLY PRESENT / ADAPTABLE | `error.hp:96-100,194`; prior study `llvm_clang.md:825-835` |
| 14 | `strlcpy`/`strlcat` full-size + truncation-by-return contract | ADAPTABLE (no new dependency) | O1/O2; `SAFECRT_INTEGRATION_PLAN.md:§5.2` excludes import |
| 15 | Atomic temp create with restrictive mode | PARTIALLY PRESENT / ADAPTABLE | O6; `fs.hp:96-117` (dir only), `:82-94` (`0777`) |
| 16 | `O_NOFOLLOW` / `O_CLOEXEC` / `openat` | ADAPTABLE (platform-gated) | O7; no first-party use |
| 17 | Error-checking mutex attributes | ADAPTABLE (new helper only) | O8; `thread.hp:24,29-60` |
| 18 | Single-report crash-state arbitration | ADAPTABLE (telemetry) | L1; `error.hp:96-100,194` already guards recursion |
| 19 | Debug `0xFE` buffer fill | OUT OF SCOPE (Agent 1 build feature) | M1 |
| 20 | ASan/UBSan/TSan runtime APIs, GC, capabilities, panics | OUT OF SCOPE (Agent 1) | L1 + `llvm_clang.md:674-742`; F1; M2 default handler |
| 21 | `_TRUNCATE`/`STRUNCATE` opt-in success-on-truncation | ADAPTABLE (new API only; do not change existing returns) | M1/M6 prior study; `DEEP_IO_CONTRACT.md:§2` |
| 22 | `arc4random*` secure randomness | OUT OF SCOPE (new platform dependency) | O5 |

---

## 6. Top adaptable techniques (ranked, no ABI/pointer changes)

1. **Clang Static Analyzer ownership annotations on existing allocators/handles.**
   Add `#if defined(__clang__)`-gated macros for `ownership_returns`, `ownership_takes`,
   `ownership_holds`, and `nonnull` to the declarations at `error.hp:348-362,513-571`
   and `stdlib/safe/safe_memory.h:41-52` / `safe_string.h:41-54`. These are declaration
   attributes only: no layout, signature, or ownership-semantics change. Directly
   expresses the liveness/ownership protocol the stdlib already implements. (L2)

2. **Pin the single diagnostic/termination seam's message format and add a tier.**
   `stdlib_note` + `STDLIB_CATEGORY_*` + `stdlib_set_error_callback` (`error.hp:39-53,
   96-100,194`) is already the C+ analogue of `_LIBCPP_ASSERT` → `_LIBCPP_VERBOSE_ABORT`.
   The adaptable delta is a documented, greppable message format (as libc++'s death-test
   regex does, prior study `llvm_clang.md:558-572`) and a compile-time check tier — not
   new runtime machinery, and not a fake `[CPLUS-*]` compiler diagnostic.

3. **Adopt `strlcpy`/`strlcat` contract discipline in documentation, not code.**
   State for the `*_auto` bounded adapters (`safe_string.h:51-54,70-98`) that the caller
   supplies the *full* destination size, NUL termination is guaranteed when it fits, and
   truncation is observable (currently via failure return, unlike O1's returned length).
   Explicitly do **not** vendor the functions — dependency forbidden and Darwin fortify
   conflict already recorded.

4. **Unify and document the secure-erase story.**
   `cplus_memset_explicit` (`c23_upstream_small.c:72-104`) already has noinline + empty-asm
   barrier (stronger than O4's `memset` + weak hook); `cplus_memzero_s`
   (`safe_memory.c:73-81`) is the checked-CRT wrapper. Declare one of them *the* secret
   wiping primitive in `stdlib/safe/README.md`-style docs and route any new secret-bearing
   code through it. No API change.

5. **Filesystem hardening with signature-safe additions only.**
   Mirror `fs_mkdtemp`'s validated-template pattern (`fs.hp:96-117`) for a new
   `0600`/`O_EXCL`/`O_NOFOLLOW` temp-file helper (O6/O7), and document that `fs_mkdir`'s
   `0777` (`:82-94`) is umask-moderated. Do not change existing function signatures.

---

## 7. Non-claims, limitations, and honesty notes

1. This is a **source study**, not an implementation. No header, build file, test, or
   binary was changed; no build, `ctest`, harness, live-game, or sanitizer run was executed.
2. No upstream source text was copied into this repository. Quoted notices (§3) are
   attribution/provenance records only.
3. Microsoft material is **documentation only**; no Microsoft source or license file was
   obtained (`live/LICENSE` returned 404).
4. OpenBSD sources O2/O4 were fetched from un-pinned `master`; their RCS IDs are quoted
   instead of a commit hash. Man pages carry only "OpenBSD-current" plus a page date.
   No commit is invented.
5. Fil-C detail beyond F1 is **[INHERITED]** from a committed study that already recorded
   its own URLs, `deluge` branch, `FILC_VERSION 687u`, and fetch failures; it was not
   re-verified in this task.
6. Clang analyzer checkers were **not executed**; the annotations are described from L2.
7. `docs/stdlib/agent2_audit/...` paths named in older task briefs do not exist on this
   branch; where this document needed those findings it cited the committed studies
   (`docs/stdlib/upstream_study/*.md`, `docs/libc/ASAN_AUDIT.md`) instead.
8. Presence claims above are source-reading claims (header/implementation inspected),
   **not proof of runtime memory safety**. With sanitizers unavailable in this toolchain
   (`docs/stdlib/upstream_study/llvm_clang.md:651-670`), OOB/UAF classes are not ruled out.
