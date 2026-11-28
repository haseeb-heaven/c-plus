# SAFE CRT (Agent 2)

Status: **verified checked slice**. Not a full ISO C replacement and not a host CRT replacement.

## Scope

- Optional checked APIs in `stdlib/safe/` with C+ headers and compiled `build/safe/libcplus_safecrt.a`.
- Bundled hardened libc slice in `libc/` (`build/libc/libcplus_hardened.a`) for selective linking.
- `scripts/build_safecrt.sh` builds the checked memory/string backend from **rurban/safeclib** (`cplus_sc_*` wrappers); `third_party/intel-safestringlib` is retained as unlinked vendored reference/provenance so two competing implementations are never linked.
- Allocator (`malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `aligned_alloc_s`) and stdio stream open/close (`cplus_fopen_s`, `cplus_fclose_s`) match standard C parameter lists and return types while providing metadata, overflow/alignment checks, double/invalid-free refusal, and stream double-close detection.

## Checked APIs (shipped)

| API | Kind | Backend / Impl | Evidence |
|---|---|---|---|
| `cplus_memcpy_s`, `cplus_memmove_s`, `cplus_memset_s`, `cplus_memcmp_s`, `cplus_memzero_s` | checked | rurban/safeclib | unit + example + adversarial + fuzz smoke |
| `cplus_strcpy_s`, `cplus_strncpy_s`, `cplus_strcat_s`, `cplus_strncat_s`, `cplus_strcmp_s`, `cplus_strnlen_s`, `cplus_strtok_s`, `cplus_strerror_s` | checked | rurban/safeclib | unit + example + adversarial + fuzz smoke |
| `malloc_s`, `calloc_s`, `realloc_s`, `free_s`, `free_checked_s`, `aligned_alloc_s` | checked allocator (standard C signatures) | `stdlib/safe/src/safe_alloc.c` | unit + example + adversarial + metadata + safety matrix |
| `cplus_safe_alloc_get_meta`, `cplus_safe_ptr_*`, `cplus_safe_query_length`, `cplus_safe_len`, `cplus_safe_transfer_ownership` | metadata & ownership | `stdlib/safe/src/safe_alloc.c` | `test_alloc_metadata_s.c`, `test_scenarios_c.c`, `test_scenarios_threads.c` |
| `cplus_fopen_s`, `cplus_fclose_s`, `cplus_fread_s`, `cplus_fwrite_s` | checked stdio | `stdlib/safe/src/safe_io.c` | unit + example + adversarial + fuzz |
| `cplus_snprintf_s`, `cplus_vsnprintf_s`, `cplus_sprintf_s`, `cplus_vsprintf_s` | bounded format | `stdlib/safe/src/safe_format.c` | unit + example + fuzz smoke |
| `cplus_printf_s`, `cplus_fprintf_s`, `cplus_scanf_s`, `cplus_fscanf_s` | adapter | `stdlib/safe/src/safe_format.c` | unit + example |
| `cplus_runtime_guard`, `cplus_runtime_push_cleanup`, `cplus_runtime_set_failstop` | recoverable runtime & LIFO cleanup | `stdlib/safe/src/safe_runtime.c` | `tests/runtime/` |

Opt-in Annex K name aliases: define `CPLUS_SAFE_USE_ANNEX_K_NAMES` before including SafeCRT headers where compatible.

## ABI-preserving ordinary-C checked layer (macro-mapped `_s`)

`stdlib/safe/safe_c.h` is an **optional, compile-time** layer over the APIs
above. It does not change the ABI or add a new backend; it maps ordinary C
spellings in the including translation unit onto the checked implementations
already listed in the table. Full mechanism, exact commands, ABI evidence and
non-claims are in [`docs/ABI_SAFE_LAYER.md`](ABI_SAFE_LAYER.md).

- **Original C source and exported symbols are unchanged.** The layer is a
  header of function-like macros (`safe_c.h:61-117`); it patches no `.c` file,
  renames no export, and exports nothing itself. New helper symbols on this
  branch are additive, not edits to existing symbols.
- **Macro-mapped at compile time, not interposed at link time.**
  `malloc/calloc/realloc/free` → `malloc_s/calloc_s/realloc_s/free_s`;
  `memset/memcpy/memmove/memcmp` and `strcpy/strncpy/strcat/strncat` →
  `cplus_*_s_auto`; `fread/fwrite` → `cplus_fread/cplus_fwrite`;
  `printf/fprintf` → `cplus_printf/cplus_fprintf`. Include the header first (or
  force-include it) and link `libcplus_safecrt.a` + `libcplus_hardened.a`;
  `CPLUS_SAFE_C_DISABLE` gives declarations only.
- **Precompiled libraries are unaffected.** A `.a` built without the header
  keeps its original calls. The `c_to_cp` lab records `c_to_cp: 24 passed,
  0 failed` and `30/30 calls match` for a C+ caller linking a precompiled C
  archive (`tests/abi/c_to_cp/README.md`,
  `tests/abi/c_to_cp/artifacts/results.log`); the separate
  `tests/abi/precompiled/` suite was PENDING at the ABI snapshot, so no result
  is claimed from it.
- **Valid input behaves identically at the data level** (`test_safe_c_api.c`
  asserts an in-bounds `memset` fills the same bytes); differences are added
  checks/diagnostics and defined failure behaviour, not a different data
  result for valid calls.
- **Invalid input is refused with documented safeclib codes.** Direct `_s`
  adapters return positive `errno_t` values (`ESNULLP` 400, `ESZEROL` 401,
  `ESLEMIN` 402, `ESLEMAX` 403, `ESOVRLP` 404, `ESNOSPC` 406, `ESUNTERM` 407;
  test `!= 0`, never `< 0`). The `_s_auto` wrappers used by the macros keep
  standard C return types: `errno = EINVAL` for NULL/unknown capacity, or
  `errno = ERANGE` plus a capacity diagnostic naming `ESNOSPC 406`
  (`docs/libc/SAFE_CRT_MEMORY_CONTRACT.md` §0, §3).
- **Unknown capacity remains unresolved.** When neither tracked metadata nor
  `__builtin_object_size` can size the object, default mode preserves raw-C
  behaviour with a warning and no safety claim; strict mode
  (`cplus_safe_set_unknown_capacity_mode(1)`) refuses with `errno = EINVAL`.
- **No Rust equivalence or full safety claim.** Pointer provenance, ownership,
  lifetime, alias safety and untrusted `printf` formats are not proven; the
  shipped header states this explicitly (`safe_memory.h:36-39`, `safe_c.h:38-40`).

ABI evidence: `build/abi/compat/results.log` (`abi_compat: 69 passed, 0 failed`,
39 audited symbol names, no ` T __Z`), `docs/abi/compatibility-matrix.md`,
`docs/stdlib/abi-preserving-runtime-safety.md` (70/70 layouts on both
toolchains), and `docs/stdlib/allocator/ABI_EVIDENCE.md` (`NO_ABI_IMPACT`).
All of it is macOS arm64 / Apple clang 16 only.

## Logging & Telemetry

- `safe_log.h`: OFF by default, metadata-only, never logs buffer contents or secrets.
- `safe_stats.h`: thread-safe registration mutex + atomic counters (`calls`/`ok`/`fail`/`double_free`/`invalid_free`). Diagnostic only.

## Limits

- Runtime checks do not prove raw pointer provenance, alias lifetimes, or unknown-capacity raw pointers; those belong to Agent 1 static analysis.
- ASan, TSan, and libFuzzer require Linux CI (`.github/workflows/libc-crt.yml`) on this macOS arm64 host. In the final Agent 2 sanitizer matrix (`tests/stdlib/agent2/SANITIZER_MATRIX.md`) ASan, Leak, Thread, Memory and CFI are UNSUPPORTED by the driver probe on the selected backend (`/usr/bin/clang`, Apple clang 16 / arm64): 45 of 72 builds rejected, 0 of 9 selected tests executed under ASan. No ASan result is claimed here.

## Integration inventory (2026-11-23)

Source: `docs/stdlib/upstream-reuse.md` §4 and `docs/libc/AUDIT_FINDINGS_TRACKER.md`.
Branch `agent/modern-stdlib`, PR #17 open (nothing pushed or merged).

| Category | Count (of 437 vendored safeclib public APIs) |
| --- | --- |
| Integrated | 40 of 437 |
| Adapter-only | 10 of 437 |
| Not integrated | 387 of 437 |

The 40 integrated APIs are the compiled checked backend built by
`scripts/build_safecrt.sh` from rurban/safeclib; the 10 adapter-only entries
are thin shims (e.g. checked-format adapters in `stdlib/safe/src/safe_format.c`).
The 387 not-integrated entries remain vendored/provenance only and are **not**
shipped as callable SafeCRT APIs. Intel SafeStringLib stays unlinked to avoid
two competing implementations. No new upstream source was added in this cycle.

Scope honesty:

- SafeCRT is a checked slice, not a full ISO C replacement: 27 of 948 ISO C
  APIs are MISSING and 49 of 948 UNSUPPORTED in `docs/libc/iso_c_inventory.json`.
- 127 wrapper-safety findings exist: 17 of 127 FIXED, 46 of 127 OPEN, 7 of 127
  GAP-COMPILER, 4 of 127 BLOCKED, 53 of 127 DOCUMENTED-CONTRACT.
- Recorded verification: 50 of 50 Agent 2 runner tests PASS; 23 of 23 evidence
  suites PASS; 23 of 27 executed sanitizer builds PASS, 4 of 27 FAIL under
  `--sanitize=integer` (runtime hashing, outside Agent 2 scope).
- Remote Linux CI, SonarCloud, Windows x86 and live-game results are
  **UNVERIFIED**. Passing local tests are observed behaviour, not a memory-safety
  proof. Agent 2 has stopped adding new stdlib surface; compiler-enforced
  safety (P0/P1/P2) is Agent 1 work.
