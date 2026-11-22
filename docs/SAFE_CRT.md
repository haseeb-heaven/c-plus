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
- ASan, TSan, and libFuzzer require Linux CI (`.github/workflows/libc-crt.yml`) on this macOS arm64 host.
