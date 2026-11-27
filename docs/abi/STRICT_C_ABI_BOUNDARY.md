# Strict C-ABI boundary contract (issues #186, #187)

Status: **enforced contract**, not a claim of cross-platform proof. This is the
authoritative description of what the C-ABI gate checks and what it does not.
CI references this document by name in `.github/workflows/compiler.yml` and
`.github/workflows/libc-crt.yml`.

## 1. Host-struct purity (`libc/install-include/`, `stdlib/safe/`)

The following standard C types must keep the host layout whether or not any
Modern C+ / hardened header is included:

`FILE`, `mbstate_t`, `pthread_t`, `pthread_mutex_t`, `atomic_int` /
`_Atomic int`, `div_t`, `ldiv_t`, `struct timespec`, `struct tm`, `fpos_t`.

`tests/abi/host_abi_purity/host_types.c` prints `sizeof`, `_Alignof` and the
documented `offsetof` values for these types in two translation units: a plain
C baseline and the same probe with `libc/install-include/*.h`,
`stdlib/safe/*.h`, `runtime/include/cplus/cplus.h` and every `stdlib/*.hp`
header included under declaration-only opt-outs (`CPLUS_SAFE_C_DISABLE`,
`CPLUS_LIBC_SAFE_DISABLE`). The output must be byte-identical. Adding metadata
fields to a host type, or changing a field order, changes a record and fails
the gate. Additional hardened state lives only in namespaced `cplus_*` types
(e.g. `cplus_alloc_meta`, `cplus_safe_alloc_meta`, `cplus_atomic_flag`) or in
out-of-band side tables; it is never embedded in a host type.

`runtime/` structs (`cplus_cleanup`, `cplus_scope`, `CPlusExceptionFrame`) and
the public `libc/` / `stdlib/safe/` structs are checked against independently
written plain-C mirror declarations in the same probe. A change to a real
struct that is not mirrored in the C-ABI contract is a gate failure by design.

## 2. Header inclusion and macro hygiene

- Including a hardened header without its opt-in macro must not redefine
  `malloc`, `memcpy`, `strcpy`, `printf`, ... (`CPLUS_SAFE_C_DISABLE`,
  `CPLUS_LIBC_SAFE_DISABLE`). Including `stdlib/safe/safe_c.h` *is* the explicit
  opt-in for source-level macro mapping; it is never applied from `stdlib/`,
  `runtime/` or `libc/` build flags implicitly.
- A plain C translation unit that includes no Modern C+ header must produce the
  same ABI-relevant preprocessor facts (`__SIZEOF_POINTER__`, `__LP64__`,
  byte order, platform defines) as one that includes the declaration-only
  headers. The probe runner diffs these records as well.
- No C-ABI header may export a mangled C++ symbol. `tests/abi/c_to_cp/run.sh`
  and the fingerprint tool check for ` T __Z` on the C archive and for the
  exact set of plain-C symbol names.

## 3. `stdlib/*.hp` interop boundaries

Every file matching `stdlib/*.hp` is discovered (never hard-coded) by
`tests/compiler/abi_gate/run_hp_boundary.py`. Two rules apply:

1. **Plain C boundary** — native handles (`FILE *`, `yyjson_val *`, sockets,
   `error *err` adapters, C callbacks) keep the C layout and the C function
   signature. RAII sugar (`class` with `drop()`) is lowered inside the C+
   translation unit by `RaiiLowering`; it adds no vptr/refcount to a C struct.
2. **Digestibility** — each header must parse in a C+ translation unit. Headers
   whose third-party dependency is not installed are reported as `BLOCKED` with
   the exact missing include; a compile failure with all dependencies present
   is a hard failure. Cross-boundary link/runtime parity is exercised by
   `tests/abi/c_to_cp/` and `tests/abi/precompiled/`, which pass plain-C
   pointers, structs, callbacks, `FILE *`, `errno_t` and variadic calls across
   the C/C+ boundary in both directions.

## 4. `_s` diagnostics and docs sync

The canonical catalog is the `safeSAbiCatalog` object in
`compiler/Analysis/libc_contracts.json` (an unknown top-level key, ignored by
the v2 contract reader). It is the single source for `-Wcplus-suggest-safe-s` /
`CPLUS-SUGGEST-SAFE-S`: every suggestion carries an
`ABI_PRESERVING_DROP_IN` or `ABI_CHANGING_ANNEX_K` tag, a fix-it, and where the
destination extent is known a capacity-filled fix-it. Compiler emission from
this catalog is a separate compiler change; this repository side owns the data,
the docs and the drift check.

`python3 tests/compiler/abi_gate/check_safe_s_catalog.py` requires the catalog
to match all shipped `_s` prototypes in `stdlib/safe/` and
`libc/install-include/`, and requires the three dedicated docs
([preserving](../SAFE_S_ABI_PRESERVING.md),
[non-preserving](../SAFE_S_NON_ABI_PRESERVING.md),
[matrix](../SAFE_S_COMPATIBILITY_MATRIX.md)) to mention exactly the catalogued
spellings. See [ABI_GATE.md](ABI_GATE.md) for the one-command runner.

## 5. Non-claims

- Evidence is host/toolchain-specific (macOS arm64 and Linux x86_64 runners in
  CI). It is not Windows x86/MSVC proof and not a memory-safety theorem.
- The gate detects *divergence*, not correctness; intentional ABI changes
  require a reviewed baseline/catalog/doc update, never an ignored failure.
- No live-game result is asserted here.
