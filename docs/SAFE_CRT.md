# SAFE CRT (Agent 2 / #22)

Status: **partial, verified slice**. Not a full ISO C replacement. Not a host CRT replacement.

## Scope

- Optional checked APIs in `stdlib/safe/` with C+ headers and compiled `libcplus_safecrt.a`.
- Bundled hardened libc slice in `libc/` (`libcplus_hardened.a`) for selective linking.
- Standard C names/signatures remain available from host libc; checked APIs are opt-in.

## Checked APIs (shipped)

| API | Kind | Impl | Evidence |
|---|---|---:|---|
| `cplus_memcpy_s` | checked | Intel safestringlib | unit + example + fuzz smoke |
| `cplus_memmove_s` | checked | Intel safestringlib | unit + example + fuzz smoke |
| `cplus_memset_s` | checked | Intel safestringlib | unit + example + fuzz smoke |
| `cplus_memcmp_s` | checked | Intel safestringlib | unit + example |
| `cplus_memzero_s` | checked | Intel safestringlib | unit + example |
| `cplus_strcpy_s` | checked | Intel safestringlib | unit + example |
| `cplus_strncpy_s` | checked | Intel safestringlib | unit + example |
| `cplus_strcat_s` | checked | Intel safestringlib | unit + example |
| `cplus_strncat_s` | checked | Intel safestringlib | unit + example |
| `cplus_strcmp_s` | checked | Intel safestringlib | unit + example |
| `cplus_strnlen_s` | checked | Intel safestringlib | unit + example |
| `cplus_fopen_s` | checked adapter | platform-backed | unit + example |
| `cplus_fclose_s` | checked adapter | platform-backed | unit + example |
| `cplus_fread_s` | checked adapter | platform-backed | unit + example |
| `cplus_fwrite_s` | checked adapter | platform-backed | unit + example |
| `cplus_snprintf_s` | checked adapter | platform-backed bounded vsnprintf | unit + example |
| `cplus_vsnprintf_s` | checked adapter | platform-backed | unit + example |
| `cplus_sprintf_s` | checked adapter | platform-backed | unit + example |
| `cplus_vsprintf_s` | checked adapter | platform-backed | unit + example |
| `cplus_printf_s` | adapter | platform-backed | unit + example |
| `cplus_fprintf_s` | adapter | platform-backed | unit + example |
| `cplus_scanf_s` | adapter | platform-backed | unit + example |
| `cplus_fscanf_s` | adapter | platform-backed | unit + example |
| `malloc_s/calloc_s/realloc_s/free_s` | C+ extension, not MS/Apple CRT | C+ safe_alloc.c | unit + example |

Opt-in familiar-name aliases: define `CPLUS_SAFE_USE_ANNEX_K_NAMES`.

## Logging

`safe_log.h` provides OFF/ERROR/WARN/INFO/DEBUG/TRACE. Disabled by default; metadata only; no buffer/secret dumping; no recursive libc wrapper logging.

## Limits

- Runtime checks do not prove raw pointer provenance/alias UAF.
- `strcpy`/`memcpy` raw C overloads cannot know unknown capacity; Agent 1 compiler proof/rejection required in strict safe mode.
- Platform-backed adapters may differ in Annex K semantics by platform; documented, not falsely claimed identical.
- ASan/libFuzzer not available on this macOS host; smoke fuzz only here. Linux CI required.
