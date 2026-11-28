# Exhaustive ISO C API Inventory (27 headers)

Branch `agent/modern-stdlib`. C17 baseline (ISO/IEC 9899:2018); C23 additions tagged `C23`. Generated companion to `iso_c_inventory.json`.

## Classification legend

- `IMPLEMENTED_BUNDLED`: Compiled in this repo from bundled/upstream source (musl) or direct libc/src implementation; includes symbol_map VENDORED and IMPLEMENTED.
- `PLATFORM_BACKED`: Namespaced cplus_* wrapper delegating to a named host C runtime backend; host semantics preserved.
- `PARTIAL`: Only a subset of the header surface is available.
- `MISSING`: No implementation in this repo and no host/namespaced wrapper entry point.
- `UNSUPPORTED`: Deliberately not shipped; exact reason recorded in limitation.
- `HOST/COMPILER-PROVIDED`: Toolchain/freestanding header or compile-time macro/type; no runtime symbol and not implemented by this library (task step 4).

> Scope note: the C17 baseline surface is exhaustive. C23 tracking covers the explicitly named APIs (memset_explicit, strlcpy, strlcat, FE_TIESAWAY, timespec_getres, char8_t/mbrtoc8/c8rtomb, atomic fetch_and/nand/wait/notify) plus representative new math families; the exhaustive _FloatN/_FloatNx overload matrix of C23 is not enumerated.

## Summary counts

| Classification | APIs |
|---|---:|
| `HOST/COMPILER-PROVIDED` | 359 |
| `IMPLEMENTED_BUNDLED` | 26 |
| `MISSING` | 117 |
| `PLATFORM_BACKED` | 397 |
| `UNSUPPORTED` | 49 |
| **TOTAL** | **948** |

By C version: C11=125, C23=47, C89=301, C95=66, C99=409

## Per-header counts

| Header | Total | Bundled | Platform | Partial | Missing | Unsupported | Host/compiler |
|---|---:|---:|---:|---:|---:|---:|---:|
| `assert.h` | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| `ctype.h` | 14 | 0 | 14 | 0 | 0 | 0 | 0 |
| `errno.h` | 4 | 0 | 1 | 0 | 0 | 0 | 3 |
| `float.h` | 39 | 0 | 0 | 0 | 0 | 0 | 39 |
| `limits.h` | 19 | 0 | 0 | 0 | 0 | 0 | 19 |
| `locale.h` | 9 | 0 | 2 | 0 | 0 | 0 | 7 |
| `math.h` | 223 | 0 | 166 | 0 | 34 | 0 | 23 |
| `setjmp.h` | 3 | 0 | 0 | 0 | 0 | 0 | 3 |
| `signal.h` | 12 | 0 | 2 | 0 | 0 | 0 | 10 |
| `stdalign.h` | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| `stdarg.h` | 5 | 0 | 0 | 0 | 0 | 0 | 5 |
| `stdatomic.h` | 49 | 0 | 0 | 0 | 0 | 34 | 15 |
| `stdbool.h` | 4 | 0 | 0 | 0 | 0 | 0 | 4 |
| `stddef.h` | 6 | 0 | 0 | 0 | 0 | 0 | 6 |
| `stdint.h` | 89 | 0 | 0 | 0 | 0 | 0 | 89 |
| `stdio.h` | 75 | 0 | 43 | 0 | 1 | 13 | 18 |
| `stdlib.h` | 49 | 4 | 34 | 0 | 0 | 2 | 9 |
| `stdnoreturn.h` | 1 | 0 | 0 | 0 | 0 | 0 | 1 |
| `string.h` | 28 | 22 | 5 | 0 | 1 | 0 | 0 |
| `tgmath.h` | 71 | 0 | 0 | 0 | 0 | 0 | 71 |
| `threads.h` | 41 | 0 | 0 | 0 | 41 | 0 | 0 |
| `time.h` | 17 | 0 | 10 | 0 | 1 | 0 | 6 |
| `uchar.h` | 9 | 0 | 0 | 0 | 9 | 0 | 0 |
| `wchar.h` | 59 | 0 | 43 | 0 | 11 | 0 | 5 |
| `wctype.h` | 21 | 0 | 18 | 0 | 0 | 0 | 3 |
| `complex.h` | 69 | 0 | 48 | 0 | 18 | 0 | 3 |
| `fenv.h` | 25 | 0 | 11 | 0 | 1 | 0 | 13 |

## Host probe: threads.h / uchar.h

Probed with `/usr/bin/clang --version` → Apple clang version 16.0.0 (clang-1600.0.26.6), target arm64-apple-darwin27.2.0.

```
$ cat > /tmp/probe.c
#include <threads.h>
#include <uchar.h>
int main(void){thrd_t t; mtx_t m; cnd_t c; char16_t a; char32_t b;(void)t;(void)m;(void)c;(void)a;(void)b;return 0;}

$ /usr/bin/clang -std=c11 /tmp/probe.c -o /tmp/probe
probe.c:1:10: fatal error: 'threads.h' file not found
1 error generated.  (EXIT=1)

$ /usr/bin/clang -std=c17 /tmp/probe.c -o /tmp/probe
probe.c:1:10: fatal error: 'threads.h' file not found
1 error generated.  (EXIT=1)

$ /usr/bin/clang -std=c11 /tmp/probe_uchar.c -o /tmp/pu
probe_uchar.c:1:10: fatal error: 'uchar.h' file not found
1 error generated.  (EXIT=1)
```

**Result:** the host toolchain provides **neither** `<threads.h>` **nor** `<uchar.h>` under `-std=c11` or `-std=c17`. Consequently all C11 threads and uchar APIs are `MISSING`. The pthread-backed `cplus_pthread_*` wrappers in `libc/src/thread/thread.c` are POSIX equivalents, not the ISO C11 `<threads.h>` API.

## Missing / partial by priority

### `math.h`
- `MISSING` **exp10** (C23)
- `MISSING` **exp10f** (C23)
- `MISSING` **exp10l** (C23)
- `MISSING` **exp16** (C23)
- `MISSING` **fadd** (C23)
- `MISSING` **faddf** (C23)
- `MISSING` **faddl** (C23)
- `MISSING` **fdiv** (C23)
- `MISSING` **fdivf** (C23)
- `MISSING` **fdivl** (C23)
- `MISSING` **fma16** (C23)
- `MISSING` **fmul** (C23)
- `MISSING` **fmulf** (C23)
- `MISSING` **fmull** (C23)
- `MISSING` **frexp16** (C23)
- `MISSING` **fsub** (C23)
- `MISSING` **fsubf** (C23)
- `MISSING` **fsubl** (C23)
- `MISSING` **hypot (3-argument C23 overload)** (C23)
- `MISSING` **llround** (C99)
- `MISSING` **logb16** (C23)
- `MISSING` **lround** (C99)
- `MISSING` **nextdown** (C23)
- `MISSING` **nextdownf** (C23)
- `MISSING` **nextdownl** (C23)
- `MISSING` **nexttoward** (C99)
- `MISSING` **nextup** (C23)
- `MISSING` **nextupf** (C23)
- `MISSING` **nextupl** (C23)
- `MISSING` **rint** (C99)
- `MISSING` **roundeven** (C23)
- `MISSING` **roundevenf** (C23)
- `MISSING` **roundevenl** (C23)
- `MISSING` **scalbln** (C99)

### `stdatomic.h`
- `UNSUPPORTED` **atomic_compare_exchange_strong** (C11)
- `UNSUPPORTED` **atomic_compare_exchange_strong_explicit** (C11)
- `UNSUPPORTED` **atomic_compare_exchange_weak** (C11)
- `UNSUPPORTED` **atomic_compare_exchange_weak_explicit** (C11)
- `UNSUPPORTED` **atomic_exchange** (C11)
- `UNSUPPORTED` **atomic_exchange_explicit** (C11)
- `UNSUPPORTED` **atomic_fetch_add** (C11)
- `UNSUPPORTED` **atomic_fetch_add_explicit** (C11)
- `UNSUPPORTED` **atomic_fetch_and** (C23)
- `UNSUPPORTED` **atomic_fetch_and_explicit** (C23)
- `UNSUPPORTED` **atomic_fetch_nand** (C23)
- `UNSUPPORTED` **atomic_fetch_nand_explicit** (C23)
- `UNSUPPORTED` **atomic_fetch_or** (C11)
- `UNSUPPORTED` **atomic_fetch_or_explicit** (C11)
- `UNSUPPORTED` **atomic_fetch_sub** (C11)
- `UNSUPPORTED` **atomic_fetch_sub_explicit** (C11)
- `UNSUPPORTED` **atomic_fetch_xor** (C11)
- `UNSUPPORTED` **atomic_fetch_xor_explicit** (C11)
- `UNSUPPORTED` **atomic_flag_clear** (C11)
- `UNSUPPORTED` **atomic_flag_clear_explicit** (C11)
- `UNSUPPORTED` **atomic_flag_test_and_set** (C11)
- `UNSUPPORTED` **atomic_flag_test_and_set_explicit** (C11)
- `UNSUPPORTED` **atomic_init** (C11)
- `UNSUPPORTED` **atomic_is_lock_free** (C11)
- `UNSUPPORTED` **atomic_load** (C11)
- `UNSUPPORTED` **atomic_load_explicit** (C11)
- `UNSUPPORTED` **atomic_notify_all** (C23)
- `UNSUPPORTED` **atomic_notify_one** (C23)
- `UNSUPPORTED` **atomic_signal_fence** (C11)
- `UNSUPPORTED` **atomic_store** (C11)
- `UNSUPPORTED` **atomic_store_explicit** (C11)
- `UNSUPPORTED` **atomic_thread_fence** (C11)
- `UNSUPPORTED` **atomic_wait** (C23)
- `UNSUPPORTED` **kill_dependency** (C11)

### `stdio.h`
- `MISSING` **vscanf** (C99)
- `UNSUPPORTED` **fclose_s** (C11)
- `UNSUPPORTED` **fopen_s** (C11)
- `UNSUPPORTED` **fprintf_s** (C11)
- `UNSUPPORTED` **fread_s** (C11)
- `UNSUPPORTED` **fscanf_s** (C11)
- `UNSUPPORTED` **gets** (C11)
- `UNSUPPORTED` **gets_s** (C11)
- `UNSUPPORTED` **printf_s** (C11)
- `UNSUPPORTED` **scanf_s** (C11)
- `UNSUPPORTED` **sprintf_s** (C11)
- `UNSUPPORTED` **sscanf_s** (C11)
- `UNSUPPORTED` **tmpnam** (C89)
- `UNSUPPORTED` **tmpnam_s** (C11)

### `stdlib.h`
- `UNSUPPORTED` **bsearch_s** (C11)
- `UNSUPPORTED` **qsort_s** (C11)

### `string.h`
- `MISSING` **memset_explicit** (C23)

### `threads.h`
- `MISSING` **ONCE_FLAG_INIT** (C11)
- `MISSING` **TSS_DTOR_ITERATIONS** (C11)
- `MISSING` **call_once** (C11)
- `MISSING` **cnd_broadcast** (C11)
- `MISSING` **cnd_destroy** (C11)
- `MISSING` **cnd_init** (C11)
- `MISSING` **cnd_signal** (C11)
- `MISSING` **cnd_t** (C11)
- `MISSING` **cnd_timedwait** (C11)
- `MISSING` **cnd_wait** (C11)
- `MISSING` **mtx_destroy** (C11)
- `MISSING` **mtx_init** (C11)
- `MISSING` **mtx_lock** (C11)
- `MISSING` **mtx_plain** (C11)
- `MISSING` **mtx_recursive** (C11)
- `MISSING` **mtx_t** (C11)
- `MISSING` **mtx_timed** (C11)
- `MISSING` **mtx_timedlock** (C11)
- `MISSING` **mtx_trylock** (C11)
- `MISSING` **mtx_unlock** (C11)
- `MISSING` **once_flag** (C11)
- `MISSING` **thrd_busy** (C11)
- `MISSING` **thrd_create** (C11)
- `MISSING` **thrd_current** (C11)
- `MISSING` **thrd_detach** (C11)
- `MISSING` **thrd_equal** (C11)
- `MISSING` **thrd_error** (C11)
- `MISSING` **thrd_exit** (C11)
- `MISSING` **thrd_join** (C11)
- `MISSING` **thrd_nomem** (C11)
- `MISSING` **thrd_sleep** (C11)
- `MISSING` **thrd_start_t** (C11)
- `MISSING` **thrd_success** (C11)
- `MISSING` **thrd_t** (C11)
- `MISSING` **thrd_timedout** (C11)
- `MISSING` **thrd_yield** (C11)
- `MISSING` **tss_create** (C11)
- `MISSING` **tss_delete** (C11)
- `MISSING` **tss_get** (C11)
- `MISSING` **tss_set** (C11)
- `MISSING` **tss_t** (C11)

### `time.h`
- `MISSING` **timespec_getres** (C23)

### `uchar.h`
- `MISSING` **c16rtomb** (C11)
- `MISSING` **c32rtomb** (C11)
- `MISSING` **c8rtomb** (C23)
- `MISSING` **char16_t** (C11)
- `MISSING` **char32_t** (C11)
- `MISSING` **char8_t** (C23)
- `MISSING` **mbrtoc16** (C11)
- `MISSING` **mbrtoc32** (C11)
- `MISSING` **mbrtoc8** (C23)

### `wchar.h`
- `MISSING` **fgetws** (C95)
- `MISSING` **fputws** (C95)
- `MISSING` **mbsinit** (C95)
- `MISSING` **wcschr** (C95)
- `MISSING` **wcscoll** (C95)
- `MISSING` **wcscspn** (C95)
- `MISSING` **wcspbrk** (C95)
- `MISSING` **wcsrchr** (C95)
- `MISSING` **wcsspn** (C95)
- `MISSING` **wcsxfrm** (C95)
- `MISSING` **wmemchr** (C99)

### `complex.h`
- `MISSING` **cacos** (C99)
- `MISSING` **cacosf** (C99)
- `MISSING` **cacosh** (C99)
- `MISSING` **cacoshf** (C99)
- `MISSING` **cacoshl** (C99)
- `MISSING` **cacosl** (C99)
- `MISSING` **casin** (C99)
- `MISSING` **casinf** (C99)
- `MISSING` **casinh** (C99)
- `MISSING` **casinhf** (C99)
- `MISSING` **casinhl** (C99)
- `MISSING` **casinl** (C99)
- `MISSING` **catan** (C99)
- `MISSING` **catanf** (C99)
- `MISSING` **catanh** (C99)
- `MISSING` **catanhf** (C99)
- `MISSING` **catanhl** (C99)
- `MISSING` **catanl** (C99)

### `fenv.h`
- `MISSING` **FE_TIESAWAY** (C23)

## Per-header API tables

### `assert.h` — 3 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| assert | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler preprocessor macro from host <assert.h>; NDEBUG controls it. Not a linkable symbol; cannot be meaningfully wrapped. |
| static_assert | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | C11 macro in <assert.h>; C23 promotes it to a keyword. No runtime symbol; toolchain-provided. |
| NDEBUG | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <assert.h>. |

### `ctype.h` — 14 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| isalnum | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_isalnum | ctype.h:isalnum | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| isalpha | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_isalpha | ctype.h:isalpha | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| isblank | function | C99 | `PLATFORM_BACKED` | libc/src/ctype/ctype2.c | cplus_libc.h:cplus_isblank | ctype.h:isblank | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| iscntrl | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_iscntrl | ctype.h:iscntrl | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| isdigit | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_isdigit | ctype.h:isdigit | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| isgraph | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_isgraph | ctype.h:isgraph | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| islower | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_islower | ctype.h:islower | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| isprint | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_isprint | ctype.h:isprint | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| ispunct | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_ispunct | ctype.h:ispunct | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| isspace | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_isspace | ctype.h:isspace | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| isupper | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_isupper | ctype.h:isupper | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| isxdigit | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_isxdigit | ctype.h:isxdigit | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| tolower | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_tolower | ctype.h:tolower | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |
| toupper | function | C89 | `PLATFORM_BACKED` | libc/src/ctype/ctype.c | cplus_libc.h:cplus_toupper | ctype.h:toupper | tests/libc/test_ctype.c | Delegates to host <ctype.h>; argument is cast to unsigned char (EOF-safe for tolower/toupper). Raw pointer/locale state remains caller responsibility. |

### `errno.h` — 4 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| EDOM | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time value macro from host <errno.h>; no runtime symbol to wrap. |
| ERANGE | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time value macro from host <errno.h>; no runtime symbol to wrap. |
| EILSEQ | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time value macro from host <errno.h>; no runtime symbol to wrap. |
| errno | macro | C89 | `PLATFORM_BACKED` | libc/src/errno/errno.c | — | errno.h:errno | tests/libc/test_libc_basic.c | Host errno accessed via __cplus_errno_location() (__thread TLS). The ISO macro itself is host-provided; this library only adds a namespaced accessor plus errno_t/RSIZE_MAX_* helpers. |

### `float.h` — 39 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| FLT_ROUNDS | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_RADIX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_MANT_DIG | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_DIG | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_EPSILON | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_MIN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_MIN_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_MAX_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_MIN_10_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_MAX_10_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_MANT_DIG | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_DIG | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_EPSILON | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_MIN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_MIN_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_MAX_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_MIN_10_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_MAX_10_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_MANT_DIG | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_DIG | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_EPSILON | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_MIN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_MIN_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_MAX_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_MIN_10_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_MAX_10_EXP | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_EVAL_METHOD | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_HAS_SUBNORM | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_HAS_SUBNORM | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_HAS_SUBNORM | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_DECIMAL_DIG | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_DECIMAL_DIG | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_DECIMAL_DIG | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| FLT_TRUE_MIN | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| DBL_TRUE_MIN | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |
| LDBL_TRUE_MIN | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <float.h>; no runtime symbol. |

### `limits.h` — 19 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| CHAR_BIT | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| SCHAR_MIN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| SCHAR_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| UCHAR_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| CHAR_MIN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| CHAR_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| MB_LEN_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| SHRT_MIN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| SHRT_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| USHRT_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| INT_MIN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| INT_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| UINT_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| LONG_MIN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| LONG_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| ULONG_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| LLONG_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| LLONG_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |
| ULLONG_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time constant macro from toolchain freestanding <limits.h>; no runtime symbol. |

### `locale.h` — 9 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| LC_ALL | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time enum constant from host <locale.h>. |
| LC_COLLATE | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time enum constant from host <locale.h>. |
| LC_CTYPE | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time enum constant from host <locale.h>. |
| LC_MONETARY | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time enum constant from host <locale.h>. |
| LC_NUMERIC | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time enum constant from host <locale.h>. |
| LC_TIME | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time enum constant from host <locale.h>. |
| struct lconv | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Struct layout defined by host <locale.h>; cplus_localeconv returns a pointer to host-owned static storage. |
| setlocale | function | C89 | `PLATFORM_BACKED` | libc/src/locale/locale.c | cplus_libc.h:cplus_setlocale | locale.h:setlocale | tests/libc/test_math_time_locale.c | Delegates to host setlocale; returned string is host-owned and may be overwritten by later calls. |
| localeconv | function | C89 | `PLATFORM_BACKED` | libc/src/locale/locale.c | cplus_libc.h:cplus_localeconv | locale.h:localeconv | tests/libc/test_math_time_locale.c | Delegates to host localeconv; returns pointer to host static struct lconv, not thread-safe. |

### `math.h` — 223 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| acos | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_acos | math.h:acos | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| asin | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_asin | math.h:asin | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| atan | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_atan | math.h:atan | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| atan2 | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_atan2 | math.h:atan2 | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| cos | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_cos | math.h:cos | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| sin | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_sin | math.h:sin | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| tan | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_tan | math.h:tan | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| acosh | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_acosh | math.h:acosh | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| asinh | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_asinh | math.h:asinh | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| atanh | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_atanh | math.h:atanh | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| cosh | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_cosh | math.h:cosh | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| sinh | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_sinh | math.h:sinh | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| tanh | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_tanh | math.h:tanh | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| exp | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_exp | math.h:exp | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| exp2 | function | C99 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_exp2 | math.h:exp2 | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| expm1 | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_expm1 | math.h:expm1 | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| log | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_log | math.h:log | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| log10 | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_log10 | math.h:log10 | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| log1p | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_log1p | math.h:log1p | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| log2 | function | C99 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_log2 | math.h:log2 | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| pow | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_pow | math.h:pow | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| sqrt | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_sqrt | math.h:sqrt | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| cbrt | function | C99 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_cbrt | math.h:cbrt | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| hypot | function | C99 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_hypot | math.h:hypot | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| ceil | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_ceil | math.h:ceil | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| floor | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_floor | math.h:floor | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| round | function | C99 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_round | math.h:round | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| trunc | function | C99 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_trunc | math.h:trunc | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| nearbyint | function | C99 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_nearbyint | math.h:nearbyint | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| rint | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_gaps.c | cplus_libc.h:cplus_rint | math.h:rint | tests/libc/test_math_gaps.c | Host-delegated; host controls errno/FP semantics. |
| fabs | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_fabs | math.h:fabs | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| ldexp | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_ldexp | math.h:ldexp | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| lgamma | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_lgamma | math.h:lgamma | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| logb | function | C99 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_logb | math.h:logb | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| modf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_modf | math.h:modf | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| scalbn | function | C89 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_scalbn | math.h:scalbn | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| frexp | function | C89 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_frexp | math.h:frexp | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| fmod | function | C89 | `PLATFORM_BACKED` | libc/src/math/math.c | cplus_libc.h:cplus_fmod | math.h:fmod | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| remainder | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_remainder | math.h:remainder | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| remquo | function | C99 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_remquo | math.h:remquo | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| fdim | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_fdim | math.h:fdim | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| fmax | function | C99 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_fmax | math.h:fmax | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| fmin | function | C99 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_fmin | math.h:fmin | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| fma | function | C99 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_fma | math.h:fma | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| copysign | function | C99 | `PLATFORM_BACKED` | libc/src/math/math2.c | cplus_libc.h:cplus_copysign | math.h:copysign | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| nan | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_nan | math.h:nan | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| nextafter | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_nextafter | math.h:nextafter | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| nexttoward | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_gaps.c | cplus_libc.h:cplus_nexttoward | math.h:nexttoward | tests/libc/test_math_gaps.c | Host-delegated; host controls errno/FP semantics. |
| erf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_erf | math.h:erf | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| erfc | function | C89 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_erfc | math.h:erfc | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| tgamma | function | C99 | `PLATFORM_BACKED` | libc/src/math/math3.c | cplus_libc.h:cplus_tgamma | math.h:tgamma | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| ilogb | function | C99 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_ilogb | math.h:ilogb | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| lrint | function | C99 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_lrint | math.h:lrint | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| llrint | function | C99 | `PLATFORM_BACKED` | libc/src/math/math4.c | cplus_libc.h:cplus_llrint | math.h:llrint | tests/libc/test_math_time_locale.c | Delegates to host libm; domain/range/errno semantics are host-defined, not reimplemented. |
| lround | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_gaps.c | cplus_libc.h:cplus_lround | math.h:lround | tests/libc/test_math_gaps.c | Host-delegated; host controls errno/FP semantics. |
| llround | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_gaps.c | cplus_libc.h:cplus_llround | math.h:llround | tests/libc/test_math_gaps.c | Host-delegated; host controls errno/FP semantics. |
| scalbln | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_gaps.c | cplus_libc.h:cplus_scalbln | math.h:scalbln | tests/libc/test_math_gaps.c | Host-delegated; host controls errno/FP semantics. |
| acosf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_acosf | math.h:acosf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| asinf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_asinf | math.h:asinf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| atanf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_atanf | math.h:atanf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| atan2f | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_atan2f | math.h:atan2f | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| cosf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_cosf | math.h:cosf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| sinf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_sinf | math.h:sinf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| tanf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_tanf | math.h:tanf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| acoshf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_acoshf | math.h:acoshf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| asinhf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_asinhf | math.h:asinhf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| atanhf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_atanhf | math.h:atanhf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| coshf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_coshf | math.h:coshf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| sinhf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_sinhf | math.h:sinhf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| tanhf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_tanhf | math.h:tanhf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| expf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_expf | math.h:expf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| exp2f | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_exp2f | math.h:exp2f | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| expm1f | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_expm1f | math.h:expm1f | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| logf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_logf | math.h:logf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| log10f | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_log10f | math.h:log10f | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| log1pf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_log1pf | math.h:log1pf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| log2f | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_log2f | math.h:log2f | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| powf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_powf | math.h:powf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| sqrtf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_sqrtf | math.h:sqrtf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| cbrtf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_cbrtf | math.h:cbrtf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| hypotf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_hypotf | math.h:hypotf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| ceilf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_ceilf | math.h:ceilf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| floorf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_floorf | math.h:floorf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| roundf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_roundf | math.h:roundf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| truncf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_truncf | math.h:truncf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| nearbyintf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_nearbyintf | math.h:nearbyintf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| rintf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_rintf | math.h:rintf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| fabsf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_fabsf | math.h:fabsf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| ldexpf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_ldexpf | math.h:ldexpf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| lgammaf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_lgammaf | math.h:lgammaf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| logbf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_logbf | math.h:logbf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| modff | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_modff | math.h:modff | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| scalbnf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_scalbnf | math.h:scalbnf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| frexpf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_frexpf | math.h:frexpf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| fmodf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_fmodf | math.h:fmodf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| remainderf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_remainderf | math.h:remainderf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| remquof | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_remquof | math.h:remquof | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| fdimf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_fdimf | math.h:fdimf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| fmaxf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_fmaxf | math.h:fmaxf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| fminf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_fminf | math.h:fminf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| fmaf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_fmaf | math.h:fmaf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| copysignf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_copysignf | math.h:copysignf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| nanf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_nanf | math.h:nanf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| nextafterf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_nextafterf | math.h:nextafterf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| nexttowardf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_nexttowardf | math.h:nexttowardf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| erff | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_erff | math.h:erff | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| erfcf | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_erfcf | math.h:erfcf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| tgammaf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_tgammaf | math.h:tgammaf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| ilogbf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_ilogbf | math.h:ilogbf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| lrintf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_lrintf | math.h:lrintf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| llrintf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_llrintf | math.h:llrintf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| lroundf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_lroundf | math.h:lroundf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| llroundf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_llroundf | math.h:llroundf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| scalblnf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_float.c | cplus_libc.h:cplus_scalblnf | math.h:scalblnf | tests/libc/test_math_float.c | PLATFORM_BACKED float wrapper; delegates to host libm. On Apple Clang/arm64 long double is binary64, so *l offers no extra precision. |
| acosl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_acosl | math.h:acosl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| asinl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_asinl | math.h:asinl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| atanl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_atanl | math.h:atanl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| atan2l | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_atan2l | math.h:atan2l | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| cosl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_cosl | math.h:cosl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| sinl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_sinl | math.h:sinl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| tanl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_tanl | math.h:tanl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| acoshl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_acoshl | math.h:acoshl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| asinhl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_asinhl | math.h:asinhl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| atanhl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_atanhl | math.h:atanhl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| coshl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_coshl | math.h:coshl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| sinhl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_sinhl | math.h:sinhl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| tanhl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_tanhl | math.h:tanhl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| expl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_expl | math.h:expl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| exp2l | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_exp2l | math.h:exp2l | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| expm1l | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_expm1l | math.h:expm1l | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| logl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_logl | math.h:logl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| log10l | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_log10l | math.h:log10l | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| log1pl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_log1pl | math.h:log1pl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| log2l | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_log2l | math.h:log2l | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| powl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_powl | math.h:powl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| sqrtl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_sqrtl | math.h:sqrtl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| cbrtl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_cbrtl | math.h:cbrtl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| hypotl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_hypotl | math.h:hypotl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| ceill | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_ceill | math.h:ceill | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| floorl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_floorl | math.h:floorl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| roundl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_roundl | math.h:roundl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| truncl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_truncl | math.h:truncl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| nearbyintl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_nearbyintl | math.h:nearbyintl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| rintl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_rintl | math.h:rintl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| fabsl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_fabsl | math.h:fabsl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| ldexpl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_ldexpl | math.h:ldexpl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| lgammal | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_lgammal | math.h:lgammal | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| logbl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_logbl | math.h:logbl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| modfl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_modfl | math.h:modfl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| scalbnl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_scalbnl | math.h:scalbnl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| frexpl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_frexpl | math.h:frexpl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| fmodl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_fmodl | math.h:fmodl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| remainderl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_remainderl | math.h:remainderl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| remquol | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_remquol | math.h:remquol | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| fdiml | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_fdiml | math.h:fdiml | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| fmaxl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_fmaxl | math.h:fmaxl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| fminl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_fminl | math.h:fminl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| fmal | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_fmal | math.h:fmal | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| copysignl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_copysignl | math.h:copysignl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| nanl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_nanl | math.h:nanl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| nextafterl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_nextafterl | math.h:nextafterl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| nexttowardl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_nexttowardl | math.h:nexttowardl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| erfl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_erfl | math.h:erfl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| erfcl | function | C89 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_erfcl | math.h:erfcl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| tgammal | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_tgammal | math.h:tgammal | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| ilogbl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_ilogbl | math.h:ilogbl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| lrintl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_lrintl | math.h:lrintl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| llrintl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_llrintl | math.h:llrintl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| lroundl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_lroundl | math.h:lroundl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| llroundl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_llroundl | math.h:llroundl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| scalblnl | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_ldouble.c | cplus_libc.h:cplus_scalblnl | math.h:scalblnl | tests/libc/test_math_ldouble.c | PLATFORM_BACKED long-double wrapper; delegates to host libm. errno/domain semantics are host-defined. |
| HUGE_VAL | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| HUGE_VALF | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| HUGE_VALL | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| INFINITY | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| NAN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| MATH_ERRNO | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| MATH_ERREXCEPT | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| math_errhandling | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| FP_FAST_FMA | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| FP_FAST_FMAF | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| FP_FAST_FMAL | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/constant from host <math.h>; no runtime symbol. |
| fpclassify | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| isfinite | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| isinf | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| isnan | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| isnormal | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| signbit | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| isgreater | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| isgreaterequal | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| isless | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| islessequal | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| islessgreater | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| isunordered | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <math.h>; expands via compiler builtins, no single C linker symbol. |
| exp10 | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| exp10f | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| exp10l | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| roundeven | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| roundevenf | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| roundevenl | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| nextup | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| nextupf | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| nextupl | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| nextdown | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| nextdownf | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| nextdownl | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fadd | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| faddf | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| faddl | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fsub | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fsubf | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fsubl | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fmul | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fmulf | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fmull | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fdiv | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fdivf | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fdivl | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| hypot (3-argument C23 overload) | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| frexp16 | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| fma16 | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| logb16 | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |
| exp16 | function | C23 | `MISSING` | — | — | — | — | C23 addition. No cplus_ wrapper and no host declaration gated for C17 baseline; not shipped on this branch. |

### `setjmp.h` — 3 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| jmp_buf | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined array type; no C+ wrapper (wrapping setjmp in another frame is undefined behavior). |
| setjmp | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host compiler macro; standalone C+ wrapper is deliberately omitted (would change the stack frame and break longjmp). |
| longjmp | function | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Provided by host <setjmp.h> and used directly by stdlib/safe/src/safe_runtime.c and vendored exceptions4c; no standalone cplus_longjmp because it must be paired with host setjmp. |

### `signal.h` — 12 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| SIG_ABRT | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| SIGFPE | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| SIGILL | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| SIGINT | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| SIGSEGV | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| SIGTERM | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| SIG_DFL | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| SIG_IGN | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| SIG_ERR | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <signal.h>. |
| sig_atomic_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined integer type; no wrapper. |
| signal | function | C89 | `PLATFORM_BACKED` | libc/src/signal/signal.c | cplus_libc.h:cplus_signal | signal.h:signal | tests/libc/test_signal.c | Delegates to host signal; handler must have C linkage. Signal-safety of logging inside the wrapper is not guaranteed. |
| raise | function | C89 | `PLATFORM_BACKED` | libc/src/signal/signal.c | cplus_libc.h:cplus_raise | signal.h:raise | tests/libc/test_signal.c | Delegates to host raise. |

### `stdalign.h` — 4 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| alignas | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure compiler keyword macro header; C23 makes alignas/alignof keywords. No runtime symbol. |
| alignof | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure compiler keyword macro header; C23 makes alignas/alignof keywords. No runtime symbol. |
| __alignas_is_defined | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure compiler keyword macro header; C23 makes alignas/alignof keywords. No runtime symbol. |
| __alignof_is_defined | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure compiler keyword macro header; C23 makes alignas/alignof keywords. No runtime symbol. |

### `stdarg.h` — 5 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| va_list | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler ABI intrinsic from host <stdarg.h>; cannot be represented as a C linker symbol. |
| va_start | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler ABI intrinsic from host <stdarg.h>; cannot be represented as a C linker symbol. |
| va_arg | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler ABI intrinsic from host <stdarg.h>; cannot be represented as a C linker symbol. |
| va_copy | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler ABI intrinsic from host <stdarg.h>; cannot be represented as a C linker symbol. |
| va_end | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler ABI intrinsic from host <stdarg.h>; cannot be represented as a C linker symbol. |

### `stdatomic.h` — 49 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| atomic_init | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_is_lock_free | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_store | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_store_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_load | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_load_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_exchange | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_exchange_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_compare_exchange_strong | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_compare_exchange_strong_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_compare_exchange_weak | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_compare_exchange_weak_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_add | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_add_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_sub | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_sub_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_or | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_or_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_xor | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_xor_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_flag_clear | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_flag_clear_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_flag_test_and_set | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_flag_test_and_set_explicit | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_signal_fence | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_thread_fence | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| kill_dependency | function | C11 | `UNSUPPORTED` | — | — | — | — | No standalone cplus_atomic_* C ABI symbol is shipped: C11 _Generic macros expand to compiler type-generic builtins and cannot be represented as a single C99 function. Host <stdatomic.h>/__atomic_* builtins are used internally only. |
| atomic_fetch_and | function | C23 | `UNSUPPORTED` | — | — | — | — | C23 addition; not shipped for the same type-generic reason as the C11 atomics. |
| atomic_fetch_and_explicit | function | C23 | `UNSUPPORTED` | — | — | — | — | C23 addition; not shipped for the same type-generic reason as the C11 atomics. |
| atomic_fetch_nand | function | C23 | `UNSUPPORTED` | — | — | — | — | C23 addition; not shipped for the same type-generic reason as the C11 atomics. |
| atomic_fetch_nand_explicit | function | C23 | `UNSUPPORTED` | — | — | — | — | C23 addition; not shipped for the same type-generic reason as the C11 atomics. |
| atomic_wait | function | C23 | `UNSUPPORTED` | — | — | — | — | C23 addition; not shipped for the same type-generic reason as the C11 atomics. |
| atomic_notify_one | function | C23 | `UNSUPPORTED` | — | — | — | — | C23 addition; not shipped for the same type-generic reason as the C11 atomics. |
| atomic_notify_all | function | C23 | `UNSUPPORTED` | — | — | — | — | C23 addition; not shipped for the same type-generic reason as the C11 atomics. |
| ATOMIC_VAR_INIT | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host compiler macro; no runtime symbol. |
| ATOMIC_FLAG_INIT | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host compiler macro; no runtime symbol. |
| atomic_flag | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_bool | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_char | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_int | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_uint | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_long | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_ullong | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| memory_order | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_intptr_t | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_uintptr_t | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_size_t | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_intmax_t | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |
| atomic_uintmax_t | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler-provided C11 type/enum from host <stdatomic.h>; no wrapper type shipped. |

### `stdbool.h` — 4 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| bool | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure macro header; bool/true/false become keywords in C23. No runtime symbol. |
| true | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure macro header; bool/true/false become keywords in C23. No runtime symbol. |
| false | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure macro header; bool/true/false become keywords in C23. No runtime symbol. |
| __bool_true_false_are_defined | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure macro header; bool/true/false become keywords in C23. No runtime symbol. |

### `stddef.h` — 6 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| ptrdiff_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Freestanding type/macro from toolchain <stddef.h>; no runtime symbol. |
| size_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Freestanding type/macro from toolchain <stddef.h>; no runtime symbol. |
| wchar_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Freestanding type/macro from toolchain <stddef.h>; no runtime symbol. |
| max_align_t | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Freestanding type/macro from toolchain <stddef.h>; no runtime symbol. |
| NULL | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Freestanding type/macro from toolchain <stddef.h>; no runtime symbol. |
| offsetof | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Freestanding type/macro from toolchain <stddef.h>; no runtime symbol. |

### `stdint.h` — 89 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| int8_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int16_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int32_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int64_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint8_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint16_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint32_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint64_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int_least8_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int_least16_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int_least32_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int_least64_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint_least8_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint_least16_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint_least32_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint_least64_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int_fast8_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int_fast16_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int_fast32_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| int_fast64_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint_fast8_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint_fast16_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint_fast32_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uint_fast64_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| intptr_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uintptr_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| intmax_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| uintmax_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Exact-width / least / fast / max width integer type from toolchain <stdint.h>; no runtime symbol. |
| INT8_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT8_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT8_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT16_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT16_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT16_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT32_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT32_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT32_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT64_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT64_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT64_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_LEAST8_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_LEAST8_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT_LEAST8_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_LEAST16_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_LEAST16_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT_LEAST16_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_LEAST32_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_LEAST32_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT_LEAST32_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_LEAST64_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_LEAST64_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT_LEAST64_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_FAST8_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_FAST8_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT_FAST8_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_FAST16_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_FAST16_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT_FAST16_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_FAST32_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_FAST32_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT_FAST32_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_FAST64_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT_FAST64_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT_FAST64_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INTPTR_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INTPTR_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINTPTR_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INTMAX_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INTMAX_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINTMAX_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| SIZE_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| PTRDIFF_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| PTRDIFF_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| SIG_ATOMIC_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| SIG_ATOMIC_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| WCHAR_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| WCHAR_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| WINT_MIN | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| WINT_MAX | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT8_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT8_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT16_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT16_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT32_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT32_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INT64_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINT64_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| INTMAX_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |
| UINTMAX_C | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time LIMIT / literal-suffix macro from toolchain <stdint.h>; no runtime symbol. |

### `stdio.h` — 75 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| fopen | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_fopen | stdio.h:fopen | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fclose | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_fclose | stdio.h:fclose | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fread | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_fread | stdio.h:fread | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fwrite | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_fwrite | stdio.h:fwrite | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fseek | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_fseek | stdio.h:fseek | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| ftell | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_ftell | stdio.h:ftell | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fflush | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_fflush | stdio.h:fflush | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fgetpos | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_fgetpos | stdio.h:fgetpos | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fsetpos | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_fsetpos | stdio.h:fsetpos | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| setbuf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_setbuf | stdio.h:setbuf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| setvbuf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_setvbuf | stdio.h:setvbuf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fgets | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_fgets | stdio.h:fgets | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fputs | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_fputs | stdio.h:fputs | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fgetc | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_fgetc | stdio.h:fgetc | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fputc | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_fputc | stdio.h:fputc | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| getc | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_getc | stdio.h:getc | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| putc | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_putc | stdio.h:putc | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| getchar | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_getchar | stdio.h:getchar | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| putchar | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_putchar | stdio.h:putchar | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| puts | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_puts | stdio.h:puts | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| ungetc | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_ungetc | stdio.h:ungetc | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| feof | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_feof | stdio.h:feof | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| ferror | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_ferror | stdio.h:ferror | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| clearerr | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_clearerr | stdio.h:clearerr | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| remove | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_remove | stdio.h:remove | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| rename | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_rename | stdio.h:rename | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| rewind | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_rewind | stdio.h:rewind | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| perror | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio2.c | cplus_libc.h:cplus_perror | stdio.h:perror | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| freopen | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_freopen | stdio.h:freopen | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| tmpfile | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio3.c | cplus_libc.h:cplus_tmpfile | stdio.h:tmpfile | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| printf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_printf | stdio.h:printf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fprintf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_fprintf | stdio.h:fprintf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| sprintf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_sprintf | stdio.h:sprintf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| snprintf | function | C99 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_snprintf | stdio.h:snprintf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| vprintf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_vprintf | stdio.h:vprintf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| vfprintf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_vfprintf | stdio.h:vfprintf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| vsprintf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_vsprintf | stdio.h:vsprintf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| vsnprintf | function | C99 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_vsnprintf | stdio.h:vsnprintf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| scanf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_scanf | stdio.h:scanf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| fscanf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_fscanf | stdio.h:fscanf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| sscanf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_sscanf | stdio.h:sscanf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| vfscanf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_vfscanf | stdio.h:vfscanf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| vsscanf | function | C89 | `PLATFORM_BACKED` | libc/src/stdio/stdio.c | cplus_libc.h:cplus_vsscanf | stdio.h:vsscanf | tests/libc/test_stdio_full.c | Delegates to host <stdio.h> on the host FILE*; the FILE buffering engine is intentionally not reimplemented. cplus_fclose is double-close guarded; cplus_fread/cplus_fwrite are overflow/NULL guarded. Format-string correctness and pointer validity remain caller responsibilities (no static wide-format checking on Apple Clang). |
| vscanf | function | C99 | `PLATFORM_BACKED` | libc/src/math/math_gaps.c | cplus_libc.h:cplus_vscanf | stdio.h:vscanf | tests/libc/test_math_gaps.c | Host-delegated; host controls errno/FP semantics. |
| gets | function | C11 | `UNSUPPORTED` | — | — | — | — | Removed from ISO C in C11 (unsafe, unbounded). Deliberately not shipped; use bounded fgets / cplus_fgets instead. |
| tmpnam | function | C89 | `UNSUPPORTED` | — | — | — | — | Insecure TOCTOU race; deliberately not shipped. Use cplus_tmpfile or mkstemp (cplus_mkstemp) instead. |
| FILE | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; cplus_* wrappers take host FILE*/fpos_t* directly. |
| fpos_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; cplus_* wrappers take host FILE*/fpos_t* directly. |
| size_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; cplus_* wrappers take host FILE*/fpos_t* directly. |
| EOF | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| BUFSIZ | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| _IOFBF | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| _IOLBF | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| _IONBF | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| SEEK_SET | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| SEEK_CUR | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| SEEK_END | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| FOPEN_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| FILENAME_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| L_tmpnam | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| TMP_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| stdin | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| stdout | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| stderr | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro/stream object from host <stdio.h>; no C+ wrapper symbol. |
| fopen_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| fclose_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| fread_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| fprintf_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| printf_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| sprintf_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| scanf_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| fscanf_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| sscanf_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| gets_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |
| tmpnam_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K (optional) interface. Not shipped as a portable ISO API: the host macOS CRT lacks Annex K semantics; a separate SafeCRT _s family exists in stdlib/safe but is not the ISO Annex K contract. |

### `stdlib.h` — 49 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| malloc | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/stdlib/alloc.c,libc/src/string/extra2.c,libc/src/string/strndup.c | cplus_libc.h:cplus_malloc | — | tests/libc/test_all_public_10_scenarios.c | Bundled hardened allocator in libc/src/stdlib/alloc.c with live metadata, overflow checking, ownership tracking and fault injection; NOT a thin host delegation. |
| calloc | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/stdlib/alloc.c | cplus_libc.h:cplus_calloc | — | tests/libc/test_all_public_10_scenarios.c | Bundled hardened allocator in libc/src/stdlib/alloc.c with live metadata, overflow checking, ownership tracking and fault injection; NOT a thin host delegation. |
| realloc | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/stdlib/alloc.c | cplus_libc.h:cplus_realloc | — | tests/libc/test_all_public_10_scenarios.c | Bundled hardened allocator in libc/src/stdlib/alloc.c with live metadata, overflow checking, ownership tracking and fault injection; NOT a thin host delegation. |
| free | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/stdlib/alloc.c | cplus_libc.h:cplus_free | — | tests/libc/test_all_public_10_scenarios.c | Bundled hardened allocator in libc/src/stdlib/alloc.c with live metadata, overflow checking, ownership tracking and fault injection; NOT a thin host delegation. |
| abort | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra2.c | cplus_libc.h:cplus_abort | stdlib.h:abort | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| atexit | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra2.c | cplus_libc.h:cplus_atexit | stdlib.h:atexit | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| exit | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra2.c | cplus_libc.h:cplus_exit | stdlib.h:exit | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| _Exit | function | C99 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_c17.c | cplus_libc.h:cplus__Exit | stdlib.h:_Exit | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| quick_exit | function | C11 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_c17.c | cplus_libc.h:cplus_quick_exit | stdlib.h:quick_exit | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| at_quick_exit | function | C11 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_c17.c | cplus_libc.h:cplus_at_quick_exit | stdlib.h:at_quick_exit | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| getenv | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra2.c | cplus_libc.h:cplus_getenv | stdlib.h:getenv | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| system | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra2.c | cplus_libc.h:cplus_system | stdlib.h:system | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| strtol | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra.c | cplus_libc.h:cplus_strtol | stdlib.h:strtol | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| strtoul | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra.c | cplus_libc.h:cplus_strtoul | stdlib.h:strtoul | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| strtoll | function | C99 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra3.c | cplus_libc.h:cplus_strtoll | stdlib.h:strtoll | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| strtoull | function | C99 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra3.c | cplus_libc.h:cplus_strtoull | stdlib.h:strtoull | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| strtod | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra3.c | cplus_libc.h:cplus_strtod | stdlib.h:strtod | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| strtof | function | C99 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra4.c | cplus_libc.h:cplus_strtof | stdlib.h:strtof | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| strtold | function | C99 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra4.c | cplus_libc.h:cplus_strtold | stdlib.h:strtold | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| atoi | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra.c | cplus_libc.h:cplus_atoi | stdlib.h:atoi | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| atol | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra.c | cplus_libc.h:cplus_atol | stdlib.h:atol | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| atoll | function | C99 | `PLATFORM_BACKED` | libc/src/stdlib/musl_stdlib_extra.c | cplus_libc.h:cplus_atoll | stdlib.h:atoll | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| abs | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra.c | cplus_libc.h:cplus_abs | stdlib.h:abs | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| labs | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra.c | cplus_libc.h:cplus_labs | stdlib.h:labs | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| llabs | function | C99 | `PLATFORM_BACKED` | libc/src/stdlib/musl_stdlib_extra.c | cplus_libc.h:cplus_llabs | stdlib.h:llabs | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| div | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/musl_stdlib_extra.c | cplus_libc.h:cplus_div | stdlib.h:div | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| ldiv | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/musl_stdlib_extra.c | cplus_libc.h:cplus_ldiv | stdlib.h:ldiv | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| lldiv | function | C99 | `PLATFORM_BACKED` | libc/src/stdlib/musl_stdlib_extra.c | cplus_libc.h:cplus_lldiv | stdlib.h:lldiv | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| rand | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra2.c | cplus_libc.h:cplus_rand | stdlib.h:rand | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| srand | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra2.c | cplus_libc.h:cplus_srand | stdlib.h:srand | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| qsort | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra.c | cplus_libc.h:cplus_qsort | stdlib.h:qsort | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| bsearch | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra.c | cplus_libc.h:cplus_bsearch | stdlib.h:bsearch | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| mblen | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra4.c | cplus_libc.h:cplus_mblen | stdlib.h:mblen | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| mbtowc | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra4.c | cplus_libc.h:cplus_mbtowc | stdlib.h:mbtowc | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| wctomb | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra4.c | cplus_libc.h:cplus_wctomb | stdlib.h:wctomb | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| mbstowcs | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra4.c | cplus_libc.h:cplus_mbstowcs | stdlib.h:mbstowcs | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| wcstombs | function | C89 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_extra4.c | cplus_libc.h:cplus_wcstombs | stdlib.h:wcstombs | tests/libc/test_stdlib_full.c | Namespaced cplus_* wrapper delegating to the host CRT. Allocator provenance/constraint semantics are host-defined; random and locale-dependent conversions inherit host behavior. |
| aligned_alloc | function | C11 | `PLATFORM_BACKED` | libc/src/stdlib/stdlib_c17.c | cplus_libc.h:cplus_aligned_alloc | stdlib.h:aligned_alloc | tests/libc/test_stdlib_c17.c | Delegates to the host allocator directly to preserve allocator provenance (free via cplus_free/host free). |
| EXIT_FAILURE | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <stdlib.h>; no wrapper symbol. |
| EXIT_SUCCESS | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <stdlib.h>; no wrapper symbol. |
| RAND_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <stdlib.h>; no wrapper symbol. |
| MB_CUR_MAX | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <stdlib.h>; no wrapper symbol. |
| div_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrapper returns the host type. |
| ldiv_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrapper returns the host type. |
| lldiv_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrapper returns the host type. |
| size_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrapper returns the host type. |
| wchar_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrapper returns the host type. |
| bsearch_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K sort/search with context argument; macOS host CRT lacks Annex K hooks, so not shipped as a portable ISO API. |
| qsort_s | function | C11 | `UNSUPPORTED` | — | — | — | — | ISO C11 Annex K sort/search with context argument; macOS host CRT lacks Annex K hooks, so not shipped as a portable ISO API. |

### `stdnoreturn.h` — 1 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| noreturn | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Pure macro header; noreturn becomes a keyword in C23. No runtime symbol. |

### `string.h` — 28 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| memcpy | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/bounded.c,libc/src/string/memory.c | cplus_libc.h:cplus_memcpy | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| memmove | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/bounded.c,libc/src/string/memory.c | cplus_libc.h:cplus_memmove | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| memset | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/bounded.c,libc/src/string/memory.c | cplus_libc.h:cplus_memset | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| memcmp | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/bounded.c,libc/src/string/memory.c | cplus_libc.h:cplus_memcmp | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| memchr | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra.c,libc/src/string/musl_extra.c | cplus_libc.h:cplus_memchr | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strcpy | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/bounded.c,libc/src/string/string.c | cplus_libc.h:cplus_strcpy | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strncpy | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/bounded.c,libc/src/string/string.c | cplus_libc.h:cplus_strncpy | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strcat | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/bounded.c,libc/src/string/string.c | cplus_libc.h:cplus_strcat | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strncat | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/bounded.c,libc/src/string/string.c | cplus_libc.h:cplus_strncat | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strcmp | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/string.c | cplus_libc.h:cplus_strcmp | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strncmp | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/string.c | cplus_libc.h:cplus_strncmp | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strcoll | function | C89 | `PLATFORM_BACKED` | libc/src/string/string_extra4.c | cplus_libc.h:cplus_strcoll | string.h:strcoll | — | Namespaced wrapper delegating to the host string.h implementation (locale-dependent collation / host error-message table). Host semantics preserved; buffer validity is caller responsibility. |
| strxfrm | function | C89 | `PLATFORM_BACKED` | libc/src/string/string_extra4.c | cplus_libc.h:cplus_strxfrm | string.h:strxfrm | — | Namespaced wrapper delegating to the host string.h implementation (locale-dependent collation / host error-message table). Host semantics preserved; buffer validity is caller responsibility. |
| strchr | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra.c | cplus_libc.h:cplus_strchr | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strrchr | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra.c | cplus_libc.h:cplus_strrchr | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strspn | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra3.c,libc/src/string/musl_extra.c | cplus_libc.h:cplus_strspn | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strcspn | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra3.c,libc/src/string/musl_extra.c | cplus_libc.h:cplus_strcspn | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strpbrk | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra3.c | cplus_libc.h:cplus_strpbrk | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strstr | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra.c | cplus_libc.h:cplus_strstr | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strtok | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/strtok.c | cplus_libc.h:cplus_strtok | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strlen | function | C89 | `IMPLEMENTED_BUNDLED` | libc/src/string/string.c | cplus_libc.h:cplus_strlen | — | tests/libc/test_libc_full.c | Compiled in this repo from bundled musl/upstream source or direct libc/src implementation (classification VENDORED/IMPLEMENTED in libc/contracts/symbol_map.json). SafeCRT _s variants live in stdlib/safe. |
| strerror | function | C89 | `PLATFORM_BACKED` | libc/src/string/strerror.c | cplus_libc.h:cplus_strerror | string.h:strerror | — | Namespaced wrapper delegating to the host string.h implementation (locale-dependent collation / host error-message table). Host semantics preserved; buffer validity is caller responsibility. |
| strdup | function | C23 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra2.c | cplus_libc.h:cplus_strdup | — | — | C23 addition (historically POSIX). Compiled in this repo (musl-derived / libc/src); no ISO C17 baseline symbol. |
| strndup | function | C23 | `IMPLEMENTED_BUNDLED` | libc/src/string/strndup.c | cplus_libc.h:cplus_strndup | — | — | C23 addition (historically POSIX). Compiled in this repo (musl-derived / libc/src); no ISO C17 baseline symbol. |
| strnlen | function | C23 | `IMPLEMENTED_BUNDLED` | libc/src/string/extra.c | cplus_libc.h:cplus_strnlen | — | — | C23 addition (historically POSIX). Compiled in this repo (musl-derived / libc/src); no ISO C17 baseline symbol. |
| strlcpy | function | C23 | `PLATFORM_BACKED` | libc/src/string/string_extra6.c | cplus_libc.h:cplus_strlcpy | string.h:strlcpy | tests/libc/test_string_extra5.c | C23 addition. Namespaced wrapper delegates to the host BSD strlcpy (present on macOS); host semantics preserved. |
| strlcat | function | C23 | `PLATFORM_BACKED` | libc/src/string/string_extra6.c | cplus_libc.h:cplus_strlcat | string.h:strlcat | tests/libc/test_string_extra5.c | C23 addition. Namespaced wrapper delegates to the host BSD strlcat (present on macOS). |
| memset_explicit | function | C23 | `MISSING` | — | — | — | — | C23 addition: constant-time, non-optimizable memset. No cplus_ wrapper and no shipped implementation; use cplus_memset only if compiler barrier is acceptable. |

### `tgmath.h` — 71 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| acos | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_acos | tgmath.h:acos | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| asin | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_asin | tgmath.h:asin | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| atan | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_atan | tgmath.h:atan | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| atan2 | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_atan2 | tgmath.h:atan2 | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| cos | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_cos | tgmath.h:cos | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| sin | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_sin | tgmath.h:sin | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| tan | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_tan | tgmath.h:tan | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| acosh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_acosh | tgmath.h:acosh | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| asinh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_asinh | tgmath.h:asinh | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| atanh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_atanh | tgmath.h:atanh | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| cosh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_cosh | tgmath.h:cosh | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| sinh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_sinh | tgmath.h:sinh | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| tanh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_tanh | tgmath.h:tanh | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| exp | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_exp | tgmath.h:exp | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| exp2 | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math4.c | cplus_libc.h:cplus_exp2 | tgmath.h:exp2 | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| expm1 | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_expm1 | tgmath.h:expm1 | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| log | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_log | tgmath.h:log | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| log10 | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_log10 | tgmath.h:log10 | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| log1p | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_log1p | tgmath.h:log1p | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| log2 | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_log2 | tgmath.h:log2 | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| pow | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_pow | tgmath.h:pow | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| sqrt | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_sqrt | tgmath.h:sqrt | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| cbrt | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_cbrt | tgmath.h:cbrt | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| hypot | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_hypot | tgmath.h:hypot | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| ceil | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_ceil | tgmath.h:ceil | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| floor | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_floor | tgmath.h:floor | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| round | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_round | tgmath.h:round | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| trunc | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_trunc | tgmath.h:trunc | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| nearbyint | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math4.c | cplus_libc.h:cplus_nearbyint | tgmath.h:nearbyint | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| rint | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| fabs | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_fabs | tgmath.h:fabs | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| ldexp | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_ldexp | tgmath.h:ldexp | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| lgamma | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_lgamma | tgmath.h:lgamma | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| logb | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math4.c | cplus_libc.h:cplus_logb | tgmath.h:logb | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| modf | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_modf | tgmath.h:modf | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| scalbn | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math4.c | cplus_libc.h:cplus_scalbn | tgmath.h:scalbn | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| frexp | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_frexp | tgmath.h:frexp | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| fmod | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math.c | cplus_libc.h:cplus_fmod | tgmath.h:fmod | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| remainder | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_remainder | tgmath.h:remainder | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| fdim | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_fdim | tgmath.h:fdim | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| fmax | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_fmax | tgmath.h:fmax | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| fmin | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_fmin | tgmath.h:fmin | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| fma | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math4.c | cplus_libc.h:cplus_fma | tgmath.h:fma | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| copysign | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math2.c | cplus_libc.h:cplus_copysign | tgmath.h:copysign | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| nan | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_nan | tgmath.h:nan | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| nextafter | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_nextafter | tgmath.h:nextafter | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| erf | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_erf | tgmath.h:erf | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| erfc | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_erfc | tgmath.h:erfc | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| tgamma | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math3.c | cplus_libc.h:cplus_tgamma | tgmath.h:tgamma | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| ilogb | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math4.c | cplus_libc.h:cplus_ilogb | tgmath.h:ilogb | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| lrint | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math4.c | cplus_libc.h:cplus_lrint | tgmath.h:lrint | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| llrint | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/math/math4.c | cplus_libc.h:cplus_llrint | tgmath.h:llrint | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| lround | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| llround | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| scalbln | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| creal | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex.c | cplus_libc.h:cplus_creal | tgmath.h:creal | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| cimag | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex.c | cplus_libc.h:cplus_cimag | tgmath.h:cimag | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| conj | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex.c | cplus_libc.h:cplus_conj | tgmath.h:conj | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| cabs | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex.c | cplus_libc.h:cplus_cabs | tgmath.h:cabs | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| carg | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex.c | cplus_libc.h:cplus_carg | tgmath.h:carg | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| cexp | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex.c | cplus_libc.h:cplus_cexp | tgmath.h:cexp | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| clog | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_clog | complex.h:clog | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| cproj | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cproj | complex.h:cproj | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| csqrt | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex.c | cplus_libc.h:cplus_csqrt | tgmath.h:csqrt | — | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| cpow | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cpow | complex.h:cpow | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| csin | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csin | complex.h:csin | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| ccos | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ccos | complex.h:ccos | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| ctan | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ctan | complex.h:ctan | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| csinh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csinh | complex.h:csinh | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| ccosh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ccosh | complex.h:ccosh | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |
| ctanh | macro | C99 | `HOST/COMPILER-PROVIDED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ctanh | complex.h:ctanh | tests/libc/test_complex_extra.c | Type-generic macro from host <tgmath.h> implemented with _Generic/compiler builtins over <math.h>/<complex.h>; no single C linker symbol can represent it. |

### `threads.h` — 41 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| thrd_create | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| thrd_equal | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| thrd_current | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| thrd_sleep | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| thrd_yield | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| thrd_exit | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| thrd_join | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| thrd_detach | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| mtx_init | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| mtx_lock | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| mtx_timedlock | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| mtx_trylock | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| mtx_unlock | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| mtx_destroy | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| cnd_init | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| cnd_signal | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| cnd_broadcast | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| cnd_wait | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| cnd_timedwait | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| cnd_destroy | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| call_once | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| tss_create | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| tss_get | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| tss_set | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| tss_delete | function | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include threads.h FAILS ("threads.h file not found") on Apple Clang 16 / macOS arm64. No cplus_thrd_*/mtx_*/cnd_*/tss_* symbols are shipped. pthread-backed cplus_pthread_* wrappers in libc/src/thread/thread.c cover create/join/mutex/cond but are NOT the ISO C11 threads.h API. |
| thrd_success | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| thrd_busy | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| thrd_error | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| thrd_nomem | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| thrd_timedout | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| mtx_plain | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| mtx_recursive | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| mtx_timed | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| ONCE_FLAG_INIT | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| TSS_DTOR_ITERATIONS | macro | C11 | `MISSING` | — | — | — | — | Macro from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| thrd_t | type | C11 | `MISSING` | — | — | — | — | Type from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| mtx_t | type | C11 | `MISSING` | — | — | — | — | Type from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| cnd_t | type | C11 | `MISSING` | — | — | — | — | Type from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| tss_t | type | C11 | `MISSING` | — | — | — | — | Type from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| thrd_start_t | type | C11 | `MISSING` | — | — | — | — | Type from C11 <threads.h>; host header absent (compile probe failed), not shipped. |
| once_flag | type | C11 | `MISSING` | — | — | — | — | Type from C11 <threads.h>; host header absent (compile probe failed), not shipped. |

### `time.h` — 17 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| clock | function | C89 | `PLATFORM_BACKED` | libc/src/time/time.c | cplus_libc.h:cplus_clock | time.h:clock | tests/libc/test_math_time_locale.c | Delegates to host time functions; struct tm/clock_t are host-defined. |
| difftime | function | C89 | `PLATFORM_BACKED` | libc/src/time/time.c | cplus_libc.h:cplus_difftime | time.h:difftime | tests/libc/test_math_time_locale.c | Delegates to host time functions; struct tm/clock_t are host-defined. |
| mktime | function | C89 | `PLATFORM_BACKED` | libc/src/time/time.c | cplus_libc.h:cplus_mktime | time.h:mktime | tests/libc/test_math_time_locale.c | Delegates to host time functions; struct tm/clock_t are host-defined. |
| gmtime | function | C89 | `PLATFORM_BACKED` | libc/src/time/time.c | cplus_libc.h:cplus_gmtime | time.h:gmtime | tests/libc/test_math_time_locale.c | Delegates to host time functions; struct tm/clock_t are host-defined. |
| localtime | function | C89 | `PLATFORM_BACKED` | libc/src/time/time.c | cplus_libc.h:cplus_localtime | time.h:localtime | tests/libc/test_math_time_locale.c | Delegates to host time functions; struct tm/clock_t are host-defined. |
| asctime | function | C89 | `PLATFORM_BACKED` | libc/src/time/time_ascii.c | cplus_libc.h:cplus_asctime | time.h:asctime | tests/libc/test_time_ascii.c | asctime/ctime are documented NON-REENTRANT per ISO C; prefer cplus_asctime_r/cplus_ctime_r. Delegates to host time functions; struct tm/clock_t are host-defined. |
| ctime | function | C89 | `PLATFORM_BACKED` | libc/src/time/time_ascii.c | cplus_libc.h:cplus_ctime | time.h:ctime | tests/libc/test_time_ascii.c | asctime/ctime are documented NON-REENTRANT per ISO C; prefer cplus_asctime_r/cplus_ctime_r. Delegates to host time functions; struct tm/clock_t are host-defined. |
| strftime | function | C89 | `PLATFORM_BACKED` | libc/src/time/time.c | cplus_libc.h:cplus_strftime | time.h:strftime | tests/libc/test_math_time_locale.c | Delegates to host time functions; struct tm/clock_t are host-defined. |
| time | function | C89 | `PLATFORM_BACKED` | libc/src/time/time.c | cplus_libc.h:cplus_time | time.h:time | tests/libc/test_math_time_locale.c | Delegates to host time functions; struct tm/clock_t are host-defined. |
| timespec_get | function | C11 | `PLATFORM_BACKED` | libc/src/time/time.c | cplus_libc.h:cplus_timespec_get | time.h:timespec_get | tests/libc/test_math_time_locale.c | Delegates to host time functions; struct tm/clock_t are host-defined. |
| CLOCKS_PER_SEC | macro | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <time.h>. |
| TIME_UTC | macro | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <time.h>. |
| clock_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrappers use host layout. |
| time_t | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrappers use host layout. |
| struct tm | type | C89 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrappers use host layout. |
| struct timespec | type | C11 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrappers use host layout. |
| timespec_getres | function | C23 | `MISSING` | — | — | — | — | C23 addition to <time.h>; no cplus wrapper; not shipped. |

### `uchar.h` — 9 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| char16_t | type | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include uchar.h FAILS ("uchar.h file not found") on Apple Clang 16 / macOS arm64. No cplus wrapper type shipped. |
| char32_t | type | C11 | `MISSING` | — | — | — | — | Host probe: /usr/bin/clang -std=c11 -include uchar.h FAILS ("uchar.h file not found") on Apple Clang 16 / macOS arm64. No cplus wrapper type shipped. |
| mbrtoc16 | function | C11 | `MISSING` | — | — | — | — | C11 UTF-16/UTF-32 conversion. Host <uchar.h> absent (compile probe failed); no cplus_ implementation. Use the wchar.h multibyte wrappers (cplus_mbrtowc/cplus_wcrtomb) as a UTF-32-in-wchar_t substitute only, with a documented wchar_t-size caveat. |
| c16rtomb | function | C11 | `MISSING` | — | — | — | — | C11 UTF-16/UTF-32 conversion. Host <uchar.h> absent (compile probe failed); no cplus_ implementation. Use the wchar.h multibyte wrappers (cplus_mbrtowc/cplus_wcrtomb) as a UTF-32-in-wchar_t substitute only, with a documented wchar_t-size caveat. |
| mbrtoc32 | function | C11 | `MISSING` | — | — | — | — | C11 UTF-16/UTF-32 conversion. Host <uchar.h> absent (compile probe failed); no cplus_ implementation. Use the wchar.h multibyte wrappers (cplus_mbrtowc/cplus_wcrtomb) as a UTF-32-in-wchar_t substitute only, with a documented wchar_t-size caveat. |
| c32rtomb | function | C11 | `MISSING` | — | — | — | — | C11 UTF-16/UTF-32 conversion. Host <uchar.h> absent (compile probe failed); no cplus_ implementation. Use the wchar.h multibyte wrappers (cplus_mbrtowc/cplus_wcrtomb) as a UTF-32-in-wchar_t substitute only, with a documented wchar_t-size caveat. |
| char8_t | type | C23 | `MISSING` | — | — | — | — | C23 addition; host header absent and no wrapper shipped. |
| mbrtoc8 | function | C23 | `MISSING` | — | — | — | — | C23 addition; host header absent and no wrapper shipped. |
| c8rtomb | function | C23 | `MISSING` | — | — | — | — | C23 addition; host header absent and no wrapper shipped. |

### `wchar.h` — 59 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| wcslen | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wcslen | wchar.h:wcslen | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wcscpy | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wcscpy | wchar.h:wcscpy | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wcsncpy | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wcsncpy | wchar.h:wcsncpy | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wcscmp | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wcscmp | wchar.h:wcscmp | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wcsncmp | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wcsncmp | wchar.h:wcsncmp | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wcscat | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wcscat | wchar.h:wcscat | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wcsstr | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wcsstr | wchar.h:wcsstr | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wmemcpy | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wmemcpy | wchar.h:wmemcpy | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wmemmove | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wmemmove | wchar.h:wmemmove | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wmemset | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wmemset | wchar.h:wmemset | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wmemcmp | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar.c | cplus_libc.h:cplus_wmemcmp | wchar.h:wmemcmp | tests/libc/test_wchar.c | Platform-backed wrapper delegating to host <wchar.h>; caller owns buffer validity. |
| wcstol | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcstol | wchar.h:wcstol | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| wcstoll | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcstoll | wchar.h:wcstoll | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| wcstoul | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcstoul | wchar.h:wcstoul | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| wcstoull | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcstoull | wchar.h:wcstoull | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| wcstod | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcstod | wchar.h:wcstod | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| wcstof | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcstof | wchar.h:wcstof | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| wcstold | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcstold | wchar.h:wcstold | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| wcstok | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcstok | wchar.h:wcstok | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| wcsftime | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_numeric.c | cplus_libc.h:cplus_wcsftime | wchar.h:wcsftime | tests/libc/test_wchar_numeric.c | Wide numeric/token/time conversion wrapper delegating to host; base/endptr semantics are host-defined. |
| mbrtowc | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_mbrtowc | wchar.h:mbrtowc | tests/libc/test_wchar_multibyte.c | Restartable multibyte conversion wrapper delegating to host; mbstate_t validity is caller responsibility. |
| wcrtomb | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_wcrtomb | wchar.h:wcrtomb | tests/libc/test_wchar_multibyte.c | Restartable multibyte conversion wrapper delegating to host; mbstate_t validity is caller responsibility. |
| mbsrtowcs | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_mbsrtowcs | wchar.h:mbsrtowcs | tests/libc/test_wchar_multibyte.c | Restartable multibyte conversion wrapper delegating to host; mbstate_t validity is caller responsibility. |
| wcsrtombs | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_wcsrtombs | wchar.h:wcsrtombs | tests/libc/test_wchar_multibyte.c | Restartable multibyte conversion wrapper delegating to host; mbstate_t validity is caller responsibility. |
| fgetwc | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_fgetwc | wchar.h:fgetwc | tests/libc/test_wchar_multibyte.c | Wide stream character I/O wrapper delegating to host; getwc/putwc use the function form to avoid host macros. |
| fputwc | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_fputwc | wchar.h:fputwc | tests/libc/test_wchar_multibyte.c | Wide stream character I/O wrapper delegating to host; getwc/putwc use the function form to avoid host macros. |
| getwc | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_getwc | wchar.h:fgetwc (function form, avoids host macro) | tests/libc/test_wchar_multibyte.c | Wide stream character I/O wrapper delegating to host; getwc/putwc use the function form to avoid host macros. |
| putwc | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_putwc | wchar.h:fputwc (function form, avoids host macro) | tests/libc/test_wchar_multibyte.c | Wide stream character I/O wrapper delegating to host; getwc/putwc use the function form to avoid host macros. |
| getwchar | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_getwchar | wchar.h:fgetwc(stdin) (avoids host macro) | tests/libc/test_wchar_multibyte.c | Wide stream character I/O wrapper delegating to host; getwc/putwc use the function form to avoid host macros. |
| putwchar | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_putwchar | wchar.h:fputwc(stdout) (avoids host macro) | tests/libc/test_wchar_multibyte.c | Wide stream character I/O wrapper delegating to host; getwc/putwc use the function form to avoid host macros. |
| ungetwc | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_multibyte.c | cplus_libc.h:cplus_ungetwc | wchar.h:ungetwc | tests/libc/test_wchar_multibyte.c | Wide stream character I/O wrapper delegating to host; getwc/putwc use the function form to avoid host macros. |
| wprintf | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_wprintf | wchar.h:vwprintf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| vwprintf | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_vwprintf | wchar.h:vwprintf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| fwprintf | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_fwprintf | wchar.h:vfwprintf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| vfwprintf | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_vfwprintf | wchar.h:vfwprintf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| swprintf | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_swprintf | wchar.h:vswprintf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| vswprintf | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_vswprintf | wchar.h:vswprintf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| wscanf | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_wscanf | wchar.h:vwscanf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| vwscanf | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_vwscanf | wchar.h:vwscanf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| fwscanf | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_fwscanf | wchar.h:vfwscanf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| vfwscanf | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_vfwscanf | wchar.h:vfwscanf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| swscanf | function | C95 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_swscanf | wchar.h:vswscanf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| vswscanf | function | C99 | `PLATFORM_BACKED` | libc/src/wchar/wchar_format.c | cplus_libc.h:cplus_vswscanf | wchar.h:vswscanf | tests/libc/test_wchar_format.c | Wide formatted I/O wrapper delegating to host. Apple Clang does not implement format(wprintf/wscanf,...) checking, so wide format strings are caller-responsibility contracts, not statically checked. |
| wcscoll | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| wcsxfrm | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| wcschr | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| wcsrchr | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| wcspbrk | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| wcsspn | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| wcscspn | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| wmemchr | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| fgetws | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| fputws | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| mbsinit | function | C95 | `MISSING` | — | — | — | — | Required by ISO C <wchar.h> but no cplus_ wrapper is declared in cplus_libc.h and no source definition exists. The host <wchar.h> provides it, but this library does not expose a namespaced entry point. |
| wchar_t | type | C95 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type used by the wrappers. |
| wint_t | type | C95 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type used by the wrappers. |
| mbstate_t | type | C95 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type used by the wrappers. |
| size_t | type | C95 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type used by the wrappers. |
| WEOF | macro | C95 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <wchar.h>. |

### `wctype.h` — 21 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| iswalnum | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswalnum | wctype.h:iswalnum | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswalpha | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswalpha | wctype.h:iswalpha | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswblank | function | C99 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswblank | wctype.h:iswblank | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswcntrl | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswcntrl | wctype.h:iswcntrl | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswdigit | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswdigit | wctype.h:iswdigit | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswgraph | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswgraph | wctype.h:iswgraph | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswlower | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswlower | wctype.h:iswlower | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswprint | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswprint | wctype.h:iswprint | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswpunct | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswpunct | wctype.h:iswpunct | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswspace | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswspace | wctype.h:iswspace | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswupper | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswupper | wctype.h:iswupper | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswxdigit | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswxdigit | wctype.h:iswxdigit | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| towlower | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_towlower | wctype.h:towlower | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| towupper | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_towupper | wctype.h:towupper | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| wctype | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_wctype | wctype.h:wctype | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| iswctype | function | C95 | `PLATFORM_BACKED` | libc/src/wctype/wctype.c | cplus_libc.h:cplus_iswctype | wctype.h:iswctype | tests/libc/test_ctype_full.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| wctrans | function | C99 | `PLATFORM_BACKED` | libc/src/wctype/wctype_extra.c | cplus_libc.h:cplus_wctrans | wctype.h:wctrans | tests/libc/test_wctype_extra.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| towctrans | function | C99 | `PLATFORM_BACKED` | libc/src/wctype/wctype_extra.c | cplus_libc.h:cplus_towctrans | wctype.h:towctrans | tests/libc/test_wctype_extra.c | Platform-backed wrapper delegating to host <wctype.h>; locale state is host-managed. |
| wint_t | type | C95 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrapper signatures use host type. |
| wctype_t | type | C95 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrapper signatures use host type. |
| wctrans_t | type | C95 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type; wrapper signatures use host type. |

### `complex.h` — 69 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| cabs | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex.c | cplus_libc.h:cplus_cabs | complex.h:cabs | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| carg | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex.c | cplus_libc.h:cplus_carg | complex.h:carg | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| creal | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex.c | cplus_libc.h:cplus_creal | complex.h:creal | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cimag | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex.c | cplus_libc.h:cplus_cimag | complex.h:cimag | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| conj | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex.c | cplus_libc.h:cplus_conj | complex.h:conj | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cexp | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex.c | cplus_libc.h:cplus_cexp | complex.h:cexp | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csqrt | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex.c | cplus_libc.h:cplus_csqrt | complex.h:csqrt | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csin | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csin | complex.h:csin | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ccos | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ccos | complex.h:ccos | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ctan | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ctan | complex.h:ctan | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csinh | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csinh | complex.h:csinh | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ccosh | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ccosh | complex.h:ccosh | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ctanh | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ctanh | complex.h:ctanh | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| clog | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_clog | complex.h:clog | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cpow | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cpow | complex.h:cpow | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cproj | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cproj | complex.h:cproj | tests/libc/test_complex.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cacos | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| casin | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| catan | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| cacosh | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| casinh | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| catanh | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| cabsf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cabsf | complex.h:cabsf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cargf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cargf | complex.h:cargf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| crealf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_crealf | complex.h:crealf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cimagf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cimagf | complex.h:cimagf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| conjf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_conjf | complex.h:conjf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cexpf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cexpf | complex.h:cexpf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csqrtf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csqrtf | complex.h:csqrtf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csinf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csinf | complex.h:csinf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ccosf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ccosf | complex.h:ccosf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ctanf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ctanf | complex.h:ctanf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csinhf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csinhf | complex.h:csinhf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ccoshf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ccoshf | complex.h:ccoshf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ctanhf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ctanhf | complex.h:ctanhf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| clogf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_clogf | complex.h:clogf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cpowf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cpowf | complex.h:cpowf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cprojf | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cprojf | complex.h:cprojf | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cacosf | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| casinf | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| catanf | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| cacoshf | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| casinhf | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| catanhf | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| cabsl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cabsl | complex.h:cabsl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cargl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cargl | complex.h:cargl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| creall | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_creall | complex.h:creall | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cimagl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cimagl | complex.h:cimagl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| conjl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_conjl | complex.h:conjl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cexpl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cexpl | complex.h:cexpl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csqrtl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csqrtl | complex.h:csqrtl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csinl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csinl | complex.h:csinl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ccosl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ccosl | complex.h:ccosl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ctanl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ctanl | complex.h:ctanl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| csinhl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_csinhl | complex.h:csinhl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ccoshl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ccoshl | complex.h:ccoshl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| ctanhl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_ctanhl | complex.h:ctanhl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| clogl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_clogl | complex.h:clogl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cpowl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cpowl | complex.h:cpowl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cprojl | function | C99 | `PLATFORM_BACKED` | libc/src/complex/complex_extra.c | cplus_libc.h:cplus_cprojl | complex.h:cprojl | tests/libc/test_complex_extra.c | Platform-backed complex wrapper delegating to host libm complex arithmetic. |
| cacosl | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| casinl | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| catanl | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| cacoshl | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| casinhl | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| catanhl | function | C99 | `MISSING` | — | — | — | — | Required by ISO C <complex.h> but no cplus_ wrapper exists. Host provides it, but this library does not expose a namespaced entry point. |
| complex | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler macro/keyword from host <complex.h>. C23 removes the imaginary macro/keyword; _Complex is the keyword. |
| imaginary | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler macro/keyword from host <complex.h>. C23 removes the imaginary macro/keyword; _Complex is the keyword. |
| I | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compiler macro/keyword from host <complex.h>. C23 removes the imaginary macro/keyword; _Complex is the keyword. |

### `fenv.h` — 25 APIs

| API | Kind | C version | Classification | Source | Public decl | Host backend | Test | Limitation |
|---|---|---|---|---|---|---|---|---|
| feclearexcept | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv.c | cplus_libc.h:cplus_feclearexcept | fenv.h:feclearexcept | tests/libc/test_fenv.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| fegetexceptflag | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv_extra.c | cplus_libc.h:cplus_fegetexceptflag | fenv.h:fegetexceptflag | tests/libc/test_fenv_extra.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| feraiseexcept | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv_extra.c | cplus_libc.h:cplus_feraiseexcept | fenv.h:feraiseexcept | tests/libc/test_fenv_extra.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| fesetexceptflag | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv_extra.c | cplus_libc.h:cplus_fesetexceptflag | fenv.h:fesetexceptflag | tests/libc/test_fenv_extra.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| fetestexcept | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv.c | cplus_libc.h:cplus_fetestexcept | fenv.h:fetestexcept | tests/libc/test_fenv.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| fegetround | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv.c | cplus_libc.h:cplus_fegetround | fenv.h:fegetround | tests/libc/test_fenv.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| fesetround | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv.c | cplus_libc.h:cplus_fesetround | fenv.h:fesetround | tests/libc/test_fenv.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| fegetenv | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv.c | cplus_libc.h:cplus_fegetenv | fenv.h:fegetenv | tests/libc/test_fenv.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| fesetenv | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv.c | cplus_libc.h:cplus_fesetenv | fenv.h:fesetenv | tests/libc/test_fenv.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| feholdexcept | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv_extra.c | cplus_libc.h:cplus_feholdexcept | fenv.h:feholdexcept | tests/libc/test_fenv_extra.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| feupdateenv | function | C99 | `PLATFORM_BACKED` | libc/src/fenv/fenv_extra.c | cplus_libc.h:cplus_feupdateenv | fenv.h:feupdateenv | tests/libc/test_fenv_extra.c | Platform-backed wrapper delegating to host <fenv.h>. FP environment is host/thread-specific; reentrancy and signal-safety are not guaranteed. |
| fenv_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type used by wrapper signatures. |
| fexcept_t | type | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Host-defined type used by wrapper signatures. |
| FE_ALL_EXCEPT | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_DIVBYZERO | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_INEXACT | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_INVALID | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_OVERFLOW | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_UNDERFLOW | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_DOWNWARD | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_TONEAREST | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_TOWARDZERO | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_UPWARD | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_DFL_ENV | macro | C99 | `HOST/COMPILER-PROVIDED` | — | — | — | — | Compile-time macro from host <fenv.h>; no wrapper symbol. |
| FE_TIESAWAY | macro | C23 | `MISSING` | — | — | — | — | C23 rounding-mode macro; not exposed by this library on the C17 baseline. |
