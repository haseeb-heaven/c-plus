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
