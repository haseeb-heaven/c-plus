# Intel safestringlib — Exhaustive Public API Inventory (Candidate Evaluation)

Generated 2026-11-22. This is an **inventory + recommendation only**: no source
files were created or modified outside this document and its companion JSON
(`docs/libc/intel_safestringlib_inventory.json`). The vendored Intel code is
evaluated as a **candidate component, not an automatic dependency**.

## Source of truth

- `third_party/intel-safestringlib/include/*.h` — `safe_lib.h`, `safe_mem_lib.h`,
  `safe_str_lib.h`, `snprintf_s.h`, `safe_lib_errno.h`, `safe_types.h`
- `third_party/intel-safestringlib/safeclib/*.c` / `*.h`
- `third_party/intel-safestringlib/unittests/*.c`
- `third_party/intel-safestringlib/PROVENANCE.md`, `LICENSE`, `README.md`,
  `CMakeLists.txt`, `rename_symbols.h`
- `scripts/build_safecrt.sh` — confirms the Intel sources are **NOT** compiled.

## Provenance and license (exact)

| Field | Value |
|---|---|
| Upstream | `https://github.com/intel/safestringlib` |
| Pinned commit | `f4491915cc682556ed64d4eb6a2461d8eed2eb0c` |
| License | MIT — `LICENSE`: "Copyright (c) 2014-2018 Intel Corporation", plus a Cisco Systems 2012-2013 MIT notice |
| Header SPDX | `SPDX-License-Identifier: MIT` on all headers/sources, with per-file copyrights (Bo Berry, Jonathan Toppins, Cisco Systems, Intel Corp) |
| Vendored by | Agent 2 (PR #17, issue #22), branch `agent/modern-stdlib` |
| SAFEC version declared | 1.3.0 (`SAFEC_VERSION_STRING`) |
| Upstream status | **ARCHIVED / read-only** — archived by owner 2026-07-20; discontinuation notice commit `68c2e2e` (2026-06-23). Upstream README: "THIS PROJECT IS ARCHIVED … Intel will not provide or guarantee development … maintenance, bug fixes, new releases or updates. Patches … are no longer accepted." |

Archival matters: a frozen, unmaintained dependency cannot receive security
fixes; every Intel API here is already covered by the actively vendored
rurban/safeclib backend, so there is no benefit that would justify the liability.

## Build integration (confirming Intel sources are NOT compiled)

`scripts/build_safecrt.sh`:

- Line 116 is explicit: `# Intel safestringlib retained in third_party for
  provenance/reference only; not compiled/linked.`
- The compile loop (lines 92–114) lists **only** `third_party/safeclib/src/**`
  sources (with `cplus_sc_*` `-D` prefixing) plus `cplus_sc_wrappers.c`.
- The archive command (line 125) packages only `sc_*.o`, `w_*.o`, `e4c_*.o` into
  `build/safe/libcplus_safecrt.a`. No Intel object is produced.
- `third_party/intel-safestringlib/CMakeLists.txt` defines a standalone
  `cplus_safestringlib` STATIC library over 16 vendored `.c` files with
  `-include rename_symbols.h` (`cplus_upstream_*` prefix). **No parent
  CMakeLists references it** (verified by grep), and it is not used by the
  build script. It is an unreferenced artifact.
- `safeclib/snprintf_support.c` is vendored but compiled by **neither** build
  path, so its four `inline` wrappers are unbuildable as shipped.

**Conclusion:** the linked checked backend is rurban/safeclib only; Intel is
reference-only. This matches `docs/libc/SAFER_UPSTREAM_INTEGRATION.md` and
`docs/libc/DEPENDENCIES.md`.

## Counts

| Metric | Count |
|---|---|
| Public function APIs enumerated | **73** |
| — from `include/safe_lib.h` | 2 |
| — from `include/safe_mem_lib.h` | 20 |
| — from `include/safe_str_lib.h` | 47 |
| — from `include/snprintf_s.h` | 4 |
| By category | memory 19, string 46, formatted output 4, constraint handler 4 |
| APIs with a vendored Intel implementation | 19 (15 build-capable `.c` + 4 `snprintf` inline definitions) |
| APIs declared only (no vendored `.c`) | 54 |
| APIs compiled by any project build | 0 |
| Upstream unit-test files vendored | 6 (covering 6 APIs) |
| APIs integrated | **0** |
| Types/macros inventoried | 17 |
| Duplicate — already integrated in C+ | 16 |
| Duplicate — available in vendored rurban/safeclib (not wrapped) | 51 |
| Unique vs rurban/safeclib | 2 (`wmemset_s`, `wcpcpy_s`) — both declaration-only |
| Intel-specific typed `snprintf` contracts | 4 |

## Complete API inventory

Legend — **Integrated?** is always `no`. **Dup class**: `I` = duplicate already
integrated; `S` = duplicate available in rurban/safeclib (not wrapped); `U` =
unique vs rurban/safeclib; `X` = Intel-specific contract. **Src?**
`impl` = vendored `.c`; `decl` = declaration-only; `inline*` = vendored `inline`
definition not compiled. The full per-field data (license, commit, limitation,
exact signature) is in the companion JSON.

### `include/safe_lib.h` — constraint handlers (2)

| Upstream name | Exact signature | C+ equivalent (safeclib-backed) | Src? | Unit test | Integrated? | Dup | Reason | License / commit | Known limitation |
|---|---|---|---|---|---|---|---|---|---|
| `abort_handler_s` | `void abort_handler_s(const char *msg, void *ptr, errno_t error);` | `cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_ABORT)` → safeclib `abort_handler_s` | impl (`abort_handler_s.c`) | — | no | I | Already linked via safeclib mode API | MIT (Toppins/Cisco/Intel 2018-2022) / `f4491915` | Calls `abort()`; unsuitable as a library default |
| `ignore_handler_s` | `void ignore_handler_s(const char *msg, void *ptr, errno_t error);` | `cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE)` (default) | impl (`ignore_handler_s.c`) | — | no | I | Already the C+ default mode | MIT (Toppins/Cisco/Intel 2018-2022) / `f4491915` | Silent; debug output only under `sldebug_printf` |

### `include/safe_mem_lib.h` — memory APIs (20)

| Upstream name | Exact signature | C+ equivalent | Src? | Unit test | Integrated? | Dup | Reason | License / commit | Known limitation |
|---|---|---|---|---|---|---|---|---|---|
| `set_mem_constraint_handler_s` | `constraint_handler_t set_mem_constraint_handler_s(constraint_handler_t handler);` | `cplus_safe_set_constraint_mode()` → `cplus_sc_set_mem_constraint_handler_s_wrapper` | impl (`safe_mem_constraint.c`) | — | no | I | Mode API already linked | MIT (Berry/Toppins/Cisco/Intel 2018) / `f4491915` | Single static handler, not thread-local |
| `memcmp_s` | `errno_t memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t slen, int *diff);` | `cplus_memcmp_s` → `cplus_sc_memcmp_s` → safeclib | impl (`memcmp_s.c`) | — | no | I | Primary duplicate | MIT (Berry/Cisco) / `f4491915` | `*diff` clamped to −1/0/1, not raw byte difference |
| `memcmp16_s` | `errno_t memcmp16_s(const uint16_t *dest, rsize_t dmax, const uint16_t *src, rsize_t slen, int *diff);` | rurban `memcmp16_s` (BOS-checked macro) | decl | — | no | S | Declared-only; safeclib covers it | MIT (header only) / `f4491915` | No vendored implementation |
| `memcmp32_s` | `errno_t memcmp32_s(const uint32_t *dest, rsize_t dmax, const uint32_t *src, rsize_t slen, int *diff);` | rurban `memcmp32_s` | decl | — | no | S | Declared-only; safeclib covers it | MIT (header only) / `f4491915` | No vendored implementation |
| `wmemcmp_s` | `errno_t wmemcmp_s(const wchar_t *dest, rsize_t dmax, const wchar_t *src, rsize_t smax, int *diff);` | rurban `wmemcmp_s` | decl | — | no | S | Declared-only; safeclib covers it | MIT (header only) / `f4491915` | No vendored implementation |
| `memcpy_s` | `errno_t memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t slen);` | `cplus_memcpy_s` → `cplus_sc_memcpy_s` → safeclib | impl (`memcpy_s.c`) | `test_memcpy_s.c` | no | I | Primary duplicate | MIT (Berry/Cisco) / `f4491915` | Zeroes dest on violation; rejects `slen==0`; pointer-range overlap check |
| `memcpy16_s` | `errno_t memcpy16_s(uint16_t *dest, rsize_t dmax, const uint16_t *src, rsize_t slen);` | rurban `memcpy16_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `memcpy32_s` | `errno_t memcpy32_s(uint32_t *dest, rsize_t dmax, const uint32_t *src, rsize_t slen);` | rurban `memcpy32_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `wmemcpy_s` | `errno_t wmemcpy_s(wchar_t *dest, rsize_t dmax, const wchar_t *src, rsize_t slen);` | rurban `wmemcpy_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `memmove_s` | `errno_t memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t slen);` | `cplus_memmove_s` → `cplus_sc_memmove_s` → safeclib | impl (`memmove_s.c`) | `test_memmove_s.c` | no | I | Primary duplicate | MIT (Berry/Cisco) / `f4491915` | Zeroes dest on violation; rejects `slen==0` |
| `memmove16_s` | `errno_t memmove16_s(uint16_t *dest, rsize_t dmax, const uint16_t *src, rsize_t slen);` | rurban `memmove16_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `memmove32_s` | `errno_t memmove32_s(uint32_t *dest, rsize_t dmax, const uint32_t *src, rsize_t slen);` | rurban `memmove32_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `wmemmove_s` | `errno_t wmemmove_s(wchar_t *dest, rsize_t dmax, const wchar_t *src, rsize_t slen);` | rurban `wmemmove_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `memset_s` | `errno_t memset_s(void *dest, rsize_t dmax, uint8_t value);` | `cplus_memset_s(dest, dmax, int value, rsize_t n)` — **4 args, not drop-in** | impl (`memset_s.c`) | `test_memset_s.c` | no | I | Signature diverges from Annex K/C+ | MIT (Berry/Cisco) / `f4491915` | Intel 3-arg vs 4-arg Annex K/C+ form |
| `memset16_s` | `errno_t memset16_s(uint16_t *dest, rsize_t dmax, uint16_t value);` | rurban `memset16_s` (4-arg macro) | decl | — | no | S | Declared-only; arg count differs | MIT (header only) / `f4491915` | No vendored implementation |
| `memset32_s` | `errno_t memset32_s(uint32_t *dest, rsize_t dmax, uint32_t value);` | rurban `memset32_s` (4-arg macro) | decl | — | no | S | Declared-only; arg count differs | MIT (header only) / `f4491915` | No vendored implementation |
| `wmemset_s` | `errno_t wmemset_s(wchar_t *dest, wchar_t value, rsize_t len);` | **none** — absent from rurban safeclib and stdlib/safe | decl | — | no | U | Unique but unimplemented; no C+ need | MIT (header only) / `f4491915` | Declaration-only; unusual `(dest, value, len)` order |
| `memzero_s` | `errno_t memzero_s(void *dest, rsize_t dmax);` | `cplus_memzero_s` → `cplus_sc_memzero_s` → safeclib | impl (`memzero_s.c`) | — | no | I | Primary duplicate | MIT (Berry/Cisco) / `f4491915` | Intel rejects `dmax==0`; C+ keeps zero-length defined |
| `memzero16_s` | `errno_t memzero16_s(uint16_t *dest, rsize_t dmax);` | rurban `memzero16_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `memzero32_s` | `errno_t memzero32_s(uint32_t *dest, rsize_t dmax);` | rurban `memzero32_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |

### `include/safe_str_lib.h` — string APIs (47)

| Upstream name | Exact signature | C+ equivalent | Src? | Unit test | Integrated? | Dup | Reason | License / commit | Known limitation |
|---|---|---|---|---|---|---|---|---|---|
| `set_str_constraint_handler_s` | `constraint_handler_t set_str_constraint_handler_s(constraint_handler_t handler);` | `cplus_safe_set_constraint_mode()` → wrapper | impl (`safe_str_constraint.c`) | — | no | I | Mode API already linked | MIT (Berry/Toppins/Cisco) / `f4491915` | Single static handler, not thread-local |
| `strcasecmp_s` | `errno_t strcasecmp_s(const char *dest, rsize_t dmax, const char *src, int *indicator);` | rurban `strcasecmp_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strcasestr_s` | `errno_t strcasestr_s(char *dest, rsize_t dmax, const char *src, rsize_t slen, char **substring);` | rurban `src/extstr/strcasestr_s.c` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strcat_s` | `errno_t strcat_s(char *dest, rsize_t dmax, const char *src);` | `cplus_strcat_s` → `cplus_sc_strcat_s` → safeclib | impl (`strcat_s.c`) | `test_strcat_s.c` | no | I | Primary duplicate | MIT (Berry/Cisco/Intel 2018-2022) / `f4491915` | Requires terminated dest (`ESUNTERM`); slack unspecified |
| `strcmp_s` | `errno_t strcmp_s(const char *dest, rsize_t dmax, const char *src, int *indicator);` | `cplus_strcmp_s` → `cplus_sc_strcmp_s` → safeclib | impl (`strcmp_s.c`) | `test_strcmp_s.c` | no | I | Primary duplicate (test added 2026-11-21) | MIT (Berry/Cisco/Intel 2018-2022) / `f4491915` | `*indicator` is raw `*dest - *src`, not clamped |
| `strcmpfld_s` | `errno_t strcmpfld_s(const char *dest, rsize_t dmax, const char *src, int *indicator);` | rurban `strcmpfld_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strcpy_s` | `errno_t strcpy_s(char *dest, rsize_t dmax, const char *src);` | `cplus_strcpy_s` → `cplus_sc_strcpy_s` → safeclib | impl (`strcpy_s.c`) | `test_strcpy_s.c` | no | I | Primary duplicate | MIT (Berry/Cisco) / `f4491915` | Only `dest[0]` nulled on error unless `SAFECLIB_STR_NULL_SLACK` |
| `stpcpy_s` | `char *stpcpy_s(char *dest, rsize_t dmax, const char *src, errno_t *err);` | rurban `stpcpy_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | Pointer-returning; no vendored implementation |
| `stpncpy_s` | `char *stpncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t smax, errno_t *err);` | rurban `stpncpy_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strcpyfld_s` | `errno_t strcpyfld_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);` | rurban `strcpyfld_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strcpyfldin_s` | `errno_t strcpyfldin_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);` | rurban `strcpyfldin_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strcpyfldout_s` | `errno_t strcpyfldout_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);` | rurban `strcpyfldout_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strcspn_s` | `errno_t strcspn_s(const char *dest, rsize_t dmax, const char *src, rsize_t slen, rsize_t *count);` | rurban `strcspn_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strfirstchar_s` | `errno_t strfirstchar_s(char *dest, rsize_t dmax, char c, char **first);` | rurban `strfirstchar_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strfirstdiff_s` | `errno_t strfirstdiff_s(const char *dest, rsize_t dmax, const char *src, rsize_t *index);` | rurban `strfirstdiff_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strisalphanumeric_s` | `bool strisalphanumeric_s(const char *str, rsize_t slen);` | rurban `strisalphanumeric_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | `bool` return; no vendored implementation |
| `strisascii_s` | `bool strisascii_s(const char *str, rsize_t slen);` | rurban `strisascii_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strisdigit_s` | `bool strisdigit_s(const char *str, rsize_t slen);` | rurban `strisdigit_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strishex_s` | `bool strishex_s(const char *str, rsize_t slen);` | rurban `strishex_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strislowercase_s` | `bool strislowercase_s(const char *str, rsize_t slen);` | rurban `strislowercase_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strismixedcase_s` | `bool strismixedcase_s(const char *str, rsize_t slen);` | rurban `strismixedcase_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strispassword_s` | `bool strispassword_s(const char *str, rsize_t slen);` | rurban `strispassword_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | Intel password constants (2 lower/upper, 1 digit/special, len 6–32) not enforced by C+ |
| `strisuppercase_s` | `bool strisuppercase_s(const char *str, rsize_t slen);` | rurban `strisuppercase_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strlastchar_s` | `errno_t strlastchar_s(char *str, rsize_t smax, char c, char **first);` | rurban `strlastchar_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | Out-param named `first` despite being last |
| `strlastdiff_s` | `errno_t strlastdiff_s(const char *dest, rsize_t dmax, const char *src, rsize_t *index);` | rurban `strlastdiff_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strljustify_s` | `errno_t strljustify_s(char *dest, rsize_t dmax);` | rurban `strljustify_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strncat_s` | `errno_t strncat_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);` | `cplus_strncat_s` → `cplus_sc_strncat_s` → safeclib | impl (`strncat_s.c`) | — | no | I | Primary duplicate | MIT (Berry/Cisco) / `f4491915` | Requires terminated dest; `slen==0` treated as success |
| `strncpy_s` | `errno_t strncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);` | `cplus_strncpy_s` → `cplus_sc_strncpy_s` → safeclib | impl (`strncpy_s.c`) | — | no | I | Primary duplicate | MIT (Berry/Cisco) / `f4491915` | Rejects `slen==0` (Annex K allows no-op) |
| `strnlen_s` | `rsize_t strnlen_s(const char *s, rsize_t smax);` | `cplus_strnlen_s` → patched rurban `strnlen_s` | impl (`strnlen_s.c`) | — | no | I | Primary duplicate; C+ already patched safeclib | MIT (Berry/Cisco) / `f4491915` | Intel loop `while (dmax && *dest)` is bounds-first (no over-read); C+ carried the equivalent fix locally |
| `strnterminate_s` | `rsize_t strnterminate_s(char *s, rsize_t smax);` | rurban `strnterminate_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | `rsize_t` return, not `errno_t` |
| `strpbrk_s` | `errno_t strpbrk_s(char *dest, rsize_t dmax, char *src, rsize_t slen, char **first);` | rurban `strpbrk_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | Non-const `char *src` |
| `strfirstsame_s` | `errno_t strfirstsame_s(const char *dest, rsize_t dmax, const char *src, rsize_t *index);` | rurban `strfirstsame_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strlastsame_s` | `errno_t strlastsame_s(const char *dest, rsize_t dmax, const char *src, rsize_t *index);` | rurban `strlastsame_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strprefix_s` | `errno_t strprefix_s(const char *dest, rsize_t dmax, const char *src);` | rurban `strprefix_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strremovews_s` | `errno_t strremovews_s(char *dest, rsize_t dmax);` | rurban `strremovews_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strspn_s` | `errno_t strspn_s(const char *dest, rsize_t dmax, const char *src, rsize_t slen, rsize_t *count);` | rurban `strspn_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strstr_s` | `errno_t strstr_s(char *dest, rsize_t dmax, const char *src, rsize_t slen, char **substring);` | rurban `strstr_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strtok_s` | `char *strtok_s(char *s1, rsize_t *s1max, const char *src, char **ptr);` | `cplus_strtok_s` / `cplus_sc_strtok_s` (locally fixed) | decl | — | no | I | C+ already integrated + fixed safeclib `strtok_s` | MIT (header only) / `f4491915` | C+ exposes a 5-arg adapter; signatures differ at C+ boundary |
| `strtolowercase_s` | `errno_t strtolowercase_s(char *str, rsize_t slen);` | rurban `strtolowercase_s` / `strlwr_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strtouppercase_s` | `errno_t strtouppercase_s(char *str, rsize_t slen);` | rurban `strtouppercase_s` / `strupr_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `strzero_s` | `errno_t strzero_s(char *dest, rsize_t dmax);` | rurban `strzero_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `wcpcpy_s` | `wchar_t *wcpcpy_s(wchar_t *dest, rsize_t dmax, const wchar_t *src, errno_t *err);` | **none** — absent from rurban safeclib and stdlib/safe | decl | — | no | U | Unique but unimplemented; no demonstrated need | MIT (header only) / `f4491915` | Declaration-only; pointer-returning wide copy |
| `wcscat_s` | `errno_t wcscat_s(wchar_t *dest, rsize_t dmax, const wchar_t *src);` | rurban `wcscat_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `wcsncat_s` | `errno_t wcsncat_s(wchar_t *dest, rsize_t dmax, const wchar_t *src, rsize_t slen);` | rurban `wcsncat_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `wcscpy_s` | `errno_t wcscpy_s(wchar_t *dest, rsize_t dmax, const wchar_t *src);` | rurban `wcscpy_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | Intel declares without `extern` |
| `wcsncpy_s` | `errno_t wcsncpy_s(wchar_t *dest, rsize_t dmax, const wchar_t *src, rsize_t slen);` | rurban `wcsncpy_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |
| `wcsnlen_s` | `rsize_t wcsnlen_s(const wchar_t *dest, rsize_t dmax);` | rurban `wcsnlen_s` | decl | — | no | S | Declared-only | MIT (header only) / `f4491915` | No vendored implementation |

### `include/snprintf_s.h` — typed formatted output (4)

| Upstream name | Exact signature | C+ equivalent | Src? | Unit test | Integrated? | Dup | Reason | License / commit | Known limitation |
|---|---|---|---|---|---|---|---|---|---|
| `snprintf_s_i` | `int snprintf_s_i(char *dest, rsize_t dmax, const char *format, int a);` | `cplus_snprintf_s` (variadic, format-checked, literal-only) — not signature-compatible | inline*, `snprintf_support.c` | — | no | X | Intel-specific fixed-arity contract; C+ stronger | MIT (Intel 2014) / `f4491915` | Partial parser (max 16 conversions); expects exactly one integer format; returns `-ESBADFMT`/`-ESFMTTYP`; prints diagnostics; not compiled |
| `snprintf_s_si` | `int snprintf_s_si(char *dest, rsize_t dmax, const char *format, char *s, int a);` | `cplus_snprintf_s` — not signature-compatible | inline*, `snprintf_support.c` | — | no | X | Intel-specific string+int wrapper | MIT (Intel 2014) / `f4491915` | Requires exactly 2 conversions; same partial-parser limits; not compiled |
| `snprintf_s_l` | `int snprintf_s_l(char *dest, rsize_t dmax, const char *format, long a);` | `cplus_snprintf_s` — not signature-compatible | inline*, `snprintf_support.c` | — | no | X | Intel-specific long wrapper | MIT (Intel 2014) / `f4491915` | Requires exactly one `FMT_LONG`; same limits; not compiled |
| `snprintf_s_sl` | `int snprintf_s_sl(char *dest, rsize_t dmax, const char *format, char *s, long a);` | `cplus_snprintf_s` — not signature-compatible | inline*, `snprintf_support.c` | — | no | X | Intel-specific string+long wrapper | MIT (Intel 2014) / `f4491915` | Requires exactly 2 conversions; same limits; not compiled |

`*` `inline` definition exists in a `.c` that neither `build_safecrt.sh` nor the
standalone Intel CMake target compiles — it is unbuildable as shipped.

Note: the Intel headers declare **no** generic `snprintf_s`, `sprintf_s`, or
`vsnprintf_s`. Those names appear only in rurban safeclib
(`include/safe_str_lib.h`) and the C+ facade (`stdlib/safe/safe_format.h`).
The task prompt's "strcmp_s inline variants / sprintf_s / vsnprintf_s" are not
present as Intel public declarations — the only inline-in-`.c` definitions are
the four typed `snprintf_s_*` wrappers above.

## Types and macros (17)

- `rsize_t` (`safe_lib.h`), `constraint_handler_t` (`safe_lib.h`), `errno_t` (`safe_types.h`).
- `SAFEC_VERSION_MAJOR/MINOR/PATCH` = 1/3/0, `SAFEC_VERSION_STRING` = `"1.3.0"`, `SAFEC_VERSION`.
- `RSIZE_MAX` = `256UL << 20` (256 MB) — diverges from C+ `RSIZE_MAX_MEM` = `SIZE_MAX >> 1`.
- `RSIZE_MAX_MEM{,16,32}` in `safe_mem_lib.h`; `RSIZE_MIN_STR`=1, `RSIZE_MAX_STR` in `safe_str_lib.h`.
- `SAFE_STR_MIN_LOWERCASE/UPPERCASE/NUMBERS/SPECIALS` = 2/2/1/1; password length 6–32.
- `sl_default_handler` = `ignore_handler_s`.
- `SNPRFNEGATE(x)` = `(-1*(x))` — Intel typed wrappers return **negated** error codes.
- `ESNULLP`..`ESNOTFND` (400–409), `ESBADFMT`/`ESFMTTYP` (410/411), `EOK` (0).
- `rename_symbols.h` — `cplus_upstream_*` prefix map (17 `#define`s) used only by the standalone Intel CMake target.

## Duplicate vs unique analysis (rurban/safeclib)

- **Already integrated (16):** the 11 checked memory/string functions
  (`memcpy_s`, `memmove_s`, `memset_s`, `memcmp_s`, `memzero_s`, `strcpy_s`,
  `strncpy_s`, `strcat_s`, `strncat_s`, `strcmp_s`, `strnlen_s`), plus
  `strtok_s` and the four handler/handler-setter symbols. C+ links these via
  `cplus_sc_*` adapters in `libcplus_safecrt.a`.
- **Available in rurban safeclib but not wrapped (51):** the typed-wide
  (`wmemcpy_s`, `wmemmove_s`, `wmemcmp_s`, `wcscpy_s`, `wcscat_s`,
  `wcsncpy_s`, `wcsncat_s`, `wcsnlen_s`) and 16/32-bit memory variants, the
  field-fixed string family (`stpcpy_s`, `stpncpy_s`, `strcpyfld*_s`,
  `strcmpfld_s`, `strfirst*_s`, `strlast*_s`, validation predicates, etc.), and
  the search/justify helpers. No need to import Intel for any of these.
- **Unique (2):** `wmemset_s`, `wcpcpy_s` — neither is declared/implemented in
  rurban safeclib, but both are **declaration-only** in the vendored Intel
  subset (no `.c`) and have no C+ consumer.
- **Intel-specific contracts (4):** the typed `snprintf_s_*` wrappers use a
  hand-written partial format parser with negated error codes and no
  compile-time format checking; C+ `safe_format.h` already provides a
  variadic, format-attribute-checked, `%n`-rejecting, literal-format-enforced
  `snprintf_s`.
- **Notable:** Intel's `memset_s` is a 3-arg non-Annex-K spelling; Intel's
  `strnlen_s` loop is bounds-first (the same fix C+ carried locally for rurban);
  error codes are positive in Intel (RCNEGATE is a no-op outside `__KERNEL__`)
  but negative in the C+ format family — these are not interchangeable.

**Archived status effect:** because the upstream is read-only and unmaintained,
no Intel bug/security fix can arrive. Given full duplication by the already
linked, locally patched rurban backend, archival tips the decision decisively
toward reference-only.

## Recommendation

### KEEP AS REFERENCE-ONLY; DO NOT INTEGRATE ANY INTEL FILES.

Rationale:

1. All 15 build-capable Intel implementations duplicate APIs C+ already links
   and tests; the remaining 4 are uncompiled `inline` wrappers.
2. 51 more Intel declarations are available in rurban safeclib if needed,
   without a second backend.
3. The only 2 unique declarations lack Intel implementations in the subset and
   lack a demonstrated need.
4. The `snprintf_s_*` wrappers are weaker than C+ `safe_format.h`.
5. Upstream is archived/read-only (2026-07-20) — no maintenance or security fixes.
6. Integrating would either collide with CRT/safeclib symbols (requiring
   `cplus_upstream_*` prefixing) or create two competing backends with
   divergent contracts.
7. Existing records (`SAFER_UPSTREAM_INTEGRATION.md`, `DEPENDENCIES.md`) already
   state reference-only; no source change is warranted.

**Dependency closure if a future requirement ever forced integration** (not
recommended): generic files `include/safe_lib.h`, `safe_lib_errno.h`,
`safe_types.h`, `safeclib/safeclib_private.h`, `safe_str_constraint.{h,c}`,
`ignore_handler_s.c`, `abort_handler_s.c`, plus the specific upstream `<api>.c`
(which for `wcpcpy_s`/`wmemset_s` is **not currently vendored**). For a
memory API add `include/safe_mem_lib.h`, `safe_mem_constraint.{h,c}`,
`mem_primitives_lib.{h,c}`. For `snprintf_support.c` add `include/snprintf_s.h`,
`include/safe_str_lib.h`. Build must retain the `cplus_upstream_*` prefix and
must **not** add these objects to `build_safecrt.sh`'s `sc_*.o` archive.

## Warnings

- `third_party/intel-safestringlib/CMakeLists.txt` can independently build a
  `cplus_safestringlib` static library from 16 Intel `.c` files. It is not
  referenced by any parent CMakeLists, but do not add it to the main build.
- 54 of 73 declarations are declaration-only — compiling callers against the
  Intel headers alone yields undefined references.
- `snprintf_support.c` is compiled by neither build path; its `inline`
  definitions are unbuildable as shipped.
- Error-code contracts differ (Intel positive codes vs C+ format negative codes).
- `RSIZE_MAX` differs (Intel 256 MB vs C+ `SIZE_MAX >> 1`).
- The snapshot (`f4491915`, SAFEC 1.3.0) is frozen at an archived upstream.

## Verification

```sh
python3 -m json.tool docs/libc/intel_safestringlib_inventory.json >/dev/null && echo JSON_OK
# => JSON_OK
```

Count cross-check from the JSON: 73 unique APIs; by header
2/20/47/4; by class 16 integrated-duplicate + 51 safeclib-available +
2 unique + 4 Intel-specific = 73; 0 integrated; 6 unit-test files.
