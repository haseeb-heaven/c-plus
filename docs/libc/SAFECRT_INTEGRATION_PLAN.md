# SafeCRT / safeclib integration plan (prioritized, from the actual inventory)

**Status:** planning document only. No implementation, no fabricated APIs, no test results are
claimed here. Derived from the machine-readable inventory and the on-disk build script on
`agent/modern-stdlib` (2026-11-23).

**Sources of truth used:**

- `docs/libc/safeclib_inventory.json` (+ its prose twin `docs/libc/SAFECLIB_API_INVENTORY.md`)
- `docs/libc/COVERAGE.md`, `docs/libc/UPSTREAM_EVALUATION.md`, `docs/libc/ISO_C_AUDIT.md`,
  `docs/libc/iso_c_inventory.json`
- `scripts/build_safecrt.sh`, `stdlib/safe/CMakeLists.txt`, `stdlib/safe/src/safeclib_ext_wrappers.c`,
  `stdlib/safe/safe_ext_mem.h`
- `third_party/safeclib/src/**/*.c`, `third_party/safeclib/include/*.h` (pinned
  `90b076e94c4b0fb202dddf948d5d620e5395c1b0`, MIT)

**Scope guard:** 437 is the count of *distinct public names in the vendored safeclib headers*, not
an ISO C conformance target. The majority are non-standard extensions (`u8`=110, wide/wchar=83,
`str` extensions within 158); only a subset (e.g. `bsearch_s`, `qsort_s`, `gets_s`, `memcpy_s`,
`strcpy_s`, …) are C11 Annex K. **This plan does not claim that all 437 APIs are needed for ISO C
conformance**, nor that integrating more of them improves conformance by itself. The ISO C target
is tracked separately in `docs/libc/iso_c_inventory.json` (948 APIs) and `ISO_C_AUDIT.md`.

---

## 1. Reconciled counts

All figures below are re-derived from `safeclib_inventory.json` (`apis` array of 437) and agree
with `SAFECLIB_API_INVENTORY.md` §"Counts" and `COVERAGE.md:76`.

| Classification | Count | Meaning (per inventory) | Rechecked from JSON |
|---|---:|---|---|
| INTEGRATED | 40 | Upstream `.c` enumerated in `scripts/build_safecrt.sh`, compiled with `-D` prefixes, archived in `build/safe/libcplus_safecrt.a`, declared in a header, with test evidence | 40 `classification=INTEGRATED`; 17 distinct upstream impl sources; 40 `build_status=compiled+linked` |
| ADAPTER_ONLY | 10 | A `cplus_*` API exists in `stdlib/safe` but is an *independent* C/C++ implementation; no upstream source is compiled | 10, all `cplus_adapter_status=independent_cpp_implementation` |
| NOT_INTEGRATED | 387 | Upstream source is vendored but not in the build-script list (and/or fails the C+ compile probe), and no adapter calls it | 387, all `build_status=not-compiled`, `cplus_adapter_status=none` |
| UNAVAILABLE | 0 | Header-declared name with no definition anywhere in vendored `src/` | 0 |
| **Total** | **437** | 40 + 10 + 387 + 0 | 437 |

Cross-checks: `counts` object in JSON = `{total:437, INTEGRATED:40, ADAPTER_ONLY:10,
NOT_INTEGRATED:387, UNAVAILABLE:0, compiled_in_build_script:40, with_cplus_adapter:27,
unit_tested:29, fuzz_tested:11}`. Family totals sum to 437: `str=158, u8=110, wchar=83, mem=51,
io=17, os=8, handler=6, misc=4`. Header totals (a name may appear twice): `safe_str_lib.h=239,
safe_u8_lib.h=114, safe_mem_lib.h=65, safe_lib.h=21`. `with_cplus_adapter=27` counts entries with a
non-null `cplus_exported_name` (27 in the JSON); it is a different measure from the adapter-status
taxonomy, whose categories sum to 47 (13 `integrated_adapter` + 20 `internal_prefixed_symbol` + 4
`prefixed_symbol_only` + 10 `independent_cpp_implementation`) — not a contradiction, just distinct
definitions.

The 40 INTEGRATED names collapse to **17 distinct upstream implementation files**
(`abort_handler_s.c`, `ignore_handler_s.c`, `mem/safe_mem_constraint.c`, `str/safe_str_constraint.c`,
`mem/memcpy_s.c`, `mem/memmove_s.c`, `mem/memset_s.c`, `extmem/memcmp_s.c`, `extmem/memzero_s.c`,
`str/strcpy_s.c`, `str/strncpy_s.c`, `str/strcat_s.c`, `str/strncat_s.c`, `str/strnlen_s.c`,
`str/strerror_s.c`, `str/strtok_s.c`, `extstr/strcmp_s.c`). The main build-script loop also compiles
`mem/mem_primitives_lib.c`, but it is a dependency-only source (no public API of its own), which is
why the main-loop upstream file count is 18 while the INTEGRATED source count is 17. The 10
ADAPTER_ONLY names are exactly:
`fopen_s`, `fprintf_s`, `fscanf_s`, `printf_s`, `scanf_s`, `snprintf_s`, `sprintf_s`, `sscanf_s`,
`vsnprintf_s`, `vsprintf_s`.

---

## 2. Build-system constraint and exact change checklist

**Documented constraint (as given in `COVERAGE.md:76` and `UPSTREAM_EVALUATION.md:23,33,84`):**
of 206 upstream `.c` files, only a 19-file subset is compiled (18 upstream sources + 1 local
adapter `src/cplus_sc_wrappers.c`); **188 upstream `.c` files are currently uncompiled**. The build
is a hard-coded allow-list, not a glob: `scripts/build_safecrt.sh` compiles only the files named in
its loops (lines 95–100 and 102–124) and archives them with `ar rcs` (line 135). Everything else in
`third_party/safeclib/src` is dead code from the linker's point of view.

Additional hard facts that constrain any batch:

- Compile probe (recorded in `SAFECLIB_API_INVENTORY.md:13,523`): **149 of 206 sources compile OK,
  57 fail** under the generated `build/safe/config.h` + build-script `-D` prefixes. Restricted to
  the 387 NOT_INTEGRATED API/source pairs in the JSON: **261 OK, 126 FAIL**.
- **230 of 387** NOT_INTEGRATED APIs have an external dependency closure *fully satisfied* by
  already-compiled sources; **223** of those also have every listed source probe-OK. These are the
  only candidates that can land as a pure source-list addition.
- `scripts/build_safecrt.sh` is **not** referenced by the root `CMakeLists.txt`; the archive is
  built standalone. `stdlib/safe/CMakeLists.txt` lists only the 12 `stdlib/safe/src/*.c` files and
  does not include `safeclib_ext_wrappers.c`, `safe_except.c`, or any vendored `.c`.
- Host check on this host (Apple SDK / macOS): grep of `$SDK/usr/include` found **no declaration of
  `bsearch_s`, `qsort_s`, `memchr_s`, `strchr_s`, `gets_s`, or `asctime_s`** — i.e. no
  host `_s` link collision; the base symbols (`_memchr`, `_bsearch`, `_qsort`, `_strlcpy`,
  `_strlcat`, `_wcscpy`, `_vsnprintf`) are present in `libSystem.B.tbd`. Exception: `strlcpy.c` /
  `strlcat.c` fail the standalone probe because Darwin `<secure/_string.h>` defines them as
  fortify macros; exclude them (the inventory's low-risk extstr group already does).

**Exactly what must change to compile a batch (no other file ownership implied here):**

1. **Add each new upstream `.c` path to the `for f in …` list** in `scripts/build_safecrt.sh`
   (lines 102–124). This is the gate: an unlisted source is never compiled or archived. If a source
   must stay unprefixed (the `timingsafe_*` case), add it to the separate loop at lines 95–100
   instead, with the one required `-D invoke_safe_mem_constraint_handler=…`.
2. **Add a `-D<public_name>=cplus_sc_<public_name>` entry (and the `_chk`/`_uchk` variants where
   present) to `SC_DEFS`** at line 38 for every new public `_s` name. Without this the vendored
   symbols are un-namespaced and can collide with host/other backends. (The timingsafe sources are
   the documented exception: their `BOS_OVR2` macros reference the same token, so they are compiled
   unprefixed and wrapped.)
3. **Close the dependency closure in the same edit**: every non-own source in the API's
   `dependency_closure.source_files` that is not already compiled must be added to step 1 too
   (e.g. `extmem/memrchr_s.c` for `strrchr_s`; `str/vsnprintf_s.c`, `wchar/wcsnlen_s.c`,
   `wchar/wcstombs_s.c` for `io/vfprintf_s`).
4. **Export a namespaced entry point**: add a thin `cplus_sc_*` wrapper to
   `stdlib/safe/src/safeclib_ext_wrappers.c` (pattern already used for the two timingsafe APIs) or
   to a new local adapter `.c`, then declare it in the matching `stdlib/safe/*.h` header
   (`safe_ext_mem.h` is the existing example). If a source cannot be prefixed, its wrapper must call
   the raw `_chk` symbol with `BOS_UNKNOWN`, as the timingsafe adapter does.
5. **Add missing config defines** only where a probe/limitation requires them: e.g. `SIZEOF_WCHAR_T`
   for the wide batch (per the inventory's medium-risk wide-string group), a `char8_t` fallback in
   `safe_types.h` for the `extu8` batch (several `vu8*_s` sources currently FAIL with "unknown type
   name `char8_t`"), and `SAFECLIB_ENABLE_UNSAFE` only if `tmpnam_s` is ever wanted (currently
   deliberately avoided).
6. **Re-run `scripts/build_safecrt.sh`** to rebuild `build/safe/libcplus_safecrt.a` (the `ar rcs`
   step is unconditional), then run the existing CRT unit/contract verification separately — this
   document issues no test commands and claims no results.
7. If the new symbols must be visible to CMake consumers, update `stdlib/safe/CMakeLists.txt`
   explicitly; it currently does not consume the safecrt archive at all.

---

## 3. Ranked NOT_INTEGRATED candidates by family

Legend used in the tables:

- **Demand** — caller-demand signal, strongest first:
  `AnnexK` = named as a not-shipped blocker in `COVERAGE.md:79`/`ISO_C_AUDIT.md`;
  `Code` = referenced by real repo code/tests/headers (grep of `stdlib/`, `libc/`, `tests/`,
  `demos/`, `scripts/`, excluding `third_party/`);
  `Base-ubiquitous` = bounded variant of a heavily used base call that is already
  IMPLEMENTED_BUNDLED or PLATFORM_BACKED in `iso_c_inventory.json`;
  `Closure` = a dependency of another NOT_INTEGRATED source that must land first.
- **Host** — host symbol availability: base symbol present in the Apple SDK (`libSystem.B.tbd`),
  no host `_s` declaration found; `iso_c_inventory.json` class given in parentheses.
- **Pinned source / standalone** — file exists under `third_party/safeclib/src` at the pinned
  commit; "OK" = inventory compile-probe result.
- **Closure** — external dependency closure: `green` = all non-own deps already compiled;
  `amber` = needs other new sources in the same/later batch; `red` = needs a FAIL/probe-broken or
  deliberately excluded source.

### 3.1 Memory (`mem`, 51 NOT_INTEGRATED; inventory low-risk group 1)

| Rank | API | Demand | Host (base) | Pinned `.c` / probe | Closure | Batch |
|---:|---|---|---|---|---|---|
| 1 | `memchr_s` | Base-ubiquitous (bounded `memchr`) | present, `memchr`=IMPLEMENTED_BUNDLED | `src/extmem/memchr_s.c`, OK | green (6 src) | B1 |
| 2 | `memrchr_s` | Closure (prereq of `strrchr_s`) + Base-ubiquitous | base `memrchr` bundled as `cplus_memrchr` (musl-derived, `COVERAGE.md:58`); not in ISO inventory; no host `_s` | `src/extmem/memrchr_s.c`, OK | green (6 src) | B1 |
| 3 | `memccpy_s` | Base-ubiquitous (bounded `memccpy`; musl `cplus_memccpy` exists) | present in libSystem | `src/extmem/memccpy_s.c`, OK | green (7 src) | B1 |
| 4 | `memset16_s` / `memset32_s` | word/pair memory primitives; anchors the extmem family | no host 16/32-bit set (no collision) | `src/extmem/memset16_s.c`, `memset32_s.c`, OK | green (5 src each) | B1 |
| 5 | `memcpy16_s`/`memcpy32_s`/`memmove16_s`/`memmove32_s`/`memzero16_s`/`memzero32_s`/`memcmp16_s`/`memcmp32_s` | same extmem family; reuse the same prefix + wrapper change | host word-op symbols not declared as `_s` | all 8 pinned `src/extmem/*.c`, all OK | green (4–5 src each) | B1 |

Note: `timingsafe_bcmp`/`timingsafe_memcmp` are already functionally integrated via the separate
loop + `safeclib_ext_wrappers.c` (see §6); do not re-batch them.

### 3.2 String (`str`, 158 NOT_INTEGRATED; inventory low-risk group 2)

| Rank | API | Demand | Host (base) | Pinned `.c` / probe | Closure | Batch |
|---:|---|---|---|---|---|---|
| 1 | `strchr_s` | Base-ubiquitous (bounded `strchr`) | `strchr`=IMPLEMENTED_BUNDLED | `src/extstr/strchr_s.c`, OK | green (5 src) | B2 |
| 2 | `strstr_s` | Base-ubiquitous (bounded substring search) | `strstr`=IMPLEMENTED_BUNDLED | `src/extstr/strstr_s.c`, OK | green (5 src) | B2 |
| 3 | `stpcpy_s` | Ordinary-C `cplus_stpcpy` already shipped; `_s` sibling fills the gap | host `stpcpy` present (not in ISO inventory) | `src/extstr/stpcpy_s.c`, OK | green (5 src) | B2 |
| 4 | `stpncpy_s` | same as `stpcpy_s` | host `stpncpy` present | `src/extstr/stpncpy_s.c`, OK | green (5 src) | B2 |
| 5 | `strcasecmp_s` | bounded case-insensitive compare; ordinary `cplus_strcasecmp` shipped | host POSIX `strcasecmp` present (not in ISO inventory) | `src/extstr/strcasecmp_s.c`, OK | green (5 src) | B2 |
| 6 | `strrchr_s` | Base-ubiquitous | `strrchr`=IMPLEMENTED_BUNDLED | `src/extstr/strrchr_s.c`, OK | **amber** — needs `memrchr_s.c` (B1) | B3 |
| 7 | `strspn_s`, `strcspn_s`, `strpbrk_s`, `strnterminate_s`, `strzero_s`, `strljustify_s`, `strcmpfld_s` | bounded scanning/classification set | base `strspn`/`strcspn`/`strpbrk` IMPLEMENTED_BUNDLED | pinned `src/extstr/*.c`, all OK | green (5 src each) | B3 |
| — | `strlcpy`, `strlcat` | Code (ordinary `cplus_strlcpy`/`cplus_strlcat` used in tests) | base present but **Darwin fortify macro conflict** | pinned `src/extstr/strlcpy.c` / `strlcat.c`, probe **FAIL** | red | excluded; inventory group already excludes them |

### 3.3 Format (`str`/`io`; format entry points are *not* a NOT_INTEGRATED gap)

The headline format APIs (`snprintf_s`, `vsnprintf_s`, `sprintf_s`, `vsprintf_s`, `printf_s`,
`fprintf_s`, `scanf_s`, `fscanf_s`, `sscanf_s`) are the 10 **ADAPTER_ONLY** entries: they already have
independent `cplus_*` implementations in `stdlib/safe/src/safe_format.c` / `safe_io.c`. Replacing
them with upstream sources is a *wrapper-semantics decision* (the inventory rates this group
`medium` risk because of overlap), not new coverage. The genuinely missing format-adjacent entries
are the `v*` (va_list / **stdarg**) sinks below.

### 3.4 I/O and stdarg (`io`, 17; `os`, 8)

| Rank | API | Demand | Host (base) | Pinned `.c` / probe | Closure | Batch |
|---:|---|---|---|---|---|---|
| 1 | `bsearch_s` | **AnnexK** (explicit `COVERAGE.md:79` blocker); also inventory low-risk group 3 | `bsearch`=PLATFORM_BACKED | `src/misc/bsearch_s.c`, OK | green (4 src) | B1 |
| 2 | `qsort_s` | **AnnexK** (same explicit blocker) | `qsort`=PLATFORM_BACKED | `src/misc/qsort_s.c`, OK | green (5 src) | B1 |
| 3 | `vprintf_s` | stdarg sink; completes the `printf_s` family without changing the existing adapter | `vprintf`=PLATFORM_BACKED, no host `_s` decl | `src/io/vprintf_s.c`, OK | green (5 src) | B4 |
| 4 | `vfprintf_s` | stdarg sink paired with the existing `fprintf_s` adapter | `vfprintf`=PLATFORM_BACKED | `src/io/vfprintf_s.c`, OK | **amber** — needs `vsnprintf_s.c`, `wcsnlen_s.c`, `wcstombs_s.c` (all pinned, all probe OK) | B4 |
| 5 | `vscanf_s`, `vsscanf_s`, `vfscanf_s` | stdarg sinks for the `scanf_s` family | host `vscanf`/`vsscanf`/`vfscanf` PLATFORM_BACKED | pinned `src/io/*.c`, OK | green/amber (verify closure before batching) | B4 |
| 6 | `asctime_s`, `ctime_s` | **AnnexK**-adjacent time entry points named as not-shipped in `COVERAGE.md:79` | `asctime`/`ctime`=PLATFORM_BACKED | `src/os/asctime_s.c`, `ctime_s.c`, OK | green (6 src) | B4 |
| 7 | `getenv_s`, `freopen_s`, `tmpfile_s` | bounded env/stream helpers | base PLATFORM_BACKED | pinned `src/os/getenv_s.c`, `src/io/freopen_s.c`, `tmpfile_s.c`, OK | green (5–6 src) | B5 |
| — | `gets_s`, `tmpnam_s` | AnnexK name (`gets_s`) | base `gets` UNSUPPORTED in ISO inventory; `tmpnam_s` gated behind `SAFECLIB_ENABLE_UNSAFE` | pinned, OK | red for `tmpnam_s` (deliberately avoided insecure API) | excluded unless policy changes |

### 3.5 Wide string / wchar (`wchar`, 83; inventory medium-risk group 5)

| Rank | API | Demand | Host (base) | Pinned `.c` / probe | Closure | Batch |
|---:|---|---|---|---|---|---|
| 1 | `wcsnlen_s` | wide bounded length; prereq for wide copy/cat | `wcsnlen` not listed in ISO inventory; host libc provides it | `src/wchar/wcsnlen_s.c`, OK | green (5 src) | B3 |
| 2 | `wcscpy_s`, `wcsncpy_s` | wide copy; base PLATFORM_BACKED | `wcscpy`/`wcsncpy`=PLATFORM_BACKED | pinned `src/wchar/*.c`, OK | green (5 src each) | B3 |
| 3 | `wcscat_s`, `wcsncat_s` | wide concat; base `wcscat` PLATFORM_BACKED | same | pinned `src/wchar/*.c`, OK | green | B3 |
| 4 | `wmemcmp_s`, `wmemcpy_s`, `wmemmove_s` | wide memory primitives | base `wmem*`=PLATFORM_BACKED | `wmemcmp_s.c` probe OK, **`wmemcpy_s.c` probe FAIL** | red for `wmemcpy_s` | defer; fix probe/config first |
| 5 | `vswprintf_s`, `vsnwprintf_s`, `vfwprintf_s`, `vswscanf_s`, `vfwscanf_s`, `vwprintf_s`, `vwscanf_s` | wide stdarg sinks; no C+ wide adapter exists | host `vswprintf` etc. present; wide ABI differs on Win/MinGW | pinned `src/wchar/*.c`, probe OK | amber (needs wide config/ABI decision) | B5 |

Wide batch caveat (from the inventory): add `SIZEOF_WCHAR_T` to the generated config first; the
wide ABI differs on Windows/MinGW and the C+ build config currently lacks it for some sources.

### 3.6 Unicode-8 (`u8` = 110, `str`-filed `extu8`) — defer

Highest count but lowest readiness: `vu8snprintf_s`, `vu8sprintf_s`, `vu8sscanf_s` and several
others FAIL the standalone probe with `unknown type name 'char8_t'` because the vendored
`include/safe_types.h` omits the `@FALLBACK_CHAR8_T` definition. No integration batch until that
config gap is closed and the host/`char8_t` policy is decided.

---

## 4. Consolidated top 10 (highest value, ranked)

Ranking = caller demand × standalone feasibility × dependency-closure readiness; risk noted.
All ten are `classification=NOT_INTEGRATED`, all have a pinned upstream `.c` at
`90b076e9…` that exists on disk and compiles OK in the recorded probe.

| # | API | Family | Demand | Host symbol | Pinned source / probe | Closure | Batch | Risk |
|---:|---|---|---|---|---|---|---|---|
| 1 | `bsearch_s` | misc / Annex K | AnnexK blocker (`COVERAGE.md:79`); inventory group 3 | base `bsearch` PLATFORM_BACKED; no host `_s` | `src/misc/bsearch_s.c`, OK | green | B1 | low |
| 2 | `qsort_s` | misc / Annex K | AnnexK blocker; pairs 2-file batch | base `qsort` PLATFORM_BACKED; no host `_s` | `src/misc/qsort_s.c`, OK | green | B1 | low |
| 3 | `memchr_s` | mem | bounded form of an IMPLEMENTED_BUNDLED primitive | `memchr` bundled; base in libSystem | `src/extmem/memchr_s.c`, OK | green | B1 | low |
| 4 | `memrchr_s` | mem | Closure prereq for `strrchr_s` + bounded `memrchr` | base `memrchr` bundled as `cplus_memrchr`; no host `_s` | `src/extmem/memrchr_s.c`, OK | green | B1 | low |
| 5 | `memccpy_s` | mem | bounded `memccpy` (ordinary `cplus_memccpy` shipped) | base in libSystem | `src/extmem/memccpy_s.c`, OK | green | B1 | low |
| 6 | `strchr_s` | str | bounded form of an IMPLEMENTED_BUNDLED primitive | `strchr` bundled | `src/extstr/strchr_s.c`, OK | green | B2 | low |
| 7 | `strstr_s` | str | bounded form of an IMPLEMENTED_BUNDLED primitive | `strstr` bundled | `src/extstr/strstr_s.c`, OK | green | B2 | low |
| 8 | `stpcpy_s` | str | ordinary `cplus_stpcpy` shipped; `_s` sibling missing | host `stpcpy` present | `src/extstr/stpcpy_s.c`, OK | green | B2 | low |
| 9 | `wcscpy_s` | wchar | wide-copy gap; base PLATFORM_BACKED | `wcscpy`=PLATFORM_BACKED; no host `_s` | `src/wchar/wcscpy_s.c`, OK | green (but needs `SIZEOF_WCHAR_T` define) | B3 | medium (wide ABI/config) |
| 10 | `vprintf_s` | io / stdarg | fills the `v*` sink gap next to the existing `printf_s` adapter | `vprintf`=PLATFORM_BACKED; no host `_s` | `src/io/vprintf_s.c`, OK | green | B4 | low–medium |

Close runners-up: `strcasecmp_s`, `strrchr_s` (amber: needs #4), `memset16_s`/`memset32_s` (cheap
add-on to B1), `wcsnlen_s`, `vfprintf_s` (amber: needs `vsnprintf_s.c` + two wchar sources).

---

## 5. Recommended batch order

| Batch | Contents | Why now | Change footprint |
|---|---|---|---|
| **B1** | `bsearch_s`, `qsort_s`; `memchr_s`, `memrchr_s`, `memccpy_s`; the 10 `mem*16/32_s` word primitives | 2-file Annex K batch plus the entire dependency-closed extmem group; inventory rates all three groups `low`; `memrchr_s` unblocks `strrchr_s` | Add ~15 paths to the build-script loop; add ~15 `-D` prefixes; add `cplus_sc_*` wrappers in `safeclib_ext_wrappers.c` + declarations; no config change |
| **B2** | `strchr_s`, `strstr_s`, `stpcpy_s`, `stpncpy_s`, `strcasecmp_s` | Highest-demand bounded string forms; all probe OK, green closure; exclude `strlcpy*`/`strlcat*` (Darwin macro) | Add 5 paths + prefixes + wrappers; reuse `safe_str_constraint.c`, `strnlen_s.c`, `strcmp_s.c` |
| **B3** | `strrchr_s`; `wcsnlen_s`, `wcscpy_s`, `wcsncpy_s`, `wcscat_s`, `wcsncat_s`; remaining extstr scanning set | `strrchr_s` now unblocked; wide core is the inventory's medium-risk group — do after narrow string is stable | Add `SIZEOF_WCHAR_T` to generated config; add paths + prefixes + wrappers; decide wide ABI contract |
| **B4** | `vprintf_s`, `vfprintf_s`, `vscanf_s`, `vsscanf_s`, `vfscanf_s`; `asctime_s`, `ctime_s` | stdarg sinks and Annex K-adjacent time names; `vfprintf_s` needs `vsnprintf_s.c`, `wcsnlen_s.c`, `wcstombs_s.c` in the same archive | Add format/wchar dependency sources; keep existing independent `cplus_snprintf_s*` adapters intact (no semantics change) |
| **B5** | `getenv_s`, `freopen_s`, `tmpfile_s`; wide stdarg sinks; only then `u8` family after config fix | Remaining host-backed entries; u8 first needs `char8_t` fallback in `safe_types.h` | Config changes (`char8_t` fallback); per-API closure check; wide ABI decision |

Explicitly **excluded** from all batches: `strlcpy*` / `strlcat*` (probe FAIL, Darwin fortify macro),
`tmpnam_s` (`SAFECLIB_ENABLE_UNSAFE`, deliberately avoided), and the 4 timingsafe entries (already
functionally integrated — see §6).

---

## 6. Count contradictions in the source docs (report only)

No source document was modified. These are the discrepancies found; they are reported, not
reconciled by editing:

1. **Four timingsafe entries are internally contradictory in `safeclib_inventory.json`.**
   `_timingsafe_bcmp_chk`, `_timingsafe_memcmp_chk`, `timingsafe_bcmp`, `timingsafe_memcmp` each
   carry a stray top-level `status: "INTEGRATED"`, `cplus_name: "cplus_sc_timingsafe_*"` and
   `test: "tests/crt/unit/test_timingsafe_s.c"`, yet their `classification` is `NOT_INTEGRATED`,
   their `build_status.status` is `not-compiled`, and their `classification_reason` states the
   source "is not enumerated in scripts/build_safecrt.sh lines 93-111, so it is not compiled or
   linked". That reason is **factually false for these two sources**: lines 95–100 of the script
   compile `src/extmem/timingsafe_bcmp.c` and `timingsafe_memcmp.c`, and
   `stdlib/safe/src/safeclib_ext_wrappers.c` exports `cplus_sc_timingsafe_bcmp` /
   `cplus_sc_timingsafe_memcmp` declared in `stdlib/safe/safe_ext_mem.h`. If these four names were
   reclassified consistently, the headline counts would become **44 INTEGRATED / 383
   NOT_INTEGRATED** instead of 40 / 387. This plan keeps the authoritative 40 / 10 / 387 figures
   and treats the four as "functionally integrated but mis-classified" (excluded from batches).

2. **"188 uncompiled `.c` files" vs. the build script's actual source count.**
   `COVERAGE.md:76` and `UPSTREAM_EVALUATION.md:23,33,84` state 206 upstream `.c` files, a 19-file
   compiled subset (18 upstream + 1 local adapter) and 188 uncompiled. The script actually
   compiles **18 upstream sources in the main loop + 2 timingsafe sources in a separate loop +
   `cplus_sc_wrappers.c`** = 21 files from `third_party/safeclib/src`; the 40 INTEGRATED APIs map
   to 17 of those 18 main-loop sources (`mem_primitives_lib.c` is dependency-only). On precise
   accounting, uncompiled upstream sources are **185–186**, not 188 — the documented 188 omits the
   two timingsafe files that *are* compiled. The "188" figure is used as the stated constraint in
   §2 because it is the documented one, but it is 2–3 files high.

3. **Compile-probe denominators differ.** `SAFECLIB_API_INVENTORY.md:13,523` says 149 of 206
   sources compile (57 fail); the JSON's per-API `source_compile_probe` entries for the 387
   NOT_INTEGRATED APIs total 261 OK / 126 FAIL (source/API pairs, and a source can back multiple
   names). Both are internally consistent with their own method but are different units; do not
   compare them as like-for-like counts.

4. **Potential (non-)contradiction noted for completeness:** `with_cplus_adapter=27` /
   "With a C+ exported name | 27" counts only entries with non-null `cplus_exported_name`, while
   the `cplus_adapter_status` taxonomy has 47 entries across four non-`none` categories. These
   measure different things; no count change is implied.

---

## 7. Non-claims

- No claim that all 437 vendored APIs are required for ISO C conformance (most are non-standard
  extensions; ISO C is tracked in `iso_c_inventory.json` / `ISO_C_AUDIT.md`).
- No claim that integrating more `_s` sources yields Rust-grade memory safety or makes raw
  pointers, capacities or format strings safe.
- No test execution, ABI evidence, Windows/Linux CI result, or remote-green claim is made here.
- No API implementation is invented: every candidate name and `.c` path above is taken verbatim
  from the pinned vendored tree and the inventory JSON.
