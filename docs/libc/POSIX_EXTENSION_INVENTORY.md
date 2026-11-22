# POSIX / Platform-Extension API Inventory — `libc/` (`agent/modern-stdlib`)

Exhaustive inventory of every **non-ISO-C** API exported as a `cplus_*` symbol by
`libc/install-include/cplus_libc.h`, classified **separately** from the ISO C17
surface documented in [`ISO_C_AUDIT.md`](ISO_C_AUDIT.md).

Sources of truth:

- `libc/install-include/cplus_libc.h` — authoritative list of all exported `cplus_*` declarations.
- `libc/src/**/*.c` — implementation bodies (host delegation vs. bundled code).
- `docs/libc/ISO_C_AUDIT.md` and `docs/libc/API_TEST_MATRIX.md` — existing classification and host-backend/test mapping.
- `tests/libc/**` — test references (verified by grep, see §6).

## 0. Critical separation: POSIX support is NOT ISO C support

> **POSIX support is not ISO C support.** Nothing in this document is part of ISO C17.
> The APIs below come from POSIX.1-2001/2008, the XSI/XOPEN option group, BSD/macOS,
> GNU/glibc, or legacy System V extensions. A conforming ISO C implementation is
> permitted to provide **none** of them.
>
> **Platform availability must be probed per target before use.** macOS, Linux/glibc,
> and Windows/MSVC expose different subsets with different signatures (notably
> `qsort_r`, `strerror_r`, and the `*r` time converters). The host build is Darwin in
> CI; the header's use of `pthread.h`, `sigset_t`, `struct sigaction`, `clockid_t`, and
> `pid_t` means it does **not** compile against a bare Windows CRT without a POSIX
> compatibility layer (MinGW-w64, Cygwin, or WSL). Do not assume a symbol that
> exists on Darwin exists on Linux or Windows, or vice versa.

Classification legend (matches `API_TEST_MATRIX.md`):

- `PLATFORM_BACKED` — thin namespaced adapter delegating to a named host C runtime function.
- `IMPLEMENTED_BUNDLED` — implemented directly in C+ (or vendored MIT musl code renamed into the C+ namespace); no host symbol required.
- `PARTIAL` — subset of the extension family available; remainder documented.
- `MISSING` — no implementation and no host backend.

"Host backend" for `IMPLEMENTED_BUNDLED` rows is `none` by definition; the note
identifies the vendored source where applicable (musl @ `7813e9119eb5fe681b6750b8a033a0490bca0941`, MIT).

## 1. Inventory table

### 1.1 String, memory, ctype, and errno extensions

| API | Extension family | C+ exported symbol | Source file | Host backend | Test | Classification | Platform notes (macOS / Linux / Windows) | Known limitation |
|---|---|---|---|---|---|---|---|---|
| `strcasecmp` | POSIX.1-2008 (BSD origin) | `cplus_strcasecmp` | `libc/src/string/extra2.c` | none — bundled C+ impl | `test_per_function_10.c`, `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ✓; Linux ✓; Windows ✗ (MSVC `_stricmp` only) | ASCII-only case folding; no locale/casefold; raw pointer/NUL validity caller responsibility |
| `strncasecmp` | POSIX.1-2008 (BSD origin) | `cplus_strncasecmp` | `libc/src/string/extra2.c`, `libc/src/string/musl_extra.c` | none — bundled C+ impl | `test_per_function_10.c`, `fuzz_libc2.c`, `test_musl_extra.c` | IMPLEMENTED_BUNDLED | macOS ✓; Linux ✓; Windows ✗ | ASCII-only case folding; NUL beyond `n` not defensive; caller-owned buffers |
| `strdup` | POSIX.1-2008 (XSI origin) | `cplus_strdup` | `libc/src/string/extra2.c` | none — bundled C+ impl (`cplus_malloc`) | `test_per_function_10.c`, `fuzz_libc2.c`, `test_concurrency_attack.c` | IMPLEMENTED_BUNDLED | macOS ✓; Linux ✓; Windows ✗ (MSVC `_strdup`) | Allocates via `cplus_malloc`; caller must `cplus_free`; NULL input returns NULL |
| `strndup` | POSIX.1-2008 | `cplus_strndup` | `libc/src/string/strndup.c` | none — bundled C+ impl (`cplus_malloc`) | `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ✓; Linux ✓; Windows ✗ | Allocates via `cplus_malloc`; caller must `cplus_free`; no capacity/alias checks |
| `strnlen` | POSIX.1-2008 (GNU origin) | `cplus_strnlen` | `libc/src/string/extra.c` | none — bundled C+ impl | `test_per_function_10.c`, `test_corpus_buffer.c` | IMPLEMENTED_BUNDLED | macOS ✓; Linux ✓; Windows ✗ (MSVC `strnlen_s`) | Returns 0 on NULL (non-standard extension of the contract); raw pointer validity caller responsibility |
| `memrchr` | GNU (glibc) | `cplus_memrchr` | `libc/src/string/extra2.c` | none — bundled C+ impl | `test_per_function_10.c`, `test_corpus_buffer.c` | IMPLEMENTED_BUNDLED | macOS ✗ (not declared by macOS `<string.h>`); Linux/glibc ✓; Windows ✗ | Bundled precisely so it works on macOS; `n == SIZE_MAX` scans from `p[n-1]` (documented corpus constraint) |
| `memccpy` | POSIX.1-2008 (XSI / GNU origin) | `cplus_memccpy` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c`, `test_corpus_null.c` | IMPLEMENTED_BUNDLED | macOS ✗/⚠ (not portable); Linux/glibc ✓; Windows ✗ | musl MIT code; word-at-a-time `__GNUC__` fast path; raw pointer/capacity validity caller responsibility |
| `stpcpy` | POSIX.1-2008 (GNU origin) | `cplus_stpcpy` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c` | IMPLEMENTED_BUNDLED | macOS ⚠ (not in all SDK headers); Linux/glibc ✓; Windows ✗ | Returns pointer to NUL terminator; destination must have room; `restrict` aliasing caller responsibility |
| `stpncpy` | POSIX.1-2008 (GNU origin) | `cplus_stpncpy` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c` | IMPLEMENTED_BUNDLED | macOS ⚠; Linux/glibc ✓; Windows ✗ | Pads with NUL up to `n`; truncation not reported; destination capacity caller responsibility |
| `strtok_r` | POSIX.1-2001 | `cplus_strtok_r` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c`, `fuzz_libc2.c`, `test_corpus_null.c` | IMPLEMENTED_BUNDLED | macOS ✓; Linux ✓; Windows ✗ | Modifies input; `saveptr` is unguarded (NULL `saveptr` is caller bug); not thread-safe on shared state |
| `strsep` | BSD / macOS | `cplus_strsep` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c`, `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ✓; Linux/glibc ✓; Windows ✗ | Modifies input; empty tokens are returned (differs from `strtok_r`); `*stringp` unguarded |
| `memmem` | GNU / BSD (dual) | `cplus_memmem` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c`, `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ⚠ (GNU name, BSD has it); Linux/glibc ✓; Windows ✗ | Bundled two-way algorithm; empty needle returns haystack; needle/haystack capacity caller responsibility |
| `strverscmp` | GNU (glibc) | `cplus_strverscmp` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c`, `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ✗; Linux/glibc ✓; Windows ✗ | GNU version-sort semantics (leading-zero rules); locale-independent; strings must be NUL-terminated |
| `strcasestr` | GNU / BSD (dual) | `cplus_strcasestr` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c`, `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ✓ (BSD); Linux/glibc ✓; Windows ✗ | ASCII-only case folding; delegates substring search to bundled `cplus_strncasecmp` |
| `strchrnul` | GNU (glibc) | `cplus_strchrnul` | `libc/src/string/musl_extra.c` | none — musl-derived bundled impl | `test_musl_extra.c`, `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ✗; Linux/glibc ✓; Windows ✗ | Returns pointer to terminating NUL when not found (never NULL); `__GNUC__` word fast path |
| `strlcpy` | BSD / macOS | `cplus_strlcpy` | `libc/src/string/string_extra6.c` | host `strlcpy` | `test_extra6_string.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓ (native); Linux/glibc ⚠ (glibc 2.38+; otherwise absent); Windows ✗ | Returns source length (truncation detectable); destination capacity `size` caller responsibility; size 0 edge case |
| `strlcat` | BSD / macOS | `cplus_strlcat` | `libc/src/string/string_extra6.c` | host `strlcat` | `test_extra6_string.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓ (native); Linux/glibc ⚠ (glibc 2.38+; otherwise absent); Windows ✗ | Destination must be NUL-terminated within `size`; returned length can exceed `size` on truncation |
| `strnstr` | BSD / macOS | `cplus_strnstr` | `libc/src/string/string_extra6.c` | host `strnstr` | `test_extra6_string.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓ (native); Linux/glibc ✗ (no glibc equivalent); Windows ✗ | Bounded haystack only; needle must be NUL-terminated; do not use for binary data |
| `swab` | POSIX.1-2001 XSI / XOPEN | `cplus_swab` | `libc/src/string/string_extra5.c` | host `swab` | `test_string_extra5.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Uses `ssize_t`; odd `n` handling is host-defined; source/destination must not overlap |
| `strsignal` | POSIX.1-2008 (GNU/BSD origin) | `cplus_strsignal` | `libc/src/string/string_extra4.c` | host `strsignal` | `test_string_extra4.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Returns pointer to host-owned static/description storage; non-reentrant; unknown signals are host-defined |
| `strerror_r` | POSIX.1-2001 XSI | `cplus_strerror_r` | `libc/src/stdlib/assert_extra.c` | host `strerror_r` (int-returning XSI form) | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓ (int form); Linux/glibc ⚠ (GNU `char *` form selected by feature macros — wrapper forces XSI int form); Windows ✗ | Only the `int` form is supported; GNU `char *` form is explicitly excluded; `buf`/`buflen` caller responsibility |
| `isascii` | Legacy BSD / XSI (obsolescent, non-POSIX.1-2008 base) | `cplus_isascii` | `libc/src/ctype/ctype3.c` | none — musl-derived bundled impl | `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ✓ (macro); Linux/glibc ✓ (macro); Windows ✗ | Bit-mask implementation `!(c & ~0x7f)`; no locale awareness |
| `toascii` | Legacy BSD / XSI (obsolescent, non-POSIX.1-2008 base) | `cplus_toascii` | `libc/src/ctype/ctype3.c` | none — musl-derived bundled impl | `fuzz_libc2.c` | IMPLEMENTED_BUNDLED | macOS ✓ (macro); Linux/glibc ✓ (macro); Windows ✗ | Simple `c & 0x7f`; no locale awareness |

### 1.2 stdlib extensions

| API | Extension family | C+ exported symbol | Source file | Host backend | Test | Classification | Platform notes (macOS / Linux / Windows) | Known limitation |
|---|---|---|---|---|---|---|---|---|
| `setenv` | POSIX.1-2001 | `cplus_setenv` | `libc/src/stdlib/stdlib_extra4.c` | host `setenv` | `test_stdlib_extra4.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ (`_putenv_s` only) | Process-global environment; not thread-safe against concurrent `getenv`/`putenv`; `overwrite` flag host semantics |
| `unsetenv` | POSIX.1-2001 | `cplus_unsetenv` | `libc/src/stdlib/stdlib_extra4.c` | host `unsetenv` | `test_stdlib_extra4.c`, `test_coverage_untested.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Process-global; nonexistent name may still return 0 (host-defined) |
| `putenv` | POSIX.1-2001 | `cplus_putenv` | `libc/src/stdlib/stdlib_extra4.c` | host `putenv` | `test_stdlib_extra4.c`, `test_coverage_untested.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ (`_putenv`) | String passed is not copied on all hosts — caller must keep it alive; unsafe with automatic storage |
| `rand_r` | POSIX.1-2001 (obsolescent in POSIX.1-2008) | `cplus_rand_r` | `libc/src/stdlib/stdlib_extra4.c` | host `rand_r` | `test_stdlib_extra4.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Reentrant but weak PRNG; `seed` pointer unguarded; deprecated by POSIX |
| `ecvt` | Legacy BSD / System V (obsolete, not POSIX) | `cplus_ecvt` | `libc/src/stdlib/stdlib_extra5.c` | host `ecvt` | `test_stdlib_extra5.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux/glibc ✓ but deprecated; Windows ✗ (`_ecvt`) | Returns static buffer (non-reentrant); locale-dependent; obsolescent |
| `fcvt` | Legacy BSD / System V (obsolete, not POSIX) | `cplus_fcvt` | `libc/src/stdlib/stdlib_extra5.c` | host `fcvt` | `test_stdlib_extra5.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux/glibc ✓ but deprecated; Windows ✗ (`_fcvt`) | Returns static buffer (non-reentrant); rounding/decpt semantics host-defined |
| `gcvt` | Legacy BSD / System V (obsolete, not POSIX) | `cplus_gcvt` | `libc/src/stdlib/stdlib_extra5.c` | host `gcvt` | `test_stdlib_extra5.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux/glibc ✓ but deprecated; Windows ✗ (`_gcvt`) | Caller-owned buffer; buffer size not passed (overflow risk); obsolescent |
| `drand48` | POSIX.1-2001 XSI | `cplus_drand48` | `libc/src/stdlib/stdlib_extra5.c` | host `drand48` | `test_stdlib_extra5.c` | PLATFORM_BACKED | macOS ✓; Linux/glibc ✓; Windows ✗ | Uses hidden global XSI state (non-reentrant); use `erand48` for explicit state |
| `erand48` | POSIX.1-2001 XSI | `cplus_erand48` | `libc/src/stdlib/stdlib_extra5.c` | host `erand48` | `test_stdlib_extra5.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux/glibc ✓; Windows ✗ | Caller must supply mutable 3-element state; not cryptographically secure |
| `seed48` | POSIX.1-2001 XSI | `cplus_seed48` | `libc/src/stdlib/stdlib_extra5.c` | host `seed48` | `test_stdlib_extra5.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux/glibc ✓; Windows ✗ | Returns pointer to previous host static state (non-reentrant); global state seeded |
| `lrand48` | POSIX.1-2001 XSI | `cplus_lrand48` | `libc/src/stdlib/stdlib_extra5.c` | host `lrand48` | `test_stdlib_extra5.c` | PLATFORM_BACKED | macOS ✓; Linux/glibc ✓; Windows ✗ | Uses hidden global XSI state (non-reentrant); range host-defined |
| `mkstemp` | POSIX.1-2008 (XSI origin) | `cplus_mkstemp` | `libc/src/stdlib/stdlib_extra6.c` | host `mkstemp` | `test_extra6_stdlib.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓ (declared in `<unistd.h>`, not `<stdlib.h>`); Linux ✓; Windows ✗ | Template must end in `XXXXXX` and is modified in place; caller must close returned fd |
| `mkdtemp` | POSIX.1-2008 (XSI origin) | `cplus_mkdtemp` | `libc/src/stdlib/stdlib_extra6.c` | host `mkdtemp` | `test_extra6_stdlib.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓ (`<unistd.h>`); Linux ✓; Windows ✗ | Template must end in `XXXXXX`; caller must remove directory when done |
| `realpath` | POSIX.1-2008 (XSI origin) | `cplus_realpath` | `libc/src/stdlib/stdlib_extra6.c` | host `realpath` | `test_extra6_stdlib.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ (Win32 `GetFullPathName`) | With `resolved == NULL` host allocates (host-specific allocator — do not mix with `cplus_free`); path length limits host-defined |
| `qsort_r` | GNU / BSD (dual, incompatible signatures) | `cplus_qsort_r` | `libc/src/stdlib/stdlib_extra6.c` | host `qsort_r` — **BSD/macOS argument order** `(base,nmemb,size,void*,compar)` | `test_extra6_stdlib.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓ (BSD order); Linux/glibc ⚠ **ABI mismatch** (glibc order is `(base,nmemb,size,compar,void*)`); Windows ✗ | Wrapper hard-codes BSD context-before-comparator order; on glibc this is a silent ABI break — must be gated/changed per host before shipping Linux |

### 1.3 POSIX time extensions

| API | Extension family | C+ exported symbol | Source file | Host backend | Test | Classification | Platform notes (macOS / Linux / Windows) | Known limitation |
|---|---|---|---|---|---|---|---|---|
| `clock_gettime` | POSIX.1-2001 | `cplus_clock_gettime` | `libc/src/time/time_extra.c` | host `clock_gettime` | `test_extra6_time.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓ (10.12+); Linux ✓; Windows ✗ (Win32 `QueryPerformanceCounter`) | `CLOCK_MONOTONIC` availability/clock IDs are host-specific; `tp` pointer unguarded |
| `nanosleep` | POSIX.1-2001 | `cplus_nanosleep` | `libc/src/time/time_extra.c` | host `nanosleep` | `test_extra6_time.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ (Win32 `Sleep`) | May return early on signal (`rem` filled); sub-ms resolution host-dependent; `req`/`rem` pointer validity caller responsibility |
| `gmtime_r` | POSIX.1-2001/2008 | `cplus_gmtime_r` | `libc/src/time/time_extra.c` | host `gmtime_r` | `test_c17_additions.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ (`gmtime_s` is different) | Wrapper adds NULL-check (`EINVAL`); `result` buffer caller-owned |
| `localtime_r` | POSIX.1-2001/2008 | `cplus_localtime_r` | `libc/src/time/time_extra.c` | host `localtime_r` | `test_c17_additions.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Wrapper adds NULL-check (`EINVAL`); result depends on host timezone/locale state |
| `asctime_r` | POSIX.1-2001/2008 | `cplus_asctime_r` | `libc/src/time/time_extra.c` | host `asctime_r` | `test_c17_additions.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Wrapper adds NULL-check (`EINVAL`); buffer must hold ≥26 bytes; out-of-range `tm` host-defined |
| `ctime_r` | POSIX.1-2001/2008 | `cplus_ctime_r` | `libc/src/time/time_extra.c` | host `ctime_r` | `test_c17_additions.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Wrapper adds NULL-check (`EINVAL`); buffer ≥26 bytes; locale/timezone dependent |

### 1.4 POSIX signal-set and signal-control extensions

| API | Extension family | C+ exported symbol | Source file | Host backend | Test | Classification | Platform notes (macOS / Linux / Windows) | Known limitation |
|---|---|---|---|---|---|---|---|---|
| `sigemptyset` | POSIX.1-2001 | `cplus_sigemptyset` | `libc/src/signal/signal2.c` | host `sigemptyset` | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | `sigset_t` is host ABI type; NULL `set` unguarded |
| `sigfillset` | POSIX.1-2001 | `cplus_sigfillset` | `libc/src/signal/signal2.c` | host `sigfillset` | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Includes host-reserved signals; blocking all signals can be fatal |
| `sigaddset` | POSIX.1-2001 | `cplus_sigaddset` | `libc/src/signal/signal2.c` | host `sigaddset` | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Invalid signal number → host error; `set` pointer unguarded |
| `sigdelset` | POSIX.1-2001 | `cplus_sigdelset` | `libc/src/signal/signal2.c` | host `sigdelset` | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Invalid signal number → host error; `set` pointer unguarded |
| `sigismember` | POSIX.1-2001 | `cplus_sigismember` | `libc/src/signal/signal2.c` | host `sigismember` | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Returns -1 on invalid signal (not a boolean); caller must handle |
| `sigaction` | POSIX.1-2001 | `cplus_sigaction` | `libc/src/signal/signal2.c` | host `sigaction` | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | POSIX signature preserved; handler must be async-signal-safe; `SA_*` flags host-specific |
| `sigprocmask` | POSIX.1-2001 | `cplus_sigprocmask` | `libc/src/signal/signal2.c` | host `sigprocmask` | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | In multithreaded programs POSIX recommends `pthread_sigmask`; wrapper does not redirect |
| `sigsuspend` | POSIX.1-2001 | `cplus_sigsuspend` | `libc/src/signal/signal2.c` | host `sigsuspend` | `test_extra7_signal.c` (declared), `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Blocks indefinitely until a signal; `mask` pointer unguarded; no timeout |
| `kill` | POSIX.1 (base) | `cplus_kill` | `libc/src/signal/signal2.c` | host `kill` | `test_extra7_signal.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ (no POSIX `kill`) | `pid_t` semantics host-defined (negative pid = process group); permissions/EPERM host-enforced |
| `sigwait` | POSIX.1-2001 | `cplus_sigwait` | `libc/src/signal/signal2.c` | host `sigwait` | `test_extra7_signal.c` (declared), `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Signal must be blocked in all threads first; `sig` out-pointer unguarded |

### 1.5 POSIX stdio stream/descriptor extensions

| API | Extension family | C+ exported symbol | Source file | Host backend | Test | Classification | Platform notes (macOS / Linux / Windows) | Known limitation |
|---|---|---|---|---|---|---|---|---|
| `fileno` | POSIX.1 | `cplus_fileno` | `libc/src/stdio/stdio3.c` | host `fileno` | `test_stdio3.c`, `test_corpus_resource.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ (`_fileno`) | Exposes host fd; buffering must be flushed before raw fd use; fd ownership stays with the `FILE*` |
| `fdopen` | POSIX.1 | `cplus_fdopen` | `libc/src/stdio/stdio3.c` | host `fdopen` | `test_stdio3.c`, `test_corpus_resource.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ (`_fdopen`) | Registers result in C+ fd leak registry; mode must be compatible with fd flags; closing the `FILE*` closes the fd |

### 1.6 POSIX threads (pthreads) extensions

Host toolchain lacks C11 `<threads.h>` (verified by compile probe in `thread.c`); these
are deliberately named `cplus_pthread_*`, not C11 `cplus_thrd_*`/`cplus_mtx_*`/`cplus_cnd_*`.

| API | Extension family | C+ exported symbol | Source file | Host backend | Test | Classification | Platform notes (macOS / Linux / Windows) | Known limitation |
|---|---|---|---|---|---|---|---|---|
| `pthread_create` | POSIX.1-2001 (pthreads) | `cplus_pthread_create` | `libc/src/thread/thread.c` | host `pthread_create` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓ (-pthread); Windows ✗ (Win32 threads) | `attr`/`arg` may be NULL; start routine must return properly; resource cleanup caller responsibility |
| `pthread_join` | POSIX.1-2001 | `cplus_pthread_join` | `libc/src/thread/thread.c` | host `pthread_join` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Joining a detached/already-joined thread is undefined; `retval` lifetime caller responsibility |
| `pthread_mutex_init` | POSIX.1-2001 | `cplus_pthread_mutex_init` | `libc/src/thread/thread.c` | host `pthread_mutex_init` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | `attr` may be NULL; mutex must be destroyed by caller |
| `pthread_mutex_lock` | POSIX.1-2001 | `cplus_pthread_mutex_lock` | `libc/src/thread/thread.c` | host `pthread_mutex_lock` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Self-deadlock on non-recursive mutex; no timeout (use `pthread_mutex_timedlock` — not exported) |
| `pthread_mutex_unlock` | POSIX.1-2001 | `cplus_pthread_mutex_unlock` | `libc/src/thread/thread.c` | host `pthread_mutex_unlock` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Unlocking an unlocked/other-thread mutex is undefined |
| `pthread_mutex_destroy` | POSIX.1-2001 | `cplus_pthread_mutex_destroy` | `libc/src/thread/thread.c` | host `pthread_mutex_destroy` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Destroying a locked or in-use mutex is undefined |
| `pthread_cond_init` | POSIX.1-2001 | `cplus_pthread_cond_init` | `libc/src/thread/thread.c` | host `pthread_cond_init` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | `attr` may be NULL; condition variable must be destroyed by caller |
| `pthread_cond_signal` | POSIX.1-2001 | `cplus_pthread_cond_signal` | `libc/src/thread/thread.c` | host `pthread_cond_signal` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Lost wakeup if predicate not rechecked under mutex; wakes at least one waiter |
| `pthread_cond_broadcast` | POSIX.1-2001 | `cplus_pthread_cond_broadcast` | `libc/src/thread/thread.c` | host `pthread_cond_broadcast` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Thundering-herd cost; predicate must be rechecked by all waiters |
| `pthread_cond_wait` | POSIX.1-2001 | `cplus_pthread_cond_wait` | `libc/src/thread/thread.c` | host `pthread_cond_wait` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Must be called with mutex held; spurious wakeups possible (predicate loop required); no timed variant exported |
| `pthread_cond_destroy` | POSIX.1-2001 | `cplus_pthread_cond_destroy` | `libc/src/thread/thread.c` | host `pthread_cond_destroy` | `test_thread.c`, `test_corpus_null.c` | PLATFORM_BACKED | macOS ✓; Linux ✓; Windows ✗ | Destroying a condvar with waiters is undefined |

## 2. Summary counts

### 2.1 By extension family

| Extension family | APIs | Classification mix |
|---|---:|---|
| POSIX.1-2008 (incl. XSI-origin routines standardized in 2008) | 16 | 8 PLATFORM_BACKED / 8 IMPLEMENTED_BUNDLED |
| POSIX.1-2001 (base, signals, pthreads, env, clocks, stdio, XSI-free) | 30 | 29 PLATFORM_BACKED / 1 IMPLEMENTED_BUNDLED (`strtok_r`) |
| POSIX.1-2001 XSI / XOPEN | 6 | 6 PLATFORM_BACKED |
| BSD / macOS | 4 | 3 PLATFORM_BACKED / 1 IMPLEMENTED_BUNDLED (`strsep`) |
| GNU (glibc-only) | 3 | 3 IMPLEMENTED_BUNDLED |
| GNU / BSD (dual, may be ABI-incompatible) | 3 | 1 PLATFORM_BACKED (`qsort_r`) / 2 IMPLEMENTED_BUNDLED (`memmem`, `strcasestr`) |
| Legacy BSD / System V (obsolescent, not POSIX) | 5 | 2 PLATFORM_BACKED / 3 IMPLEMENTED_BUNDLED |
| **Total non-ISO-C extension APIs** | **67** | **50 PLATFORM_BACKED / 17 IMPLEMENTED_BUNDLED** |

### 2.2 By classification

| Classification | Count | Share |
|---|---:|---:|
| PLATFORM_BACKED | 50 | 74.6% |
| IMPLEMENTED_BUNDLED | 17 | 25.4% |
| PARTIAL | 0 | 0% |
| MISSING | 0 | 0% |
| **Total** | **67** | **100%** |

Every inventoried extension has an implementation and a host/bundled backend. No
extension in `cplus_libc.h` is `MISSING`; no extension family is `PARTIAL` at the
per-symbol level (family-level gaps are recorded in the platform-notes column,
e.g. GNU-only symbols are bundled precisely because macOS lacks them).

## 3. C11 additions explicitly requested for disambiguation (NOT POSIX extensions)

The task brief called out `timespec_get` and `aligned_alloc` as "C11 (not POSIX)".
They are **ISO C11/C17** and therefore out of scope for the main POSIX table, but are
recorded here for completeness. `quick_exit`, `at_quick_exit`, and `_Exit` are also
C11. All are `PLATFORM_BACKED` per `stdlib_c17.c` and `time_extra.c`.

| API | Correct classification | C+ exported symbol | Source file | Host backend | Test | Why not in main table |
|---|---|---|---|---|---|---|
| `timespec_get` | ISO C11 (C17 `<time.h>`), not POSIX | `cplus_timespec_get` | `libc/src/time/time_extra.c` | host `timespec_get` | `test_extra6_time.c` | ISO C11; POSIX provides `clock_gettime` instead |
| `aligned_alloc` | ISO C11 (C17 `<stdlib.h>`), not POSIX | `cplus_aligned_alloc` | `libc/src/stdlib/stdlib_c17.c` | host `aligned_alloc` | `test_stdlib_c17.c` | ISO C11; calls host allocator directly to preserve provenance |
| `quick_exit` | ISO C11, not POSIX | `cplus_quick_exit` | `libc/src/stdlib/stdlib_c17.c` | host `quick_exit` | `test_stdlib_c17.c` | ISO C11 |
| `at_quick_exit` | ISO C11, not POSIX | `cplus_at_quick_exit` | `libc/src/stdlib/stdlib_c17.c` | host `at_quick_exit` | `test_stdlib_c17.c` | ISO C11 |
| `_Exit` | ISO C11, not POSIX | `cplus__Exit` | `libc/src/stdlib/stdlib_c17.c` | host `_Exit` | `test_stdlib_c17.c` | ISO C11 |

## 4. Deliberately excluded from this inventory

- **ISO C17 functions that merely delegate to the host** — `setvbuf`, `setbuf`,
  `fgetpos`, `fsetpos`, `ungetc`, `freopen`, `tmpfile`, `rewind`, `fgetc`, `fputc`,
  and the entire `<math.h>`/`<complex.h>`/`<wchar.h>`/`<fenv.h>` extended surface.
  They are ISO C17, not POSIX; see `ISO_C_AUDIT.md` and `API_TEST_MATRIX.md`.
  (Their POSIX-neighbour wrappers `fileno`/`fdopen` ARE included in §1.5.)
- **C+ proprietary hardening APIs, not platform extensions** — `cplus_malloc`/`calloc`/
  `realloc`/`free`/`free_checked`, `cplus_alloc_*` metadata/query family,
  `cplus_*_bounded` capacity-checked operations, `cplus_transfer_ownership`,
  fail-injection, logging/telemetry, and `cplus_libc_set_unknown_capacity_mode`.
  These are C+ inventions with no host or standards counterpart.
- **`getopt` / `getopt_long`** — not declared in `cplus_libc.h`; no `cplus_getopt`
  symbol exists. Not inventoried (grep-verified absent).
- **Annex K `*_s` helpers** — hosted in `stdlib/safe`, not `libc`, and mostly
  `UNSUPPORTED` per `ISO_C_AUDIT.md`; Annex K is ISO C11, not POSIX.

## 5. Per-target availability warnings

1. **Do not treat this header as portable ISO C.** It includes `<pthread.h>`, uses
   `sigset_t`, `struct sigaction`, `clockid_t`, `pid_t`, and `ssize_t`. A stock
   Windows/MSVC CRT provides none of these; a POSIX compatibility layer is required.
2. **`qsort_r` is the highest-risk ABI mismatch.** The C+ wrapper hard-codes BSD/macOS
   argument order (context before comparator). glibc uses the opposite order. Passing
   it unmodified on Linux will produce a comparator call with swapped arguments and
   undefined behavior. Gate or add a host-specific implementation before Linux release.
3. **GNU-only names are bundled on purpose.** `memrchr`, `strverscmp`, `strchrnul`,
   `memmem`, and `strcasestr` are implemented from musl so the API works on macOS,
   where the host `<string.h>` does not declare them. They are not host-delegated.
4. **BSD-only names are host-delegated and are NOT on glibc.** `strlcpy`, `strlcat`,
   and `strnstr` call the host directly; glibc ≤2.37 lacks them. Do not assume Linux
   availability without a probe.
5. **`strerror_r` has two incompatible POSIX/GNU forms.** This project uses the
   `int`-returning XSI form only; feature-test macros on glibc must be controlled.
6. **No extension wrapper makes raw pointers safe.** All entries inherit the C+
   contract: pointer validity, NUL-termination, buffer capacity, format-string
   correctness, and allocator provenance remain the caller's responsibility.

## 6. Verification against the public header

Method: enumerated every `cplus_*` declaration in `libc/install-include/cplus_libc.h`,
classified each as ISO C17 / C11 / POSIX-platform / C+-proprietary, then grepped
`docs/libc/POSIX_EXTENSION_INVENTORY.md` for every non-ISO, non-proprietary symbol.

- Non-ISO, non-proprietary `cplus_*` symbols found in the header: **67**.
- Symbols present in this inventory: **67**.
- **Omissions: none.**
- `cplus_getopt` / `cplus_getopt_long`: **not present in the header** (no omission).
- C11 symbols called out by the brief (`timespec_get`, `aligned_alloc`, `quick_exit`,
  `at_quick_exit`, `_Exit`) are documented in §3 and intentionally excluded from the
  POSIX table because they are ISO C, not POSIX.
- Source files cross-checked: all 67 symbols resolved to a concrete `libc/src/**/*.c`
  definition; all 67 resolved to at least one test reference under `tests/libc/`.
