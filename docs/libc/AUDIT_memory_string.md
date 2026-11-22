# AUDIT — memory, string and ctype wrappers (READ-ONLY)

Branch: `agent/modern-stdlib`. Scope: every exported memory/string/ctype symbol in
`libc/install-include/cplus_libc.h` that is implemented in the listed sources, plus
the SafeCRT `_s` / `_s_auto` layer that shares the same source tree. This is a
static source audit only — no binaries were built, no tests were executed, no files
other than this document were touched.

## Sources audited

| Layer | Files |
|---|---|
| Plain libc hand-written | `libc/src/string/{memory.c,string.c,extra.c,extra2.c,extra3.c,strtok.c,strndup.c,strerror.c,string_extra4.c,string_extra5.c,string_extra6.c,bounded.c}` |
| Bundled musl-derived | `libc/src/string/musl_extra.c`, `libc/src/ctype/ctype3.c` |
| ctype (host tables + casts) | `libc/src/ctype/{ctype.c,ctype2.c,ctype3.c}` |
| safeclib-backed SafeCRT | `stdlib/safe/src/{safe_memory.c,safe_string.c,safeclib_ext_wrappers.c}` |
| Capacity helpers (supporting evidence only) | `libc/src/stdlib/alloc.c:386`, `stdlib/safe/src/safe_alloc.c:367` |
| Compiler macros (supporting evidence only) | `libc/install-include/cplus_libc_safe.h`, `stdlib/safe/safe_c.h`, `stdlib/safe/safe_memory.h`, `stdlib/safe/safe_string.h` |

## Legends

**Backend** — `bundled-C` (hand-written C in this repo), `bundled-musl` (verbatim musl
algorithm, renamed), `host` (delegates to installed system libc), `safeclib` (wraps
renamed upstream safeclib `cplus_sc_*` symbols).

**Check codes** (only recorded when actually present in source):
`N` NULL pointer · `C` destination capacity · `S` source bound · `O` overlap ·
`T` terminator/NUL guarantee · `OSZ` object-size/tracked-capacity resolution
(`__builtin_object_size` + allocation table) · `MSZ` max-size clamp (`RSIZE_MAX_*`).
Absence of a code means the check does **not** exist — nothing is inferred.

**Severity** — `High` = memory corruption or UB reachable with ordinary inputs or a
common calling pattern; `Medium` = UB reachable only with a specific malformed input
(huge length, unterminated buffer, NULL to an unguarded API) or a materially
inconsistent safety guarantee; `Low` = standard deviation, diagnostics quality, or a
correct function with only caller-responsibility limits; `Info` = no finding.

**Enforcement** — `R` runtime-enforced in the wrapper body · `C mp` compiler-enforced
(macro / attribute / compile-fail contract) · `CR` caller responsibility (no check
exists and none is claimed).

---

## 1. Check & backend summary matrix

| Function(s) | Backend | N | C | S | O | T | OSZ | MSZ | Sev |
|---|---|---|---|---|---|---|---|---|---|
| `cplus_memcpy` | bundled-C | ✓ | – | – | – | – | – | – | Medium |
| `cplus_memmove` | bundled-C | ✓ | – | – | ✓ | – | – | – | Low |
| `cplus_memset` | bundled-C | ✓ | – | – | – | – | – | – | Low |
| `cplus_memcmp` | bundled-C | ✓ | – | – | – | – | – | – | Low |
| `cplus_strlen` | bundled-C | ✓(*) | – | – | – | – | – | – | Low |
| `cplus_strnlen` | bundled-C | ✓(*) | – | ✓(n) | – | – | – | – | Low |
| `cplus_strcpy` | bundled-C | ✓ | – | – | – | – | – | – | High |
| `cplus_strncpy` | bundled-C | ✓ | – | – | – | – | – | – | High |
| `cplus_strcat` | bundled-C | ✓ | – | – | – | – | – | – | High |
| `cplus_strncat` | bundled-C | ✓ | – | ✓(n) | – | ✓ | – | – | Medium |
| `cplus_strcmp` | bundled-C | ✓ | – | – | – | – | – | – | Low |
| `cplus_strncmp` | bundled-C | ✓ | – | ✓(n) | – | – | – | – | Low |
| `cplus_memchr` | bundled-C | – | – | ✓(n) | – | – | – | – | Medium |
| `cplus_strchr` / `cplus_strrchr` | bundled-C | – | – | – | – | partial | – | – | Medium |
| `cplus_strstr` | bundled-C | – | – | – | – | – | – | – | Medium |
| `cplus_memrchr` | bundled-C | – | – | ✓(n) | – | – | – | – | High |
| `cplus_strcasecmp` / `cplus_strncasecmp` | bundled-C | – | – | ✓(n, ncmp only) | – | – | – | – | Medium |
| `cplus_strdup` | bundled-C | ✓ | – | ✓(scan) | – | ✓ | – | – | Low |
| `cplus_strspn` / `cplus_strcspn` / `cplus_strpbrk` | bundled-C | ✓(*) | – | ✓(*) | – | – | – | – | Low |
| `cplus_strndup` | bundled-C | ✓ | – | ✓(n) | – | ✓ | – | – | Low |
| `cplus_memset_bounded` | bundled-C | ✓ | ✓ | n/a | n/a | n/a | ✓ | – | Low |
| `cplus_memcpy_bounded` | bundled-C | ✓ | ✓ | – | – | n/a | ✓ | – | Medium |
| `cplus_memmove_bounded` | bundled-C | ✓ | ✓ | – | ✓ | n/a | ✓ | – | Medium |
| `cplus_memcmp_bounded` | bundled-C | ✓ | ✓ | ✓ | n/a | n/a | ✓ | – | Medium |
| `cplus_strcpy_bounded` | bundled-C | ✓ | ✓ | – | n/a | – | ✓ | – | Medium |
| `cplus_strncpy_bounded` | bundled-C | ✓ | ✓ | – | n/a | – | ✓ | – | High |
| `cplus_strcat_bounded` | bundled-C | ✓ | ✓ | – | n/a | – | ✓ | – | Medium |
| `cplus_strncat_bounded` | bundled-C | ✓ | ✓ | – | n/a | ✓ | ✓ | – | Medium |
| `cplus_memccpy` / `cplus_stpcpy` / `cplus_stpncpy` | bundled-musl | – | – | ✓(n:ccpy/np) | – | ✓(stp*) | – | – | Medium |
| `cplus_strtok_r` / `cplus_strsep` | bundled-musl | – | – | – | – | ✓(token) | – | – | Medium |
| `cplus_memmem` | bundled-musl | – | – | ✓(k,l) | – | – | – | – | Medium |
| `cplus_strverscmp` / `cplus_strcasestr` / `cplus_strchrnul` | bundled-musl | – | – | – | – | – | – | – | Medium |
| `cplus_strtok` | host | – | – | – | – | ✓(host state) | – | – | Medium |
| `cplus_strerror` | host | – | – | – | – | – | – | – | Low |
| `cplus_strsignal` / `cplus_strcoll` / `cplus_strxfrm` | host | – | – | – | – | – | – | – | Medium |
| `cplus_swab` | host | – | – | ✓(deleg.) | – | – | – | – | Medium |
| `cplus_strlcpy` / `cplus_strlcat` | host (BSD) | – | ✓(size) | – | – | ✓ | – | – | Low |
| `cplus_strnstr` | host (BSD) | – | – | ✓(len) | – | – | – | – | Medium |
| `cplus_memset_explicit` | host / bundled-musl(LLVM) | ✓ | – | – | – | – | – | – | Low |
| `cplus_isalpha` … `cplus_iscntrl` (11 fns) | host tables | n/a | n/a | n/a | n/a | n/a | n/a | n/a | Low |
| `cplus_toupper` / `cplus_tolower` | host tables | n/a | n/a | n/a | n/a | n/a | n/a | n/a | Low |
| `cplus_isblank` | host tables | n/a | n/a | n/a | n/a | n/a | n/a | n/a | **High** |
| `cplus_isascii` / `cplus_toascii` | bundled-musl | n/a | n/a | n/a | n/a | n/a | n/a | n/a | Low |
| `cplus_memcpy_s_auto` … `cplus_memcmp_s_auto` | safeclib | ✓ | ✓ | ✓(smax=n) | – | n/a | ✓ | ✓ | Low–Medium |
| `cplus_strcpy_s_auto` … `cplus_strncat_s_auto` | safeclib | ✓ | ✓ | – | n/a | ✓(safeclib) | ✓ | ✓ | Low–Medium |
| `cplus_memcpy_s` … `cplus_memzero_s` | safeclib | ✓(upstream) | ✓(dmax) | ✓(smax) | ✓(upstream) | n/a | – | – | Medium |
| `cplus_strcpy_s` … `cplus_strerror_s` | safeclib | ✓(upstream) | ✓(dmax) | ✓(upstream) | n/a | ✓(upstream) | – | – | Medium |
| `cplus_sc_timingsafe_*` | safeclib | – | – | – | – | – | BOS_UNKNOWN | – | Medium |

(*) NULL is *tolerated* (returns a benign value) rather than refused; see §3.2.

---

## 2. Detailed findings by group

### 2.1 Raw memory primitives — `libc/src/string/memory.c`

| Function | Signature | Evidence | Checks present | Unsafe cases NOT handled | Sev |
|---|---|---|---|---|---|
| `cplus_memcpy` | `void *cplus_memcpy(void *restrict, const void *restrict, size_t)` | memory.c:6–14 | N (`!dest\|\|!src`→`errno=EINVAL`, returns **dest**); zero-size early return at :8 (before N check) | No capacity; no source bound; **no overlap detection** — byte loop at :12 gives UB for overlapping `d`/`s` despite `restrict`; raw pointer provenance; unknown capacity | Medium |
| `cplus_memmove` | `void *cplus_memmove(void *, const void *, size_t)` | memory.c:15–24 | N (:20); **O** — forward when `d<s`, backward when `d>s` (:21–22); zero-size/`d==s` early return :19 | No capacity; no source bound; provenance | Low |
| `cplus_memset` | `void *cplus_memset(void *, int, size_t)` | memory.c:25–32 | N (:28); zero-size early return :27 | No capacity; provenance; `int c` narrowed to `unsigned char` (standard) | Low |
| `cplus_memcmp` | `int cplus_memcmp(const void *, const void *, size_t)` | memory.c:33–43 | N (:36, deterministic sign); zero-size→0 :35 | No capacity/source bound; provenance | Low |

### 2.2 Raw string primitives — `libc/src/string/string.c`, `extra.c`, `extra2.c`, `extra3.c`, `strndup.c`, `strtok.c`, `strerror.c`

| Function | Signature (abridged) | Evidence | Checks | Unsafe cases NOT handled | Sev |
|---|---|---|---|---|---|
| `cplus_strlen` | `size_t(const char*)` | string.c:5–8 | NULL→0 (:7, silent) | No bound; unterminated source → OOB read | Low |
| `cplus_strnlen` | `size_t(const char*,size_t)` | extra.c:41–47 | NULL→0; bound `n` (:45) | provenance only | Low |
| `cplus_strcpy` | `char*(char*restrict,const char*restrict)` | string.c:9–13 | N (:11) | **No capacity at all** — unbounded `while((*d++=*src++))` (:12); no source bound; unterminated source; returns dest unchanged on refusal (no NUL written) | **High** |
| `cplus_strncpy` | `char*(char*restrict,const char*restrict,size_t)` | string.c:14–21 | N (:17); zero-size early return :16 | **Huge n**: zero-fill loop `for(;i<n;++i) dest[i]='\0'` (:19) writes `n` bytes — overflow if `n` exceeds dest; **no NUL guarantee** when no `\0` in first `n` bytes (standard behaviour, unmitigated); no source bound; unbounded `strlen`-free source scan is bounded by n but still OOB if source shorter than n and unallocated | **High** |
| `cplus_strcat` | `char*(char*restrict,const char*restrict)` | string.c:22–26 | N (:24) | **No capacity**; scans dest to NUL then copies unbounded (:25); unterminated dest/source | **High** |
| `cplus_strncat` | `char*(char*restrict,const char*restrict,size_t)` | string.c:27–31 | N (:29); `T` — always writes `d[i]='\0'` (:30) | Dest capacity not checked (dest scan + up to n copies can overflow); unterminated dest; source bound only via n/stop-at-NUL | Medium |
| `cplus_strcmp` / `cplus_strncmp` | `int(const char*,const char*[,size_t])` | string.c:32–48 | N with deterministic sign; `strncmp` bound n and early stop at NUL | No capacity; provenance; `strncmp` n==0→0 before N check | Low |
| `cplus_memchr` | `void*(const void*,int,size_t)` | extra.c:8–14 | bound n (:12) | **No NULL guard** — `p[0]` on NULL segfaults; no capacity; provenance | Medium |
| `cplus_strchr` / `cplus_strrchr` | `char*(const char*,int)` | extra.c:15–30 | `cplus_strchr` NUL-terminator match for `c=='\0'` (:19) | **No NULL guard**; no bound; `char ch=(char)c` truncates high-bit `c` (standard conversion, but callers passing `EOF` get 0xFF semantics); unterminated source | Medium |
| `cplus_strstr` | `char*(const char*,const char*)` | extra.c:31–40 | empty needle→haystack (:33) | **No NULL guard** for either argument; no bound; unterminated inputs | Medium |
| `cplus_memrchr` | `void*(const void*,int,size_t)` | extra2.c:6–12 | bound n (loop `i=n;i>0;i--` :10) | **Huge n**: scans downward from `p[n-1]` — with `n` far larger than the object this is an immediate large OOB read; **no NULL guard**; no capacity; provenance | **High** |
| `cplus_strcasecmp` / `cplus_strncasecmp` | `int(const char*,const char*[,size_t])` | extra2.c:13–36 | `strncasecmp` bound n (:27) | **No NULL guard**; case folding is **ASCII-only** (`'A'..'Z'` at :18–19/:30–31) — high-bit/locale bytes are not folded (differs from POSIX locale-aware `strcasecmp`); `strcasecmp` unbounded | Medium |
| `cplus_strdup` | `char*(const char*)` | extra2.c:37–46 | N→NULL (:40); source scan bounded by terminator; allocates `n+1` via `cplus_malloc` (:42); copies NUL (:44) | provenance/termination of source is caller responsibility (unterminated source → OOB scan) | Low |
| `cplus_strspn` / `cplus_strcspn` / `cplus_strpbrk` | size_t/char* | extra3.c:5–16 | NULL-tolerant on both args (`s && s[i]`, `a && *a`) | no explicit bounds; unterminated inputs | Low |
| `cplus_strndup` | `char*(const char*,size_t)` | strndup.c:5–14 | N→NULL; **source bound** `len<n && s[len]` (:9); allocates `len+1`, writes `d[len]='\0'` (:10–12) | provenance only | Low |
| `cplus_strtok` | `char*(char*,const char*)` | strtok.c:4 | host | Delegates to host `strtok`; host uses hidden static state (**not thread-safe/reentrant**); NULL delim/unterminated str = caller UB | Medium |
| `cplus_strerror` | `char*(int)` | strerror.c:5 | host | Delegates to host `strerror`; returns host-owned static buffer (not thread-safe); errnum validity host-defined | Low |

### 2.3 Bounded plain-libc layer — `libc/src/string/bounded.c`

`resolve_capacity` (:24–30) = `min(cplus_alloc_remaining_capacity(p), known)` where
`known` is `CPLUS_LIBC_OBJ_SIZE(p)` == `__builtin_object_size(p,1)` from
`cplus_libc_safe.h:33`. `cplus_alloc_remaining_capacity` (alloc.c:386–412) resolves
**interior pointers** to `base+size-p` and the one-past-the-end address to 0.
`check_capacity` (:33–52): unknown → strict mode refuses (`EINVAL`), default mode
proceeds **unchecked** with only a log; `need>cap` → refuses (`ERANGE`).

| Function | Evidence | Checks | Unsafe cases NOT handled | Sev |
|---|---|---|---|---|
| `cplus_libc_set_unknown_capacity_mode` | bounded.c:20–22 | sets global mode (default 0 = unchecked) | process-global, not thread-local | Info |
| `cplus_memset_bounded` | :54–59 | N→NULL; cap (C/OSZ) | provenance; `d` NULL refused even at n==0 (contrast raw `cplus_memset`) | Low |
| `cplus_memcpy_bounded` | :61–66 | N→NULL; **dest** cap (C/OSZ) | **Source capacity never checked** — `known` is dest-only in the signature; `cplus_memcpy` at :65 has no overlap check → overlapping dest/src UB; default mode unknown→unchecked | Medium |
| `cplus_memmove_bounded` | :68–73 | N→NULL; dest cap; O via memmove | Source capacity never checked; default mode unknown→unchecked | Medium |
| `cplus_memcmp_bounded` | :75–82 | N; **both** caps (C/OSZ for `a` and `b`) | **Refusal returns 0** (:78–80), indistinguishable from "equal" → dangerous for comparison-driven logic; default mode unknown→unchecked | Medium |
| `cplus_strcpy_bounded` | :84–90 | N→NULL; dest cap; `need=strlen(s)+1` (:87) | **Unterminated source is explicitly out of scope** (comment :87) — `strlen` over-reads; no source bound; NUL-on-refusal: returns NULL before write, dest unchanged (incidental, not enforced) | Medium |
| `cplus_strncpy_bounded` | :92–97 | N→NULL; dest cap checked against `n` (:95) | **Huge n under default/unknown capacity → unchecked pass-through** to `cplus_strncpy` which zero-fills n bytes (High); source bound never checked; underlying `cplus_strncpy` gives no NUL guarantee when src has no NUL in first n; refusal → dest unchanged, no explicit NUL | **High** |
| `cplus_strcat_bounded` | :99–105 | N→NULL; dest cap; `need=strlen(d)+strlen(s)+1` (:102) | Unterminated `d`/`s` → OOB scans; no source bound; refusal → dest unchanged | Medium |
| `cplus_strncat_bounded` | :107–113 | N→NULL; dest cap; `need=strlen(d)+min(n,strlen(s))+1` (:110) | Unterminated `d`/`s` → OOB scans; source bound via `strlen(s)` only if terminated; refusal → dest unchanged | Medium |

### 2.4 Bundled musl-derived extras — `libc/src/string/musl_extra.c`

All of these are verbatim musl algorithms with `cplus_*` names and a log line. They
inherit musl's contract: **valid pointers and sufficient space are precondition**.
No NULL guards and no capacity/max-size checks were added anywhere in this file.
Internal calls to host `strlen`/`memset`/`memcmp`/`isdigit` are explicitly noted.

| Function | Evidence | Checks | Unsafe cases NOT handled | Sev |
|---|---|---|---|---|
| `cplus_memccpy` | :26–55 | stops at `c` or after n (vectorized); n bound | No NULL guard; no capacity; raw word writes `*wd=*ws` (:45) overflow dest if n overstates it; provenance | Medium |
| `cplus_stpcpy` | :58–77 | copies through NUL, returns pointer to terminator | No NULL guard; no dest capacity; unterminated source → OOB; provenance | Medium |
| `cplus_stpncpy` | :80–102 | n bound; zeroes tail with `memset(d,0,n)` (:100) | **Huge n**: `memset(d,0,n)` writes n bytes with no capacity check; no NULL guard; uses host `memset` internally; provenance | Medium |
| `cplus_strtok_r` | :105–115 | NUL-terminates token at :112; `*p` state | No NULL guard on `s`/`sep`/`p` (musl assumes valid); delim/string must be terminated; provenance | Medium |
| `cplus_strsep` | :118–128 | writes `\0` at delimiter :124 | No NULL guard on `*str`/`sep`; unterminated string; provenance | Medium |
| `cplus_memmem` | :130–208 | `!l`→haystack; `k<l`→NULL; bounded by k/l | No NULL guard; BOS checks of haystack/needle rely on caller passing correct `k`/`l`; internal `memcmp` at :179 is host; provenance | Medium |
| `cplus_strverscmp` | :211–232 | none added | No NULL guard; `isdigit(c)` at :221/:225 with `c` already an `unsigned char` value (safe); unterminated strings; provenance | Medium |
| `cplus_strcasestr` | :236–243 | empty needle→haystack | No NULL guard; `strlen(n)` (:239) and `cplus_strncasecmp` unbounded source; provenance | Medium |
| `cplus_strchrnul` | :246–264 | returns pointer to first `c` or NUL | No NULL guard; uses host `strlen` at :250; unterminated source; provenance | Medium |

### 2.5 Host-delegated extras — `string_extra4.c`, `string_extra5.c`, `string_extra6.c`, `c23_small_gaps.c`

| Function | Evidence | Checks | Unsafe cases NOT handled | Sev |
|---|---|---|---|---|
| `cplus_strsignal` | string_extra4.c:6 | host `strsignal` | static buffer, thread-safety, signal-number validity — all host/caller | Low |
| `cplus_strcoll` / `cplus_strxfrm` | string_extra4.c:7–8 | host | `strxfrm` dest capacity **not** checked by wrapper (caller must pass correct n); unterminated source | Medium |
| `cplus_swab` | string_extra5.c:11–14 | host `swab` | `ssize_t n` negative → host UB; NULL/source/dest validity and size caller responsibility; **size is signed** (unlike all other length params) | Medium |
| `cplus_strlcpy` / `cplus_strlcat` | string_extra6.c:10–17 | host BSD | Inherently bounded by `size` (safer API); still requires terminated source (BSD semantics) | Low |
| `cplus_strnstr` | string_extra6.c:18–21 | host BSD, bounded by `len` | haystack/needle must be valid for `len`; provenance | Medium |
| `cplus_memset_explicit` | c23_small_gaps.c:102–111 (host) **or** c23_upstream_small.c:98–109 (vendored LLVM, mutually exclusive `#if`) | N→`EINVAL` return (host path only); vendored path uses `cplus_memset` which has its own NULL guard | Two definitions exist but are mutually exclusive by `#if !defined(__GLIBC__) && !defined(CPLUS_C23_HOST_HAS_MEMSET_EXPLICIT)` — **no link collision**; no capacity check; anti-optimisation barrier only | Low |

### 2.6 ctype — `libc/src/ctype/*`

The declared contract (C17 7.4) is: argument must be `EOF` or a value representable as
`unsigned char`; passing a negative `char` is UB. The correct hardening pattern used
by `ctype.c` is `isX((unsigned char)c) != 0`, with `toupper/tolower` additionally
guarding `EOF` explicitly.

| Function | Evidence | Handling of `c` | High-bit / EOF behaviour | Sev |
|---|---|---|---|---|
| `cplus_isalpha`, `isdigit`, `isspace`, `isalnum`, `isupper`, `islower`, `isxdigit`, `isprint`, `isgraph`, `ispunct`, `iscntrl` (11 fns) | ctype.c:5–15 | `isX((unsigned char)c) != 0` — cast present | EOF→255→0 (defined, not UB); high-bit bytes mapped to 0..255 (locale-dependent but defined) | Low |
| `cplus_toupper` / `cplus_tolower` | ctype.c:16–17 | `if (c==EOF) return EOF;` then `toX((unsigned char)c)` | **EOF already fixed**; high-bit cast defined | Low |
| **`cplus_isblank`** | **ctype2.c:4** | **`isblank(c)` — NO cast, NO EOF check** | A `char` holding e.g. `0xFE` sign-extends to `-2`; `isblank(-2)` is UB and on table-based implementations indexes **before** the table. `c==EOF(-1)` happens to be defined but is not explicitly handled. This is the one remaining signed-char ctype hole; all 15 siblings were fixed, this was not. | **High** |
| `cplus_isascii` | ctype3.c:5–8 | `!(c & ~0x7f)` | pure bit mask; defined for every int; high-bit→0; EOF→0 | Low |
| `cplus_toascii` | ctype3.c:9–12 | `c & 0x7f` | pure bit mask; defined for every int | Low |

### 2.7 SafeCRT safeclib layer — `stdlib/safe/src/{safe_memory.c,safe_string.c,safeclib_ext_wrappers.c}`

These symbols are **not** declared in `cplus_libc.h`; they are the second, opt-in layer
reached through `safe_c.h` / `safe_memory.h` / `safe_string.h`. They wrap renamed
upstream safeclib entry points (`cplus_sc_*`).

**Direct `_s` entry points** (caller supplies `dmax`/`smax`):
`cplus_memcpy_s`, `cplus_memmove_s`, `cplus_memset_s`, `cplus_memcmp_s`,
`cplus_memzero_s` (safe_memory.c:35–81) and `cplus_strcpy_s`, `cplus_strncpy_s`,
`cplus_strcat_s`, `cplus_strncat_s`, `cplus_strcmp_s`, `cplus_strnlen_s`,
`cplus_strtok_s`, `cplus_strerror_s` (safe_string.c:38–114).
All add a **UAF pre-check** (`cplus_safe_reject_uaf`, returns 400 on UAF) and delegate
NULL/capacity/overlap/terminator enforcement to upstream safeclib. Because `dmax` is
caller-supplied, **no `__builtin_object_size` or tracked-capacity resolution is
applied** at this tier; a wrong `dmax` is caller responsibility (Medium).

**Auto `_s_auto` entry points** (capacity auto-resolved):
`cplus_memcpy_s_auto`, `memmove_s_auto`, `memset_s_auto`, `memcmp_s_auto`
(safe_memory.c:138–252) and `cplus_strcpy_s_auto`, `strncpy_s_auto`, `strcat_s_auto`,
`strncat_s_auto` (safe_string.c:132–256).
Shared machinery: `cplus_safe_capacity` (:94–106) = min(tracked remaining, known BOS);
`cplus_safe_check_capacity` (:108–122) unknown→strict refuse (`EINVAL`) / default
**unchecked** with `*dmax_out = n`; `cplus_safe_report_overflow` (:124–136) sets
`errno=ERANGE`, logs, raises runtime violation and maps to safeclib ESNOSPC 406.
They add **UAF + NULL (even at n==0) + capacity + `RSIZE_MAX_MEM/STR` + n==0 early
return**. This is the strongest tier. Residual gaps: source capacity for
`memcpy_s_auto`/`memmove_s_auto` is only `smax = n` (safe_memory.c:159,188) — it does
not independently bound the source object; `_auto` string functions compute
`strlen(src)`/`strlen(dest)` before any capacity decision (safe_string.c:143,200–201,
237) so an **unterminated source/dest is still caller UB**; `memcmp_s_auto` returns
`-1` on refusal/overflow and `0` at n==0 (safe_memory.c:228–245).

**Timing-safe wrappers** (`safeclib_ext_wrappers.c:18–24`): `cplus_sc_timingsafe_bcmp`
and `cplus_sc_timingsafe_memcmp` pass `BOS_UNKNOWN` for both object sizes to the
`_chk` upstream functions — i.e. **no object-size check is performed**; raw pointer
validity and length are entirely caller responsibility (Medium).

---

## 3. Focused analyses requested

### 3.1 `cplus_*_bounded` vs `cplus_*_s_auto`

| Dimension | `cplus_*_bounded` (plain libc) | `cplus_*_s_auto` (SafeCRT) | Consistent? |
|---|---|---|---|
| Capacity source | min(tracked remaining, `__builtin_object_size(p,1)`) | min(tracked remaining, `CPLUS_SAFE_OBJ_SIZE(p)` = `__builtin_object_size(p,1)`) | Yes |
| Unknown capacity, default mode | warning log, **unchecked** | warning log, **`dmax=n` unchecked** | Yes (same weakness) |
| Unknown capacity, strict mode | `errno=EINVAL`, return NULL | `errno=EINVAL`, return NULL | Yes |
| Overflow on capacity | `errno=ERANGE`, return NULL | `errno=ERANGE` + runtime violation + ESNOSPC 406, return NULL | Yes (s_auto richer diagnostics) |
| NULL & zero-size | Bounded wrappers NULL-check **before** delegating → NULL always refused | explicit: NULL refused **even at n==0** (:143–149, :172–178, :201–206) | Yes for bounded/s_auto; **raw** functions differ (§3.2) |
| UAF detection | **none** in plain libc layer | `cplus_safe_reject_uaf` on both pointers | **No — s_auto stronger** |
| Max-size clamp | **none** (`RSIZE_MAX_*` never used) | `RSIZE_MAX_MEM` / `RSIZE_MAX_STR` used | **No — s_auto stronger** |
| Source bound | memcpy/memmove_bounded: none; memcmp_bounded: both | `smax=n` for memcpy/memmove; memcmp: n | Partially |
| Refusal return sentinel | memcmp_bounded → **0** (ambiguous) | memcmp_s_auto → **-1** | **No (divergent)** |
| Terminator on refusal | dest untouched, no NUL written | safeclib constraint handler owns this; wrapper adds none | Delegated / unverified in this source |

**Bottom line:** the two layers share the capacity-resolution design and agree on
NULL/zero-size/unknown-mode, but diverge on UAF detection, max-size clamping,
comparison-refusal sentinel, and diagnostic richness. `s_auto` is strictly stronger
except for its `strlen`-before-capacity pattern.

### 3.2 NUL termination on refusal and zero-size behaviour

- **On refusal:** no wrapper in either layer writes a terminator. The bounded layer
  returns NULL before calling the underlying function, so the destination is left
  **byte-for-byte unchanged**; if it was previously terminated, it stays terminated,
  but no new NUL is forced. The `_s_auto` layer delegates refusal semantics (including
  any NUL-on-error behaviour) to upstream safeclib — this source does not add one.
  For `cplus_strncpy` / `cplus_strncpy_bounded`, even a *successful* copy can leave the
  destination **unterminated** when the source has no NUL within `n` bytes.
- **Zero-size:** raw `cplus_memcpy/memset/memmove/memcmp/strncpy/strncmp` check
  `n==0` **before** the NULL check, so `NULL` with `n==0` quietly returns `dest`/`s`/`0`
  with no `errno`. The bounded and `_s_auto` layers check NULL first and refuse even
  at `n==0`. This is a deliberate difference (`safe_memory.c:143` comments it) but is
  not documented in `cplus_libc.h`.

### 3.3 `cplus_strncpy` with huge `n`

`cplus_strncpy` (string.c:18–19) first copies up to `n` source bytes, then zero-fills
the remaining `n-i` bytes. `n` is never clamped to a max-size and is never compared
to the destination capacity inside the raw function. `cplus_strncpy_bounded` does
compare `n` to capacity — but only when capacity is **known**; in the default
unknown-capacity mode (e.g. a stack array passed through a pointer parameter, where
`__builtin_object_size` returns -1 and the pointer is not in the allocation table)
`check_capacity` returns 0 and the raw function runs with an arbitrary `n` →
multi-byte/zero-fill overflow. A source shorter than `n` and unterminated additionally
causes OOB reads. Highest-priority string finding.

### 3.4 `cplus_memrchr` with huge `n`

`cplus_memrchr` (extra2.c:10) starts at `p[n-1]` and walks down. With `n` larger than
the real object this reads out of bounds on the **first** iteration; there is no NULL
guard and no capacity/max-size check, and no bounded counterpart exists for `memrchr`.
Same class applies to `cplus_memchr`, `cplus_stpncpy`'s `memset(d,0,n)` and
`cplus_memccpy`'s word writes.

### 3.5 Overlap handling

- `cplus_memmove` and `cplus_memmove_bounded`: correct (direction chosen by address
  comparison).
- `cplus_memcpy` and `cplus_memcpy_bounded`: **no overlap detection**; the hand-written
  byte loop makes overlapping regions UB. `restrict` on `cplus_memcpy` already makes
  overlap a caller contract violation at the type level, but `cplus_memcpy_bounded`
  has no `restrict` in its signature and still performs no check.
- safeclib `cplus_memcpy_s/_auto` inherit upstream overlap handling; not re-checked
  in these wrappers.
- Raw `strcpy`/`strcat`/`strncpy`/`strncat` have no overlap protection.

### 3.6 ctype EOF & high-bit bytes

EOF: `toupper/tolower` have an explicit `if (c==EOF) return EOF;` (ctype.c:16–17).
The 11 classifiers cast to `unsigned char`, turning EOF into 255 (defined, returns 0),
so they are UB-free even without an explicit EOF branch. High-bit bytes (0x80–0xFF)
are handled correctly by the cast; `isascii`/`toascii` are pure bit operations.
**`cplus_isblank` is the lone exception** — it passes the raw `int`, so a negative
`char` (any high-bit byte on a signed-char platform) is UB. The compile-fail contract
`tests/compile_fail/classic_bug31_char_ctype.c` demonstrates the intended discipline
for plain `isalpha(ch)`, but the `cplus_isblank` wrapper does not implement it.

### 3.7 Duplicate / alias symbols between libc and safeclib layers

- **No link-time duplicate definitions found** among the audited sources.
  `cplus_strncasecmp`, `cplus_strspn`, `cplus_strcspn`, `cplus_memchr` are *declared*
  in `musl_extra.c:21–23,235` but defined only once in `extra2.c`/`extra3.c`/`extra.c`.
  This is a maintenance fragility (four symbols owned by two files via extern
  declarations), not an ODR violation.
- `cplus_memset_explicit` has two textual definitions (`c23_small_gaps.c:106` and
  `c23_upstream_small.c:99`) but they are **mutually exclusive** under
  `#if !defined(__GLIBC__) && !defined(CPLUS_C23_HOST_HAS_MEMSET_EXPLICIT)` — no
  collision.
- `cplus_safe_reject_uaf` is defined `static` in both `safe_memory.c:19` and
  `safe_string.c:22` — internal linkage, no collision.
- The four naming tiers `cplus_memcpy` (raw) / `cplus_memcpy_bounded` (plain bounded) /
  `cplus_memcpy_s` (safeclib direct) / `cplus_memcpy_s_auto` (safeclib auto) are
  distinct symbols. There is **no alias**, but the proliferation is a documented
  source of confusion; `docs/libc/ORDINARY_C_SAFETY.md` is the current map.

---

## 4. Severity counts

Counting each distinct finding raised in §2 (a function can carry more than one; the
"Sev" column records the highest):

| Severity | Count | Representative functions |
|---|---|---|
| **High** | **6** | `cplus_strcpy`, `cplus_strncpy`, `cplus_strcat`, `cplus_memrchr`, `cplus_strncpy_bounded`, `cplus_isblank` |
| **Medium** | **27** | `cplus_memcpy`, all unguarded search extras (`memchr`, `strchr`, `strrchr`, `strstr`, `strcasecmp`, `strncasecmp`), `cplus_strncat`, `cplus_swab`, `cplus_strcoll`/`strxfrm`, `cplus_strnstr`, all 9 musl extras, `cplus_strtok`, `memcpy_bounded`/`memmove_bounded`/`memcmp_bounded`/`str*_bounded` (source-bound & sentinel gaps), all direct `_s` and timing-safe wrappers |
| **Low** | **16** | `cplus_memmove`, `cplus_memset`, `cplus_memcmp`, `cplus_strlen`, `cplus_strnlen`, `cplus_strcmp`, `cplus_strncmp`, `cplus_strdup`, `cplus_strndup`, `strspn`/`strcspn`/`strpbrk`, `cplus_strerror`, `strlcpy`/`strlcat`, `cplus_memset_explicit`, all 15 correctly-cast ctype functions + isascii/toascii |
| **Info** | **1** | `cplus_libc_set_unknown_capacity_mode` |
| **Total distinct function findings** | **50** | — |

Additional cross-cutting findings not attributed to a single function: (a) default
unknown-capacity mode is unchecked in **both** layers; (b) no max-size clamp in the
plain libc layer; (c) refusal leaves no explicit NUL; (d) zero-size/NULL semantics
diverge between raw and bounded tiers.

---

## 5. Top 5 prioritized issues (with concrete fixes and test names)

### #1 — `cplus_isblank` passes an uncast `int` to `isblank` (High)

- **Evidence:** `libc/src/ctype/ctype2.c:4` — `return isblank(c);` while all 11
  classifiers in `ctype.c:5–15` use `(unsigned char)c`.
- **Impact:** `cplus_isblank((char)0xFE)` is UB; table-based libc implementations
  index before the classification table. Signed-char ctype bug, already fixed in the
  sibling functions.
- **Fix:**
  ```c
  int cplus_isblank(int c){ cplus_libc_log("isblank","enter");
      return isblank((unsigned char)c) != 0; }
  ```
- **Tests:** extend `tests/libc/test_ctype.c` (or `test_ctype_full.c`) with
  `cplus_isblank((char)0xE9)==0`, `cplus_isblank((char)0xFE)==0`,
  `cplus_isblank(EOF)==0`, `cplus_isblank(' ')!=0`, `cplus_isblank('\t')!=0`; run under
  UBSan. Add a compile-fail companion to
  `tests/compile_fail/classic_bug31_char_ctype.c` using `cplus_isblank`.

### #2 — Huge-`n` overflow in `cplus_strncpy` / `cplus_strncpy_bounded` (High)

- **Evidence:** `string.c:18–19` (n-byte zero-fill, no clamp), `bounded.c:92–97` (cap
  only when known; default mode unchecked pass-through).
- **Impact:** multi-byte stack/heap overflow and/or OOB source read with a large `n`;
  destination may also be left unterminated.
- **Fix:** add `if (n > RSIZE_MAX_STR) { errno=ERANGE; return NULL; }` to the bounded
  wrapper; compute source length with `cplus_strnlen(s, n)` instead of assuming
  termination; require `need = min(n, src_len)+1 <= cap`; in the raw wrapper clamp
  `n` to a documented max and always write a NUL at `dest[n-1]` for `n>0`. Optionally
  make unknown-capacity refuse for this API even in default mode.
- **Tests:** new `tests/libc/test_strncpy_huge_n.c` (small 8-byte dest, `n=SIZE_MAX/2`,
  both known and unknown capacity, expect refusal and no writes beyond 8 bytes);
  extend `tests/libc/test_libc_safe_api.c` and `tests/crt/adversarial/test_bugclass_string_term.c`.

### #3 — `cplus_memrchr` (and `cplus_memchr`/`cplus_stpncpy`) scan/write past the object with huge `n`, no NULL guard (High/Medium)

- **Evidence:** `extra2.c:6–12` (downward scan from `p[n-1]`), `extra.c:8–14` (NULL
  deref), `musl_extra.c:100` (`memset(d,0,n)`).
- **Impact:** immediate OOB read/write on the first access when `n` overstates the
  buffer; NULL segfault. No bounded counterpart exists.
- **Fix:** add `if (!s) return NULL;` to `memrchr`/`memchr`; clamp `n` to
  `RSIZE_MAX_MEM`; for `stpncpy` bound the tail `memset` to the number of bytes actually
  copied; add `cplus_memrchr_bounded(const void*,int,size_t,size_t known)` following
  `bounded.c` pattern for the capacity-checked path.
- **Tests:** extend `tests/libc/test_string_extra2.c` with ASan cases
  `memrchr(smallbuf, 'x', 1<<20)` and `memrchr(NULL,'x',4)`; add
  `tests/crt/adversarial/test_mem50_memcpy_bounds.c`-style guard-byte test for `stpncpy`.

### #4 — Inconsistent refusal/termination semantics across the four memory tiers (Medium)

- **Evidence:** `memory.c:8/19/27/35` (zero-size before NULL in raw tier),
  `bounded.c:78–80` (`memcmp_bounded` refusal returns 0),
  `safe_memory.c:228–245` (`memcmp_s_auto` returns -1), `bounded.c:84–113` (no NUL on
  refusal; unterminated source out of scope).
- **Impact:** `memcmp_bounded` refusal is indistinguishable from equality — a caller
  branching on `==0` may authorize an action. Raw vs bounded NULL-at-zero-size differs
  silently.
- **Fix:** return a documented sentinel from `memcmp_bounded` (e.g. `INT_MIN`) and set
  `errno`; document the sentinel in `cplus_libc.h`; align raw functions to NULL-check
  first (or document the zero-size exception); explicitly NUL-terminate `dest` on
  string refusal when capacity permits.
- **Tests:** new `tests/libc/test_bounded_refusal_sentinel.c` (assert
  `memcmp_bounded(bad_ptr,...,known=0) == CPLUS_BOUNDS_REFUSED && errno==EINVAL`);
  extend `tests/crt/unit/test_memfam_null_and_zerolen.c` to assert both tiers agree on
  NULL+n==0.

### #5 — Source capacity is never independently verified in the bounded memcpy/memmove path (Medium)

- **Evidence:** `bounded.c:61–73` (`known` parameter is destination-only; underlying
  `cplus_memcpy`/`memmove` at memory.c:6–24 have no source bound check); contrast
  `safe_memory.c:159,188` which pass `smax=n` to safeclib.
- **Impact:** a short source buffer with a large `n` is over-read even when the
  destination is correctly sized.
- **Fix:** extend signatures to accept `known_src` (e.g.
  `cplus_memcpy_bounded(d,s,n,known_d,known_s)`) or apply
  `__builtin_object_size(s,1)` / `cplus_alloc_remaining_capacity(s)` internally in a
  `_bounded2` variant; refuse in strict mode when source capacity is unknown.
- **Tests:** new `tests/libc/test_bounded_source_overread.c` (small source inside a
  guard page / ASan redzone, large `n`, correct large dest — expect refusal, no OOB
  read); extend `tests/crt/adversarial/test_mem50_cross_api.c`.

---

## 6. Enforcement separation (explicit)

**Runtime-enforced in wrapper bodies (`R`):** NULL guards (most functions),
destination capacity vs `min(tracked remaining, __builtin_object_size)` (`*_bounded`,
`*_s_auto`), max-size clamps (`*_s_auto` only), UAF refusal (`*_s_auto`/`_s`), overlap
direction (`memmove`), terminator writes (`strncat`, `strdup`, `strndup`, tokenizers),
deterministic NULL comparison results, `errno`/constraint-handler diagnostics,
strict fail-closed mode.

**Compiler-enforced (`C mp`):** `CPLUS_LIBC_OBJ_SIZE(p)` /
`CPLUS_SAFE_OBJ_SIZE(p)` = `__builtin_object_size(p,1)` in
`cplus_libc_safe.h:33` / `safe_memory.h:63` (feeds the runtime capacity check — the
compile-time part only computes a constant where the compiler can); `format(printf,…)`
/ `format(scanf,…)` attributes on the stdio wrappers in `cplus_libc.h:120–132`;
the compile-fail contract `tests/compile_fail/classic_bug31_char_ctype.c` (plain
`<ctype.h>` discipline). **No raw `cplus_*` memory/string function is itself
compiler-enforced** — they are ordinary C functions with no attributes.

**Caller responsibility (`CR`) — no check exists and none is claimed:** raw pointer
provenance and lifetime; object size for all raw (`cplus_memcpy`, `cplus_strcpy`,
`memrchr`, all musl extras, host-delegated extras) functions; source termination for
every `strlen`-based bounded/s_auto wrapper; destination termination for `strncpy`;
overlap in raw `memcpy`/`str*`; positive/correct `ssize_t n` for `swab`; correct
`dmax`/`smax` for the direct `_s` tier; format strings for raw `printf`-family;
thread-safety of `strtok`/`strerror`/`strsignal`. Default unknown-capacity mode is
explicitly **unchecked** in both layers — the caller must opt into strict mode.

---

## 7. Verification note

This is a static audit. No build, test, or sanitizer run was performed as part of it;
all "evidence" citations are `file:line` references to the checked-in source on
branch `agent/modern-stdlib`. Assertions about upstream safeclib behaviour (e.g.
NUL-on-constraint-violation, its own NULL/overlap checks) are attributed to the
delegate and were **not** verified by reading `third_party/` in this pass. Test names
in §5 are proposed additions, not claims of existing coverage.
