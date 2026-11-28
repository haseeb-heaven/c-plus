# Safer Upstream Libraries Study & Integration Record

This document records the upstream projects evaluated for Agent 2 memory safety, exact vendored commits, reused components, local safety fixes, and boundaries.

| Project | Pinned Commit | License | Integration Status | Exact Scope & Local Modifications |
|---|---|---|---|---|
| **rurban/safeclib** | `90b076e94c4b0fb202dddf948d5d620e5395c1b0` | MIT (`third_party/safeclib/COPYING`) | **Linked primary backend** (`libcplus_safecrt.a`) | Selected mem/str subset (`memcpy_s`, `memmove_s`, `memset_s`, `memcmp_s`, `memzero_s`, `strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`, `strcmp_s`, `strnlen_s`, `strtok_s`, `strerror_s`, constraint handlers) built via `scripts/build_safecrt.sh` with `-D__STDC_WANT_LIB_EXT1__=0`, per-symbol `-D...=cplus_sc_*` prefix defines and `cplus_sc_*` wrappers (the older `-DCPLUS_SAFECLIB_BUILD` note was inaccurate: that flag is not passed by the current script). Local fix in `src/str/strtok_s.c` (trailing token `*ptr = dest` update + empty remaining input `NULL` return) and `cplus_sc_wrappers.c` (`cplus_sc_strtok_s`). |
| **intel/safestringlib** | `f4491915cc682556ed64d4eb6a2461d8eed2eb0c` | MIT (`third_party/intel-safestringlib/LICENSE`) | **Vendored reference only (not linked)** | Audited against `safeclib`; excluded from `build_safecrt.sh` so two competing backends never link simultaneously for the same API. Retained for provenance/reference. |
| **musl libc** | Audited (`libc/upstream/musl/PROVENANCE.md`) | MIT | **Selective primitives reused** (`libcplus_hardened.a`) | Selective memory, string (`musl_extra.c`), and integer conversion primitives integrated into `libc/src/` under `cplus_*` names without replacing host OS libc/CRT. Local fix in `musl_extra.c` pointer subtraction cast. |
| **llvm-project/libc** | Studied (reference) | Apache-2.0 with LLVM-exception | **Study reference (not bulk-imported)** | Studied contract validation (`LIBC_COPT_*` bounds/precondition patterns) and differential test methodology; mirrored in `tests/libc/test_differential_libc.c` against host libc without importing LLVM build infrastructure. |
| **rurban/ctl** | `ede5f455392f2380dd16aead26835eece6cd20e4` | MIT (`third_party/ctl/LICENSE`) | **Header-only C containers** | Audited ownership, allocation failure, underflow, and overflow. Hardened `third_party/ctl/ctl/vector.h` (`pop_back` empty guard, `fit` multiplication overflow check, `realloc` failure pointer preservation) and verified with `tests/ctl/run_ctl_tests.sh`. |
| **thradams/cake** | Studied (design reference) | MIT | **Design reference** | Studied explicit owner/view/nullable pointer annotations and ownership-transfer rules; applied in `cplus_transfer_ownership`, `cplus_safe_transfer_ownership`, `stdlib_transfer_ownership`, `file_borrow`, and `vector::slice` generation invalidation. Static compiler flow checks belong to Agent 1. |
| **checkedc/checkedc** | Studied (design reference) | MIT / LLVM-exception | **Design reference** | Studied bounds-aware `_Array_ptr` / `count(n)` contracts; applied as runtime metadata (`byte_size`, `elem_size`, `elem_count`, `generation`), `cplus_safe_query_length`, `cplus_alloc_query_length`, `cplus_safe_len`, and `cplus_alloc_len`. |
| **exceptions4c** | Pinned (`third_party/exceptions4c/PROVENANCE.md`) | **Apache-2.0** (`LICENSE`, `NOTICE`) | **Linked for C try/catch helpers** | Fixed C99 `(void)` prototype in `exceptions4c.h` and TLS compatibility in `stdlib/safe/src/safe_except.c`; tested via `tests/exceptions/run_except_tests.sh`. |
| **yyjson** | Pinned (`third_party/yyjson/`) | MIT | **Linked for `stdlib/json.hp`** | Tested via `tests/json/run_json_tests.sh`, `tests/stdlib/json_test.cp`, and `tests/stdlib/fuzz/fuzz_json.cp`. |

All upstream licenses, copyright notices, and provenance files are preserved intact; no host OS libc or CRT symbols are replaced globally.

## Upstream Licenses & Provenance

The full integration audit and security/ownership evaluation are recorded in
the table above; this section summarizes the pinned upstream licenses.

### 1. rurban/safeclib - primary linked checked CRT backend

- Upstream: <https://github.com/rurban/safeclib>
- Commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT (Cisco 2012-2013, Reini Urban 2017; `third_party/safeclib/COPYING` preserved)
- Local path: `third_party/safeclib/`
- Linked via `scripts/build_safecrt.sh` (`-D__STDC_WANT_LIB_EXT1__=0`, per-symbol `-D...=cplus_sc_*` prefixes, `cplus_sc_*` wrappers; `-DCPLUS_SAFECLIB_BUILD` is documented historically but not passed by the current script)
- Local fixes recorded in `third_party/safeclib/PROVENANCE.md` (`src/str/strtok_s.c`, `src/cplus_sc_wrappers.c`)

### 2. intel/safestringlib - vendored reference only, NOT linked

- Upstream: <https://github.com/intel/safestringlib>
- Commit: `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`
- License: MIT (`third_party/intel-safestringlib/LICENSE` preserved)
- Local path: `third_party/intel-safestringlib/`
- Status: excluded from `scripts/build_safecrt.sh`, so two competing backends never link simultaneously for the same API

### 3. musl libc - selective MIT primitives

- Upstream: <https://git.musl-libc.org/cgit/musl>
- Commit: `7813e9119eb5fe681b6750b8a033a0490bca0941`
- License: MIT (`libc/upstream/musl/PROVENANCE.md`)
- Scope: selected memory/string/numeric primitives in `libc/src/` (`libc/src/string/musl_extra.c`, `libc/src/stdlib/musl_stdlib_extra.c`)

### 4. rurban/ctl - vendored header-only C containers

- Upstream: <https://github.com/rurban/ctl>
- Commit: `ede5f455392f2380dd16aead26835eece6cd20e4`
- License: MIT (`third_party/ctl/LICENSE` preserved)
- Local path: `third_party/ctl/` (`ctl/vector.h` hardened for empty `pop_back`, overflow in `fit`, and `realloc` failure preservation)

### 5. exceptions4c, yyjson, stb, miniaudio, Nuklear

- exceptions4c: **Apache-2.0** (`third_party/exceptions4c/PROVENANCE.md`, `LICENSE`, `NOTICE`) - C99 `(void)` prototype fix in `exceptions4c.h` (the earlier "MIT" characterization was incorrect)
- yyjson (MIT), stb (MIT/PD), miniaudio (MIT-0/PD), Nuklear (MIT/PD): original third-party headers and licenses preserved

Platform CRTs (Apple libc, glibc, UCRT, musl) are linked through the system
toolchain only; they are never copied or globally replaced.

## End-to-end verification (this host, alpha-version)

| Upstream | Gate | Exact result |
|---|---|---|
| safeclib (linked CRT) | `sh tests/crt/unit/run_unit_tests.sh` | `crt unit tests: 106 passed, 0 failed` |
| safeclib / full CRT | `sh tests/crt/run_all_crt.sh` | `adversarial mitigations: 68 passed, 3 failed` - 3 pre-existing I/O-clamp contract conflicts in `stdlib/safe/src/safe_io.c`, unrelated to safeclib |
| intel/safestringlib | build-script audit | `scripts/build_safecrt.sh:127` excludes it; headers only in test include paths, no sources compiled |
| musl primitives | selective source audit | `libc/src/string/musl_extra.c`, `libc/src/stdlib/musl_stdlib_extra.c`, pinned SHA + MIT in provenance |
| rurban/ctl | `sh tests/ctl/run_ctl_tests.sh` | `ctl tests: 11 passed, 0 failed` (with `CPLUS_CC=/usr/bin/clang`) |
| exceptions4c | `sh tests/exceptions/run_except_tests.sh` | `exception tests: 2 passed, 0 failed` (with `CPLUS_CC=/usr/bin/clang CPLUS_CLANG=/usr/bin/clang`) |

| yyjson | `sh tests/json/run_json_tests.sh` | `json tests: 1 passed, 0 failed` (with `CPLUS_CC=/usr/bin/clang`); fuzz `FUZZ10K json_log 10000 0 0 0` |
| stb | `sh tests/stdlib/run_stdlib_tests.sh demo_image_demo` | **current failure**: `[CPLUS-ARITH] division by zero cannot be excluded` in `stdlib/image.hp:101,161,162` - warnings-as-errors |
| miniaudio | `sh tests/stdlib/run_stdlib_tests.sh demo_audio_demo` | `C+ stdlib tests: 1 passed, 0 failed, 0 skipped` |
| Nuklear | `sh examples/nuklear_gui/run.sh` | `NUKLEAR_GUI_TEST 7 7 0`, real SDL3 framebuffer BMP 1,920,138 bytes |

Environment caveat: the default `clang` on this host is Homebrew 23, which
forces a missing `MacOSX27.sdk` and makes the ctl/exceptions runners fail
before executing tests. With Apple clang 16 (`/usr/bin/clang`) both pass;
this is a host-toolchain issue, not a vendored-library defect.


Open current-state issues (not provenance defects): `json_test.cp` in
`--strict` mode hits `[CPLUS-FFI] arithmetic in a macro expansion cannot
be instrumented` (regression vs recorded strict PASS); `image_demo.cp` hits
the `[CPLUS-ARITH]` warnings above; and `tests/stdlib/method_tests/multimedia/run_all.sh`
prints `METHOD_TEST multimedia 127 127 0 0` but exits 1 because a stale
`build/ext/libnuklear.a` contains duplicate `_nk_*` symbols from two object
files. All four upstreams, their licenses and their facades are intact.

