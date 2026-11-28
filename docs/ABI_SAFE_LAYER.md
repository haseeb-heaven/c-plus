# ABI-preserving checked layer for ordinary C names

- Date: 2026-11-23. Branch: `agent/modern-stdlib`.
- File ownership for this task: this document plus `docs/SAFE_CRT.md` and
  `docs/CRT_MATRIX.md`. No header, source, test, build script, `third_party/`
  file or git index entry was created or modified.
- Status: **documentation of an existing compile-time layer**. No new safety,
  ABI or portability result is claimed here; every count below is quoted from a
  named on-disk artifact or sibling document.

This document describes `stdlib/safe/safe_c.h`: the optional header that maps
ordinary ISO C call spellings (`malloc`, `memset`, `strcpy`, `fread`, `printf`,
…) onto the checked `_s` implementations already shipped in
`build/safe/libcplus_safecrt.a` and `build/libc/libcplus_hardened.a`. It is the
answer to the question "can callers get checked behaviour without rewriting
their C source or changing the binary contract?" — with the limits stated in
§5.

---

## 1. The ABI-preserving checked layer — exact claims

Each statement below is deliberately narrow. Words such as "unchanged" and
"identical" mean only what the bullet says; they are not general safety or
compatibility guarantees.

1. **Original C source is unchanged.** The mechanism is a public header of
   function-like macros (`stdlib/safe/safe_c.h:61-117`). No `.c` file in `libc/`
   or `stdlib/safe/` is patched, regenerated or conditionalised by this layer.
   The C library and SafeCRT libraries are built from the same sources whether
   or not any consumer includes the header (`scripts/build_safecrt.sh`,
   `tests/libc/build_libc.sh`).
2. **Exported symbols are unchanged.** No existing exported name is renamed,
   removed, retyped or given a different calling convention, and the macro
   layer itself exports **nothing**. The checked targets (`malloc_s`,
   `cplus_memset_s_auto`, `cplus_strcpy_s_auto`, `cplus_fread`, `cplus_printf`,
   …) were already public entry points before a consumer opts in. New helper
   symbols added on this branch are **additive**, not edits to an existing
   symbol; this document does not claim the archive's symbol set is byte-for-byte
   identical to any older release. The recorded `nm -gU` audit checks 22 + 10 +
   7 = 39 audited names across the three archives and finds no ` T __Z`
   C++-mangled symbols (`docs/abi/compatibility-matrix.md` §4.1, §8;
   `tests/abi/abi_compat_expected_{libc,safecrt,compat}_symbols.txt`).
3. **Ordinary names are macro-mapped at compile time, not at link time.**
   Inclusion of `safe_c.h` enables `CPLUS_SAFE_C_ENABLE` and rewrites the
   ordinary tokens in **that translation unit only**:
   `malloc/calloc/realloc/free` → `malloc_s/calloc_s/realloc_s/free_s`
   (`safe_c.h:68-75`); `memset/memcpy/memmove/memcmp` →
   `cplus_*_s_auto` (`:78-89`); `strcpy/strncpy/strcat/strncat` →
   `cplus_str*_s_auto` (`:92-103`); `fread/fwrite` →
   `cplus_fread/cplus_fwrite` (`:106-109`); `printf/fprintf` →
   `cplus_printf/cplus_fprintf` (`:112-115`). There is **no interposition** of
   host libc symbols and no change to `runtime/include/cplus/cplus.h`.
   Defining `CPLUS_SAFE_C_DISABLE` before the include gives declarations only
   (`safe_c.h:14-15,61-63`).
4. **Precompiled libraries are unaffected.** A `.a`/`.o` built without the
   header keeps its original call instructions and ABI; it is neither
   instrumented nor broken by linking the checked archives. The `c_to_cp` lab
   builds a plain-C archive with `/usr/bin/clang` and links it directly from a
   C+ caller: recorded `c_to_cp: 24 passed, 0 failed`, `30/30 calls match`,
   exit 0 (`tests/abi/c_to_cp/README.md`,
   `tests/abi/c_to_cp/artifacts/results.log`). This supersedes the
   4/4 PARTIAL figure in the older ABI snapshot
   (`docs/abi/compatibility-matrix.md` §4.3). The header-neutrality check
   compared layout output with and without the C+ headers and produced a
   byte-identical `diff` (§4.1). The separate `tests/abi/precompiled/` suite was
   **PENDING** at the ABI snapshot — do not read this bullet as that suite's
   result.
5. **Valid-input behaviour is identical at the data level.** For an in-bounds,
   known-capacity call the mapped wrapper performs the same operation and
   yields the same destination bytes / return value as the raw C call.
   `tests/crt/unit/test_safe_c_api.c:27-31` asserts an ordinary
   `memset(buf, 0x41, 64)` fills bytes 0 and 63; case (7) at `:70-78` asserts a
   stack array of known `__builtin_object_size` is filled in strict mode too.
   "Identical" excludes timing, added logging/statistics, and every failure
   path: refusals set `errno`, may print a diagnostic, and can leave a defined
   empty destination where raw C would have had undefined behaviour.
6. **Invalid input is refused with documented safeclib codes.** The two tiers
   report the refusal differently and the distinction matters:
   - Direct checked adapters return a positive safeclib `errno_t`
     (`stdlib/safe/safe_memory.h:24-30`):
     `ESNULLP 400`, `ESZEROL 401`, `ESLEMIN 402`, `ESLEMAX 403`,
     `ESOVRLP 404`, `ESNOSPC 406`, `ESUNTERM 407`. Test with `!= 0`, **never**
     `< 0`.
   - The `_s_auto` wrappers used by the ordinary-name macros keep the standard
     C return type and signal refusal through `errno = EINVAL` (NULL pointer or
     strict-mode unknown capacity), `errno = ERANGE` plus a violation
     diagnostic naming `ESNOSPC 406` (capacity/size overflow), and defined
     sentinels such as `NULL`, `0`, `-1` or an empty destination
     (`stdlib/safe/src/safe_memory.c:108-136,138-252`;
     `docs/libc/SAFE_CRT_MEMORY_CONTRACT.md` §0 and §3). The recorded demo
     diagnostic is `[CPLUS-SAFE] memset_s: buffer overflow refused (requested
     16 > capacity 0, ESNOSPC 406)` and exit status 70, not SIGSEGV
     (`docs/libc/ORDINARY_C_SAFETY.md` "Observed behaviour").
7. **The unknown-capacity limitation remains.** Capacity is
   `min(tracked remaining capacity, __builtin_object_size(p,1))`; when neither
   source can size the object, capacity is **unknown, never verified**
   (`stdlib/safe/src/safe_memory.c:94-122`). Default mode preserves raw-C
   behaviour and emits a warning but makes **no safety claim**; strict mode
   with `cplus_safe_set_unknown_capacity_mode(1)` refuses with `errno = EINVAL`
   (`safe_memory.h:55-59`). Source capacity of a bounded copy is not
   independently proven, and raw `memcpy` with overlapping operands is still
   caller UB — use `memmove` (`docs/libc/ORDINARY_C_SAFETY.md` "Honest limits").
8. **No Rust equivalence and no full safety claim.** The shipped header says so
   verbatim: the checks "do NOT prove pointer provenance, ownership, lifetime,
   or alias/UAF safety — this is not Rust-grade safety"
   (`stdlib/safe/safe_memory.h:36-39`). Raw `printf` with an untrusted format
   remains unsafe; the checked `_s` format API is the safe path
   (`safe_c.h:38-40`). Passing ABI or unit tests is contract-conformance
   evidence, not a memory-safety proof, and says nothing about Windows x86,
   Wine, DLL injection or live-game behaviour.

---

## 2. Where the mapping lives

| Ordinary spelling | Compile-time target | Source of the macro |
|---|---|---|
| `malloc`, `calloc`, `realloc`, `free` | `malloc_s`, `calloc_s`, `realloc_s`, `free_s` | `stdlib/safe/safe_c.h:68-75` |
| `memset`, `memcpy`, `memmove`, `memcmp` | `cplus_memset_s_auto`, `cplus_memcpy_s_auto`, `cplus_memmove_s_auto`, `cplus_memcmp_s_auto` | `stdlib/safe/safe_c.h:78-89` |
| `strcpy`, `strncpy`, `strcat`, `strncat` | `cplus_strcpy_s_auto`, `cplus_strncpy_s_auto`, `cplus_strcat_s_auto`, `cplus_strncat_s_auto` | `stdlib/safe/safe_c.h:92-103` |
| `fread`, `fwrite` | `cplus_fread`, `cplus_fwrite` | `stdlib/safe/safe_c.h:106-109` |
| `printf`, `fprintf` | `cplus_printf`, `cplus_fprintf` (Clang `format` attributes → compile-time argument checking) | `stdlib/safe/safe_c.h:111-115` |

`CPLUS_SAFE_OBJ_SIZE(p)` is `__builtin_object_size((p), 1)`, i.e. the exact
enclosing object for stack/global storage; on a compiler without the builtin it
is `(size_t)-1` ("unknown") and rule 7 applies
(`stdlib/safe/safe_memory.h:61-67`). Interior heap pointers resolve to the
**remaining** capacity of their tracked block, including zero at the
one-past-the-end address (`docs/libc/ORDINARY_C_SAFETY.md`
"Capacity provenance (the critical fix)").

Companion documentation: `docs/libc/ORDINARY_C_SAFETY.md` (ordinary-C activation
and both shipped layers), `docs/libc/SAFE_CRT_MEMORY_CONTRACT.md` (per-function
error/atomicity contract), `docs/SAFE_CRT.md` and `docs/CRT_MATRIX.md`
(integration inventory).

---

## 3. Pointers to ABI evidence

All completed evidence is from **macOS arm64, Apple clang 16, C+ 0.1.0** only.
The ABI snapshot is `2026-11-23T03:58:05Z`, branch `agent/modern-stdlib`,
recorded HEAD `cbcea57174e5c6b26d07bcc4e1b5c8f7501b340a` and is explicitly a
snapshot, not a live dashboard (`docs/abi/README.md`,
`docs/abi/compatibility-matrix.md`).

| Evidence | Recorded result | Where |
|---|---|---|
| Baseline ABI suite (`sh tests/abi/run_abi_compat.sh`) | `RESULT: abi_compat: 69 passed, 0 failed`; 0 timeouts in 69 runner steps; includes 22+10+7 symbol names, no ` T __Z`, both link directions, header-neutrality `diff` exit 0 | `build/abi/compat/results.log`; `docs/abi/compatibility-matrix.md` §4.1 |
| Program-internal summaries | `20/20` pointer ABI, `80/80` layouts, `13/13` C→C+, `10/10` C+→C | `build/abi/compat/{pointers,layouts,c_driver,cplus_caller}.out` |
| Layout probe, two toolchains | `70/70 layouts match` on both `/usr/bin/clang` and `./build/c+`; `diff -u build/abi/host.out build/abi/cplus.out` byte-identical | `docs/stdlib/abi-preserving-runtime-safety.md` §2.2; `build/abi/host.out`, `build/abi/cplus.out` |
| Thin pointers | `sizeof(void*) = sizeof(size_t) = sizeof(unsigned long) = 8` in both toolchains; metadata is out-of-band | `docs/stdlib/abi-preserving-runtime-safety.md` §2.1 |
| Source-level header audit | `NO_ABI_IMPACT` for the audited header series; no consumer-visible `ABI_CHANGE` | `docs/abi/source_audit.md` |
| Allocator ABI evidence | `NO_ABI_IMPACT` verdict with caveats (Darwin/arm64 only; additive metadata structs are a new stability obligation) | `docs/stdlib/allocator/ABI_EVIDENCE.md` §4 |
| Precompiled plain-C archive called from C+ | newer sibling lab: `c_to_cp: 24 passed, 0 failed`, `30/30 calls match`, rc=0; older ABI snapshot recorded only 4/4 observed values and marked the area PARTIAL | `tests/abi/c_to_cp/README.md`; `tests/abi/c_to_cp/artifacts/results.log`; `docs/abi/compatibility-matrix.md` §4.3 |
| Ordinary-name checked layer behaviour | `test_safe_c_api.c` cases 1–7; recorded demo refusal `ESNOSPC 406`, exit 70 | `tests/crt/unit/test_safe_c_api.c`; `docs/libc/ORDINARY_C_SAFETY.md` |

Reading constraints that must accompany those numbers:

- `tests/abi/legacy_c/`, `tests/abi/cp_to_c/`, `tests/abi/precompiled/` and
  `tests/abi/fingerprints/` were **PENDING** at capture; PENDING is neither
  pass nor failure (`docs/abi/compatibility-matrix.md` §3, §7 item 5).
- `./build/c+` was rebuilt externally and some paths hit an unrelated
  `CPLUS_EXIT_ARITHMETIC` runtime mismatch; that is an external
  compiler/runtime blocker, **not** an ABI mismatch found by these probes
  (`docs/abi/compatibility-matrix.md` §6;
  `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`).
- Windows x86/MSVC-UCRT, Linux, varargs and disassembly-level calling
  convention are **UNTESTED**; no mangled C++ ABI is exercised
  (`docs/abi/compatibility-matrix.md` §2, §7).

---

## 4. Exact reproduction commands

Run from the repository root (`$ROOT`) with `/usr/bin/clang`. These commands
reproduce the evidence above; they do not establish new claims. Build products
go to `build/` or to the session temp directory, never to `third_party/` or the
git index, and they are separate from this documentation task.

```sh
# 1. Build the two shipped archives from unchanged C sources.
sh scripts/build_safecrt.sh          # -> build/safe/libcplus_safecrt.a
sh tests/libc/build_libc.sh          # -> build/libc/libcplus_hardened.a

# 2. Preprocess an ordinary-name caller: the rewrite is visible as a macro
#    expansion in THIS translation unit; no library source is touched.
/usr/bin/clang -std=c99 -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  -E scratch/demo.c | grep -m2 -E 'memset\(buffer|cplus_memset_s_auto'
#    Observed: a declaration of cplus_memset_s_auto and the rewritten call
#    with __builtin_object_size(...).
#    With -DCPLUS_SAFE_C_DISABLE the same source keeps ordinary declarations.

# 3. Compile and run the ordinary-name regression (no `_s` spelling in main).
mkdir -p build/safe/unit
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  tests/crt/unit/test_safe_c_api.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o build/safe/unit/test_safe_c_api
build/safe/unit/test_safe_c_api        # prints: safe_c_api: ok
#    Or run the whole unit group: sh tests/crt/unit/run_unit_tests.sh

# 4. Reproduce the checked refusal with ordinary spellings only.
TMP=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode
/usr/bin/clang -std=c99 -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  scratch/demo.c build/safe/libcplus_safecrt.a \
  build/libc/libcplus_hardened.a -lm -pthread -o "$TMP/demo_ordinary_c"
"$TMP/demo_ordinary_c"                 # prints the refused-write diagnostic
echo $?                                # 70 (recorded); not SIGSEGV
#    Recorded stdout is quoted in docs/libc/ORDINARY_C_SAFETY.md.

# 5. Re-run the authoritative ABI suite and inspect the audited exports.
sh tests/abi/run_abi_compat.sh
tail -1 build/abi/compat/results.log   # RESULT: abi_compat: 69 passed, 0 failed
/usr/bin/nm -gU build/safe/libcplus_safecrt.a \
  | grep ' T __Z' || echo 'none'        # expect: none
wc -l tests/abi/abi_compat_expected_*_symbols.txt   # 22 + 10 + 7 = 39

# 6. Re-check layout neutrality (two toolchains, byte-identical output).
sh tests/abi/run_abi_layouts.sh
diff -u build/abi/host.out build/abi/cplus.out
tail -1 build/abi/host.out

# 7. Re-run the precompiled-plain-C ↔ C+ boundary lab (sibling result).
sh tests/abi/c_to_cp/run.sh
tail -1 tests/abi/c_to_cp/artifacts/results.log
```

The driver-based steps in the ABI matrix additionally use
`./build/c+ build --sanitize=none …`; their exact command list is in
`docs/abi/compatibility-matrix.md` §4.1 and `tests/abi/ABI_COMPAT_NOTES.md`.
The external `CPLUS_EXIT_ARITHMETIC` blocker (§3) can affect steps that rebuild
the instrumented runtime; it is out of scope for this documentation task.

---

## 5. Non-claims and limits

1. **Not a replacement CRT.** SafeCRT is a checked slice: 40 of 437 vendored
   safeclib APIs integrated, 10 adapter-only, 387 not integrated; 27 of 948 ISO
   C APIs MISSING and 49 UNSUPPORTED in `docs/libc/iso_c_inventory.json`
   (`docs/SAFE_CRT.md` "Integration inventory").
2. **Not a safety proof.** Runtime checks cannot prove pointer provenance,
   alias lifetimes, data-race freedom, or unknown-capacity raw pointers; UAF
   classification is best-effort and the recently-free cache is bounded
   (`docs/stdlib/abi-preserving-runtime-safety.md` §3;
   `docs/libc/SAFE_CRT_MEMORY_CONTRACT.md` §0).
3. **Not "all inputs are safe".** Unknown capacity is unchecked by default
   (§1 item 7); `printf` format validity is the caller's responsibility for
   raw calls; overlapping raw `memcpy` is UB.
4. **Not cross-platform.** No Windows x86, Linux, Wine, DLL-injection or
   live-game result is asserted by this document; the only completed ABI
   evidence is macOS arm64 / Apple clang 16.
5. **No new verification by this author.** Commands in §4 are reproduction
   instructions. The counts and diagnostic strings quoted here come from the
   named artifacts; no test runner was executed while writing this document.

## 6. Evidence inventory (read for this document)

- `stdlib/safe/safe_c.h`, `stdlib/safe/safe_memory.h`,
  `stdlib/safe/src/safe_memory.c`, `scripts/build_safecrt.sh`,
  `tests/libc/build_libc.sh`
- `tests/crt/unit/test_safe_c_api.c`, `tests/crt/unit/run_unit_tests.sh`,
  `scratch/demo.c`
- `docs/libc/ORDINARY_C_SAFETY.md`, `docs/libc/SAFE_CRT_MEMORY_CONTRACT.md`,
  `docs/SAFE_CRT.md`, `docs/CRT_MATRIX.md`
- `docs/abi/README.md`, `docs/abi/compatibility-matrix.md`,
  `docs/abi/source_audit.md`, `docs/stdlib/abi-preserving-runtime-safety.md`,
  `docs/stdlib/allocator/ABI_EVIDENCE.md`,
  `docs/stdlib/AGENT1_EXTERNAL_RUNTIME_BLOCKER.md`
- On disk: `build/abi/compat/results.log`, `build/abi/compat/*.out`,
  `build/abi/host.out`, `build/abi/cplus.out`,
  `tests/abi/abi_compat_expected_*_symbols.txt`, `tests/abi/c_to_cp/README.md`,
  `tests/abi/c_to_cp/artifacts/results.log`,
  `tests/abi/c_to_cp/artifacts/caller.stdout`
