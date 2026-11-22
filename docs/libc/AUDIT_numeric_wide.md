# AUDIT_numeric_wide — math / time / wchar / wctype / locale / conversion wrappers

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23  **Mode:** READ-ONLY audit
(only this file was created; no source, build, test or contract file was changed).

**Sources audited:** `libc/src/math/*.c`, `libc/src/time/*.c`, `libc/src/wchar/*.c`,
`libc/src/wctype/*.c`, `libc/src/locale/locale.c`,
`libc/src/stdlib/stdlib_extra*.c`, `libc/src/stdlib/musl_stdlib_extra.c`,
`libc/src/stdlib/inttypes_wide.c`, `libc/log/log.c`,
`libc/install-include/cplus_libc.h`, `libc/upstream/musl/PROVENANCE.md`,
plus the C23 blocker registers `docs/libc/{C23_MATH_UPSTREAM,C23_SMALL_UPSTREAM,
UCHARC23_UPSTREAM,MATH_GAPS_EVIDENCE,COVERAGE_100_AUDIT}.md`.

**Backend legend**
- **HOST** = thin `cplus_*` delegate to identically named system libm/libc symbol;
  no independent algorithm, no symbol interposition. Rounding, `errno`, FP
  exception flags, domain/range and locale behaviour are whatever the host does.
- **BUILTIN** = delegate to a compiler intrinsic (`__builtin_*`).
- **MUSL-COPY** = verbatim upstream body (musl @ `7813e911…`, MIT, see
  `PROVENANCE.md`) with only renames/logging.
- **NATIVE** = hand-written in this repo.
- **DECL-ONLY** = public declaration emitted, **no definition**; calling it is an
  unresolved external at link time.

**Verification performed:** `nm -gU` on `build/compat/cmake/libcplus_hardened_libc.a`
confirms only `cplus_exp10/f/l` (MUSL-COPY) and `cplus_roundeven/f/l` (BUILTIN) C23
math symbols are defined; the other 23 C23 math entry points are absent.
`__has_builtin` probes: host Apple Clang 16 has `__builtin_roundeven*` but **not**
`__builtin_exp10*`; `<fenv.h>` has no `FE_TIESAWAY`. Tests SKIP the gaps
(`test_math_c23.c`, `test_math_gaps.c`, `test_c23_small_gaps.c`,
`test_uchar_c23.c`, `test_math_c23_upstream.c`).

---

## 1. Severity register (finding → severity → layer)

| ID | Finding | Severity | Layer |
|---|---|---|---|
| F01 | Wide format strings pass `%n` through; no effective compile-time format checking | **HIGH** | Caller |
| F02 | Non-reentrant static-buffer time APIs (`gmtime`, `localtime`, `asctime`, `ctime`, `ecvt/fcvt/gcvt`) | MEDIUM | Runtime |
| F03 | `setlocale`/`localeconv` global mutable state; no synchronisation; static `lconv` | MEDIUM | Runtime |
| F04 | `cplus_exp10l` returns `0.0L` on any non-binary64 `long double` ABI | MEDIUM | Runtime |
| F05 | `abs`/`labs`/`llabs`/`imaxabs` negate the most-negative value (UB) | MEDIUM | Runtime |
| F06 | `atoi`/`atol` silently truncate overflow; no `ERANGE` surfaced | MEDIUM | Runtime |
| F07 | 23 C23 math APIs are DECL-ONLY (declared but link-failing) | MEDIUM | Compiler |
| F08 | `FE_TIESAWAY` absent (compiler macro, no host/upstream value) | MEDIUM | Compiler |
| F09 | `timespec_getres` absent (no pinned upstream source) | MEDIUM | Compiler |
| F10 | `mbrtoc8` / `c8rtomb` absent (no pinned musl source) | MEDIUM | Compiler |
| F11 | `format(wprintf/wscanf,…)` attributes ignored on Clang → diagnostics suppressed | MEDIUM | Compiler |
| F12 | `strtol`/`strtoul`/`strtoll`/`strtoull`/`strtod`/`atof`/`strtof`/`strtold`/`atoll` lack the NULL `nptr` guard that `atoi`/`strtoimax` etc. have | MEDIUM | Caller |
| F13 | NULL `tp`/`ts`/`req` and NULL `tm` to time/strftime/wcsftime not guarded | MEDIUM | Caller |
| F14 | Hostile `struct tm` values (out-of-range fields) delegated unchecked | MEDIUM | Caller |
| F15 | Unbounded wide-string operations (`wcscpy`, `wcscat`, `wmemcpy`, `wcsxfrm`, …) have no capacity parameter/guard | MEDIUM | Caller |
| F16 | Logger `write(2)` can clobber `errno` when enabled and stderr fails | LOW | Runtime |
| F17 | NaN payload / FP-exception / signed-zero semantics are host-specific, not guaranteed by this layer | LOW | Runtime |
| F18 | Latent duplicate `cplus_exp10` definition if a toolchain provides `__builtin_exp10` | LOW | Compiler |
| F19 | Huge width/precision wide formats permit CPU/memory DoS | LOW | Caller |
| F20 | `div`/`ldiv`/`lldiv`/`imaxdiv` divide-by-zero and `wcstok` state misuse are UB | LOW | Caller |
| F21 | Public header uses `long` instead of `time_t` for `cplus_time`/`cplus_gmtime`/… | LOW | Compiler |

**Counts: HIGH 1 · MEDIUM 14 · LOW 6 · total 21.**
(Per-function row counts are given at the end of each family section.)

---

## 2. MATH  (≈190 exported `cplus_*` symbols)

| Functions (all `cplus_` prefixed) | Signature shape | Backend | Checks present | Unsafe cases NOT handled | Sev | Evidence | Recommendation |
|---|---|---|---|---|---|---|---|
| `fabs sqrt sin cos tan pow exp log log10 floor ceil fmod` | `double f(double[,double])` | HOST | log only | NaN/Inf/rounding/errno/FP flags all host-specific | LOW (F17) | `math.c:4-15` | Document "host libm is source of truth"; add a host-libm version report to docs |
| `atan2 asin acos atan sinh cosh tanh log2 cbrt hypot round trunc fmin fmax copysign ldexp` | `double f(double[,double\|int])` | HOST | log only | domain/range delegated | LOW (F17) | `math2.c:7-22` | same |
| `frexp`, `modf` | `double f(double,int*/double*)` | HOST + guard | **NULL out-param → `errno=EINVAL`, return 0.0** | none material | OK | `math2.c:23-26` | keep; add NULL test if not present |
| `nan nextafter fdim remainder log1p expm1 asinh acosh atanh erf erfc tgamma lgamma` | `double f(double[,double]\|const char*)` | HOST | log only | `nan(tagp)` payload is host-defined; `lgamma` sign (`signgam`) not surfaced by wrapper | LOW (F17) | `math3.c:6-18` | document NaN-payload and `signgam` caveats |
| `fma logb ilogb exp2 nearbyint lrint llrint scalbn` | mixed | HOST | log only | FP flags/rounding delegated; `ilogb` special returns host-defined | LOW (F17) | `math4.c:10-22` | document |
| `remquo` | `double f(double,double,int*)` | HOST + guard | NULL `quo` → EINVAL/0.0 | caller-supplied `quo` aliasing | LOW | `math4.c:11-15` | keep |
| `rint nexttoward lround llround scalbln` | mixed | HOST | log only | FP flags delegated | LOW | `math_gaps.c:12-31` | document |
| `vscanf` | `int(const char*,va_list)` | HOST | NULL `fmt` → EINVAL/EOF | untrusted narrow format (`%n`) | MEDIUM (F01 analogue) | `math_gaps.c:32-36` | caller-contract docs + narrow format attribute check |
| `sinf cosf tanf asinf acosf atanf atan2f sinhf coshf tanhf asinhf acoshf atanhf expf exp2f expm1f logf log2f log10f log1pf powf sqrtf cbrtf hypotf floorf ceilf truncf roundf nearbyintf rintf lrintf llrintf lroundf llroundf fabsf fmodf remainderf fdimf fminf fmaxf fmaf copysignf ldexpf scalbnf scalblnf ilogbf logbf erff erfcf tgammaf lgammaf nextafterf nexttowardf nanf` (54) | `float f(...)` | HOST | log only | all FP semantics host-specific | LOW (F17) | `math_float.c:20-73` | document |
| `frexpf modff remquof` | float + out-param | HOST + guard | NULL out-param → EINVAL/0.0f | — | OK | `math_float.c:76-90` | keep |
| `sinl cosl tanl asinl acosl atanl atan2l sinhl coshl tanhl asinhl acoshl atanhl expl exp2l expm1l logl log2l log10l log1pl powl sqrtl cbrtl hypotl floorl ceill truncl roundl nearbyintl rintl lrintl llrintl lroundl llroundl erfl erfcl tgammal lgammal fabsl fmodl remainderl fdiml fminl fmaxl fmal copysignl ldexpl scalbnl scalblnl ilogbl logbl nextafterl nexttowardl nanl` (≈43) | `long double f(...)` | HOST | log only | **on macOS arm64 `long double == double`** → no extended precision (documented); FP flags host | LOW (F17) | `math_ldouble.c:1-112` | document ABI caveat |
| `frexpl modfl remquol` | long double + out-param | HOST + guard | NULL out-param → EINVAL/0.0L | — | OK | `math_ldouble.c:81-104` | keep |
| `roundeven roundevenf roundevenl` | `{double,float,long double} f(x)` | BUILTIN (`__builtin_roundeven*`) | log only; tie-to-even verified by host | rounding mode inherited from host FPSR | LOW | `math_c23.c:81-112`; `nm` shows 3 defined | keep; add `fesetround` variation test |
| `exp10 exp10f exp10l` | `f f(x)` | **MUSL-COPY** (`exp10.c/f/l`) | log; uses bundled `cplus_modf/exp2/pow` | **`exp10l` returns literal `0.0L` when `LDBL_MANT_DIG != 53` (x86 80-bit)** | MEDIUM (F04) | `math_c23_musl.c:15-60` | see Top-5 #4 |
| `nextup nextupf nextupl nextdown nextdownf nextdownl fadd faddf faddl fsub fsubf fsubl fmul fmulf fmull fdiv fdivf fdivl hypot3 frexp16 fma16 logb16 exp16` (23) | various | **DECL-ONLY** | none — no body emitted | calling any → unresolved external; tests SKIP | MEDIUM (F07) | `math_c23.c:122-359`, `math_c23_upstream.c:48-129`, `MATH_GAPS_EVIDENCE.md:12-90` | see §3 and Top-5 #3 |

**Math row coverage:** 12 + 16 + 2 + 13 + 8 + 1 + 5 + 1 + 54 + 3 + 43 + 3 + 3 + 3 + 23 ≈ **187 exported/declared symbols**.

---

## 3. C23 gap register — exact blockers

| Gap | Count | Status | Exact blocker | Evidence |
|---|---:|---|---|---|
| C23 math APIs (`nextup/f/l`, `nextdown/f/l` = 6; `fadd/fsub/fmul/fdiv` × `/f/l` = 12; 3-arg `hypot` = 1; `frexp16/fma16/logb16/exp16` = 4) | **23** | DECL-ONLY | Host `<math.h>` declares none; `__has_builtin` false for all but `roundeven`; vendored musl subset has **no math sources**; LLVM libc `llvmorg-20.1.1` wrappers are one-liners over `src/__support/FPUtil/*` C++ templates (FPBits, DyadicFloat, NormalFloat, FMA, expxf16 tables) — non-self-contained closure; no `cplus_*` equivalent. `/f` variants `faddf/fsubf/fmulf/fdivf`, `fmaf16`, `hypot3` are HTTP 404 at pin; LLVM `fadd`/`faddl` also have a **signature mismatch** (`float` vs C23 `double`/`long double`). | `C23_MATH_UPSTREAM.md:11-32,48-129`; `MATH_GAPS_EVIDENCE.md:12-90`; `math_c23_upstream.c:1-129` |
| `FE_TIESAWAY` | 1 | BLOCKED | Compiler-provided macro, absent on host `<fenv.h>` (only `FE_TONEAREST/UPWARD/DOWNWARD/TOWARDZERO`) **and** absent from LLVM libc pinned `fenv-macros.h`; AArch64 FPCR has no ties-away encoding → publishing a number = fabrication | `C23_SMALL_UPSTREAM.md:17-28,51-53,69-72,128-134`; probe this date: no `FE_TIESAWAY` |
| `timespec_getres` | 1 | BLOCKED | No pinned upstream source: musl `v1.2.6`/`master` has only `timespec_get.c`; LLVM libc `llvmorg-20.1.1`/`main` has no `timespec_getres.{cpp,h}` or CMake target. Deriving from `clock_getres` = new algorithm (forbidden) | `C23_SMALL_UPSTREAM.md:20,49-50,63-68,123-127` |
| `mbrtoc8` / `c8rtomb` | 2 | BLOCKED | No pinned musl revision ships them (404 at pin, `v1.2.6`, `master`; zero commit messages). Only an **unmerged 2023 mailing-list patch** exists, rejected for: not pinned; depends on musl-internal `mbstate_t` layout; `s[0]` read before NULL check (non-conforming); needs `weak_alias`, C11 `_Alignas`, `INT_MIN` → beyond "minimal integration"; known C-locale correctness defect | `UCHARC23_UPSTREAM.md:15-37,90-120`; `PROVENANCE.md:55-56` |
| `memset_explicit` (context) | — | CLOSED via LLVM libc copy | `c23_upstream_small.c` (not in CMake); duplicate-symbol risk with `c23_small_gaps.c` on glibc ≥ 2.38 | `C23_SMALL_UPSTREAM.md:19,74-115,165-176` |

**Note:** `COVERAGE_100_AUDIT.md:15` reports 27 MISSING but `:52` says 28 — an internal count inconsistency in that doc; the authoritative per-header inventory is `docs/libc/iso_c_inventory.json` (23 math + 4 named gaps + `memset_explicit` candidate).

---

## 4. TIME (26 exported symbols)

| Functions | Signature | Backend | Checks present | Unsafe cases NOT handled | Sev | Evidence | Recommendation |
|---|---|---|---|---|---|---|---|
| `time difftime clock mktime` | host signatures | HOST | `time(NULL)` valid per C | `mktime` normalises hostile `tm`; no validation | LOW/F14 | `time.c:5-10` | document |
| `gmtime localtime` | `struct tm *f(const time_t*)` | HOST | NULL `timep` not guarded | **static host buffer, non-reentrant, data race** | MEDIUM (F02) | `time.c:8-9` | prefer `cplus_gmtime_r/localtime_r`; add header warning |
| `strftime` | `size_t(char*,size_t,const char*,const struct tm*)` | HOST | none | NULL `s`/`fmt`/`tm` crash; hostile `tm` | MEDIUM (F13/F14) | `time.c:11` | add NULL guards + doc |
| `asctime ctime` | `char *f(const struct tm*\|const time_t*)` | HOST | NULL → EINVAL/NULL | **non-reentrant shared static buffer** (documented in file) | MEDIUM (F02) | `time_ascii.c:11-35` | header warning; prefer `_r` |
| `clock_gettime nanosleep timespec_get` | POSIX/C11 | HOST | **none on `tp`/`ts`/`req`** | NULL out-ptr → host UB; `timespec_getres` absent | MEDIUM (F13/F09) | `time_extra.c:9-20` | add NULL guards; see §3 |
| `gmtime_r localtime_r asctime_r ctime_r` | `_r` signature | HOST | **NULL on every required pointer → EINVAL/NULL** | caller must supply adequately sized `buf`/`result` | OK | `time_extra.c:21-40` | keep; mark PREFERRED in header |

**Row coverage:** 4 + 2 + 1 + 2 + 3 + 4 = **16 symbols** (all of `time*.c`).

---

## 5. WCHAR (56 exported symbols)

| Functions | Signature | Backend | Checks present | Unsafe cases NOT handled | Sev | Evidence | Recommendation |
|---|---|---|---|---|---|---|---|
| `wcslen wcscpy wcsncpy wcscmp wcsncmp wcscat wcsstr wmemcpy wmemmove wmemset wmemcmp` | standard | HOST | **none** | NULL/unterminated wide strings; no capacity on `wcscpy/wcscat/wmemcpy` | MEDIUM (F15) | `wchar.c:7-17` | caller must ensure capacity/NUL; point to hardened layer |
| `wprintf vwprintf fwprintf vfwprintf swprintf vswprintf wscanf vwscanf fwscanf vfwscanf swscanf vswscanf` | wide variadic | HOST (`v*`) | NULL fmt/stream/dest (when n>0) → EINVAL/-1 or EOF | **`%n` pass-through (F01)**; wide format attrs ignored (F11); huge width (F19); va_list not validated | **HIGH** (F01) | `wchar_format.c:21-48,89-185` | Top-5 #1 |
| `mbrtowc wcrtomb` | restartable | HOST | none — but NULL `pwc`/`ps`/`s` are **valid ISO C** and passed through | caller-supplied `mbstate_t` layout | LOW | `wchar_multibyte.c:28-38` | keep |
| `mbsrtowcs wcsrtombs` | restartable | HOST | NULL `src` → EINVAL/(size_t)-1 | invalid state | LOW | `wchar_multibyte.c:42-54` | keep |
| `fputwc fgetwc putwc getwc ungetwc putwchar getwchar` | wide stream I/O | HOST (`fputwc/fgetwc` function forms) | NULL stream → EINVAL/WEOF (except `putwchar/getwchar`, fixed streams) | invalid `wint_t` | LOW | `wchar_multibyte.c:58-102` | keep |
| `wcstol wcstoll wcstoul wcstoull wcstod wcstof wcstold` | numeric conversion | HOST | **NULL `nptr` → EINVAL + `*endptr=NULL` (good)**; `endptr` may be NULL; base/errno/ERANGE delegated | locale-dependent decimal point / whitespace (host) | LOW | `wchar_numeric.c:30-98` | keep (better than narrow `strtol`!) |
| `wcstok` | 3-arg tokenizer | HOST (`_wcstok` Win) | `delim`/`saveptr` required → EINVAL; `s==NULL` continuation valid | fabricated `saveptr` | LOW (F20) | `wchar_numeric.c:100-117` | document |
| `wcsftime` | wide time format | HOST | NULL `format`/`wcs` (n>0) → EINVAL | **NULL `tm` unguarded**; hostile `tm` | MEDIUM (F13) | `wchar_numeric.c:119-129` | add `tm` guard |
| `wcscoll wcsxfrm wcschr wcsrchr wcspbrk wcsspn wcscspn wmemchr fgetws fputws` | standard | HOST | **none** (explicitly documented as pass-through) | unbounded wide strings (F15); `wcscoll/wcsxfrm` **locale-sensitive** (F03) | MEDIUM | `wchar_gaps.c:13-78` | document locale + capacity contracts |
| `mbsinit` | `int(const mbstate_t*)` | HOST | none needed — `NULL` is valid ISO C | — | OK | `wchar_gaps.c:80-83` | keep |

**Row coverage:** 11 + 12 + 2 + 2 + 8 (putwchar/getwchar included above) + 7 + 1 + 1 + 10 + 1 = **55–56 symbols**.

---

## 6. WCTYPE + LOCALE (20 exported symbols)

| Functions | Signature | Backend | Checks present | Unsafe cases NOT handled | Sev | Evidence | Recommendation |
|---|---|---|---|---|---|---|---|
| `iswalpha iswdigit iswspace iswalnum iswupper iswlower iswxdigit iswprint iswgraph iswpunct iswcntrl iswblank` | `int(wint_t)` | HOST | none needed (value arg); normalised `!=0` | **locale-dependent classification** (F03) | LOW | `wctype.c:7-18` | document locale dependency |
| `towupper towlower` | `wint_t(wint_t)` | HOST | none | locale-dependent mapping; invalid `wint_t` | LOW | `wctype.c:19-20` | document |
| `wctype wctrans` | `t(const char*)` | HOST | NULL `property` → EINVAL/0 | unknown property → host 0 | LOW | `wctype.c:22-26`; `wctype_extra.c:12-16` | keep |
| `iswctype towctrans` | mixed | HOST | none | **fabricated descriptor** UB (file documents "must come from wctype/wctrans") | LOW | `wctype.c:28-31`; `wctype_extra.c:18-22` | document |
| `setlocale` | `char*(int,const char*)` | HOST | none | **global process state, not thread-safe; returned string may be overwritten** | MEDIUM (F03) | `locale.c:5`; `cplus_libc.h:161` (no note) | header warning + mutex/TLS snapshot |
| `localeconv` | `struct lconv*(void)` | HOST | none | **returns static struct; data race with setlocale/other calls** | MEDIUM (F03) | `locale.c:6` | copy-on-return or lock; header warning |

**Row coverage:** 12 + 2 + 2 + 2 + 1 + 1 = **20 symbols**.

---

## 7. CONVERSION / STDLIB-extra (≈45 relevant exported symbols)

| Functions | Signature | Backend | Checks present | Unsafe cases NOT handled | Sev | Evidence | Recommendation |
|---|---|---|---|---|---|---|---|
| `abs labs` | int/long | **NATIVE** | log | **`INT_MIN`/`LONG_MIN` negation UB** | MEDIUM (F05) | `stdlib_extra.c:9-10` | use unsigned trick or return `INT_MIN`; test min values |
| `atoi atol` | `int/long(const char*)` | NATIVE (strtol + fallback) | `s? s:"0"` avoids NULL crash | **overflow truncation, no ERANGE** | MEDIUM (F06) | `stdlib_extra.c:11-12` | document; prefer `cplus_strtol` + errno check |
| `strtol strtoul` | `unsigned/long(const char*,char**,int)` | HOST | **no NULL `nptr` guard** | NULL `nptr` crash; invalid `base` → host | MEDIUM (F12) | `stdlib_extra.c:13-14` | add NULL guard like `wcstol` |
| `qsort bsearch` | standard | HOST | none | NULL `compar`/bad `nmemb*size` → UB | LOW (F20) | `stdlib_extra.c:15-16` | document |
| `strtoll strtoull strtod atof` | standard | HOST | **no NULL `nptr` guard** | NULL crash; `strtod` rounding/locale host | MEDIUM (F12) | `stdlib_extra3.c:10-25` | add NULL guard |
| `strtof strtold imaxabs imaxdiv` | standard | HOST | none | NULL `n` crash; `imaxabs(INTMAX_MIN)` UB; `/0` UB | MEDIUM/F12, LOW/F05,F20 | `stdlib_extra4.c:13-16` | add guards; min-value handling |
| `strtoimax strtoumax` | standard | HOST | **NULL `nptr` → `*endptr=NULL`, EINVAL, 0** | — | OK | `stdlib_extra4.c:18-27` | keep |
| `mblen mbtowc wctomb` | multibyte | HOST | none | NULL `s` UB; **non-reentrant internal state** | MEDIUM/F13, LOW/F02 | `stdlib_extra4.c:28-39` | document; prefer `mbrtowc` with explicit `mbstate_t` |
| `mbstowcs wcstombs` | multibyte string | HOST | NULL source/dest(n>0) → EINVAL/(size_t)-1 | — | OK | `stdlib_extra4.c:40-49` | keep |
| `wcstoimax wcstoumax` | wide intmax | HOST | **NULL `nptr` guard present** | locale/base delegated | OK | `inttypes_wide.c:18-28` | keep |
| `div ldiv lldiv` | `(…div_t)(num,den)` | **MUSL-COPY** | log | **`den==0` → UB (SIGFPE)**; `INT_MIN/-1` UB | LOW (F20) | `musl_stdlib_extra.c:12-25` | document caller precondition |
| `llabs` | `long long f(long long)` | **MUSL-COPY** | log | `LLONG_MIN` negation UB | MEDIUM (F05) | `musl_stdlib_extra.c:27-30` | test min; consider bit-trick fix |
| `atoll` | `long long f(const char*)` | **MUSL-COPY** | log; correctly accumulates negative to avoid overflow | **NULL `s` → UB** (`isspace(*s)`) | MEDIUM (F12) | `musl_stdlib_extra.c:32-45` | add NULL guard |
| `ecvt fcvt gcvt` | legacy FP→string | HOST | none | **static/shared buffers (non-reentrant)**; NULL `decpt`/`sign`/`buf` | MEDIUM (F02/F13) | `stdlib_extra5.c:9-20` | document; prefer `snprintf` |
| `drand48 erand48 seed48 lrand48` | POSIX PRNG | HOST | none | shared PRNG state (not thread-safe) | LOW | `stdlib_extra5.c:21-36` | document |
| `mkstemp mkdtemp realpath qsort_r` | POSIX | HOST | none; `qsort_r` BSD/glibc ABI switch + `ENOSYS` fallback | caller buffers; `realpath` NULL `resolved` implementation-defined | LOW | `stdlib_extra6.c:15-40` | out of numeric scope; note ABI switch is correct |

**Row coverage:** 2 + 2 + 2 + 2 + 4 + 4 + 2 + 3 + 2 + 2 + 3 + 1 + 1 + 3 + 4 + 4 ≈ **41 conversion/stdlib-extra symbols** (plus non-numeric extras listed for completeness).

---

## 8. Top 5 prioritized issues — concrete fixes and tests

### #1 (HIGH, F01/F11) — Wide format strings: `%n` pass-through with no effective checking
**Evidence:** `wchar_format.c:21-40` explicitly refuses to detect/reject `%n`; `:56-67` emits
`__attribute__((format(wprintf,…)))` which Clang ignores (diagnostic suppressed with pragma),
so **zero static checking** occurs on this host.
**Fix:** (a) header-level "trusted-format-only" requirement; (b) add a hardened sibling
`cplus_swprintf_checked(...)` that scans the wide format for `%n` (respecting `%%`) and
returns `-1/EINVAL` before touching `va_list`; (c) for narrow wrappers use
`__attribute__((format(printf,…)))` which Clang *does* honour.
**Test:** `tests/libc/test_wchar_format.c` add: build `L"%n"` with a sentinel `int *`,
call the checked variant, assert `-1`, `errno==EINVAL`, sentinel unchanged; assert plain
`cplus_swprintf` still rejects NULL `fmt` and honours `%n` only in the documented
unchecked path; compile a `-Wformat=2` probe to prove narrow attributes fire.

### #2 (MEDIUM, F02/F03) — Non-reentrant time/locale state
**Evidence:** `time.c:8-9`, `time_ascii.c:26-35`, `locale.c:5-6`.
**Fix:** annotate public declarations in `cplus_libc.h` with "not thread-safe; use
`*_r`"; provide a mutex-guarded `cplus_setlocale_snapshot()` returning a copied
`struct lconv` and a thread-local locale snapshot for `wcstol/wcscoll` callers.
**Test:** two-thread stress (pthreads already wrapped) calling `cplus_gmtime` vs
`cplus_gmtime_r` on distinct `time_t`s; assert `_r` results are always distinct and
static-buffer results may interleave; TSan build if available.

### #3 (MEDIUM, F07–F10) — 23 C23 math APIs + 4 named C23 gaps are missing
**Fix (in priority order):** (1) upgrade/enable a toolchain exposing `__builtin_nextup*`,
`__builtin_fadd*` etc. (cheapest — the `math_c23.c` skeleton is already builtin-gated);
(2) for `nextup/nextdown` only, an explicitly *sanctioned* emulation via
`nextafter(x, ±Inf)` with documented special-case handling (currently only used as a
test oracle — policy change required); (3) vendor the LLVM libc `FPUtil` closure if the
project accepts C++ internals. For `timespec_getres`/`FE_TIESAWAY`/`mbrtoc8`/`c8rtomb`,
do **not** fabricate: keep SKIPs until a pinned source/compiler support appears.
**Test:** `test_math_gaps.c` already prints 23 SKIPs + 23 `EQUIV` exercises; convert each
SKIP to a real assertion when a backend lands, using the existing oracle column in
`MATH_GAPS_EVIDENCE.md:46-70`.

### #4 (MEDIUM, F04/F12/F13) — Wrong `exp10l` result and inconsistent NULL guards
**Evidence:** `math_c23_musl.c:51-59` returns `0.0L` for non-binary64 `long double`;
`wcstol` (wchar_numeric.c:30-38) guards NULL but `strtol` (stdlib_extra.c:13) does not;
`clock_gettime`/`timespec_get`/`nanosleep` (time_extra.c:9-20) have no guards.
**Fix:** replace the `exp10l` fallback with `powl(10.0L, x)` for non-64-bit `LDBL`
(or `#error` on unsupported ABI rather than returning a wrong value); add
`if (!p) { errno=EINVAL; return ...; }` guards to every `strto*`/`clock_gettime`/
`timespec_get`/`nanosleep` entry point.
**Test:** compile for an 80-bit-`long double` target and assert
`cplus_exp10l(0.5) == powl(10.0L,0.5)`; UBSan/ASan test calling all listed functions
with NULL and asserting defined return + `errno==EINVAL`.

### #5 (MEDIUM, F05/F06) — Integer/float conversion edge semantics
**Evidence:** `stdlib_extra.c:9-12`, `musl_stdlib_extra.c:27-45`.
**Fix:** implement `abs/labs/llabs/imaxabs` via unsigned magnitude (`u = x < 0 ? 0u-(unsigned)x : x`)
so `MIN` returns itself without UB; make `atoi/atol` call `cplus_strtol` and document
truncation on overflow (or add `*_errno` variants).
**Test:** boundary table `INT_MIN/LONG_MIN/LLONG_MIN/INTMAX_MIN`, `±0`, `MAX`; and
`atoi("999999999999999999999")` documenting the truncation result.

---

## 9. Layer separation summary

- **Runtime (library-visible behaviour):** F02 non-reentrancy, F03 locale global state,
  F04 `exp10l`, F05 min-value negation, F06 atoi overflow, F16 logger/`errno`,
  F17 host FP/NaN semantics. These are owned by this repo (even when delegated, the
  *choice* to delegate and the guard policy are ours).
- **Compiler/toolchain:** F07–F11 and F18/F21 — missing builtins/headers, ignored wide
  format attributes, latent duplicate symbol, header type mismatch. Fix requires a
  toolchain/upstream decision, not wrapper code alone.
- **Caller:** F01 (format trust), F12–F15, F19–F20 — raw pointers, hostile `struct tm`,
  unbounded wide buffers, huge widths, invalid descriptors. The wrappers document
  these as caller responsibilities; **a wrapper is not a capacity/format safety
  boundary** (README:34-35 states this explicitly).

## 10. Honest classification statement
Aside from `exp10/f/l` (musl copy), `roundeven/f/l` (compiler builtin), `abs/labs/atoi/atol`
(native), and the musl stdlib integer helpers, **every function in these families is a
thin HOST delegate**: there is no independent implementation of math, time, wide
conversion, classification, locale or `strto*` semantics in this repository. Domain/range,
`errno`, FP flags, locale sensitivity and NaN payloads are inherited from the host and are
therefore host-version dependent. The NULL out-parameter guards on `frexp*/modf*/remquo*`,
`mbsrtowcs/wcsrtombs`, `_r` time APIs, `wcsto*` and `strtoimax` are genuine, documented
hardening extensions; their **absence** on the narrow `strto*`, `clock_gettime`,
`timespec_get`, `nanosleep`, `strftime` and `wcsftime`-`tm` paths is the main consistency
gap. No live-game/runtime claim is made — findings are from source review, `nm` symbol
inspection and local builtin/preprocessor probes only.
