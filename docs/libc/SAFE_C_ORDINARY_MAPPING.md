# Ordinary-name → checked `_s` mapping (`stdlib/safe/safe_c.h`)

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23
**Scope:** exact, source-derived audit of the 20 ordinary C spellings requested
for this review. Sources read: `stdlib/safe/safe_c.h`, `safe_memory.h`,
`safe_string.h`, `safe_io.h`, `safe_alloc.h`, `safe_format.h`,
`scripts/build_safecrt.sh`, plus the implementations those headers resolve to
(`stdlib/safe/src/safe_memory.c`, `safe_string.c`, `safe_alloc.c`, `safe_io.c`,
`libc/src/stdio/stdio.c`, `libc/install-include/cplus_libc.h`,
`stdlib/safe/safe_error.h`). Every row below cites its defining lines. No
mapping is inferred beyond what the macros actually expand to.

---

## 1. Mechanism (must be read before the table)

**Ordinary names remain source-compatible in spelling and argument lists, but
they are macro-mapped at compile time — they are NOT implemented by symbol
interposition.** Including `stdlib/safe/safe_c.h` `#undef`s the ordinary token
and `#define`s it to a checked target (`safe_c.h:61-117`). The preprocessor
rewrites the call in each translation unit that includes the header; nothing
replaces a symbol in a shared or static host C library. **Precompiled C
libraries (and any object file or translation unit compiled without the header)
are completely unaffected**: their `malloc`/`memcpy`/`printf` relocations still
bind to the host CRT, exactly as before. Define `CPLUS_SAFE_C_DISABLE` before
the include to suppress all mappings and get declarations only (`safe_c.h:14-15,
61-63`).

Three distinct checked targets are used; they live in two different archives:

| Target family | Example | Link archive |
|---|---|---|
| Direct `_s` rename | `malloc` → `malloc_s` | `build/safe/libcplus_safecrt.a` |
| Auto-bounds `_s_auto` wrapper (ordinary signature + inferred capacity) | `memcpy` → `cplus_memcpy_s_auto` | `build/safe/libcplus_safecrt.a` |
| Namespaced hardened-libc wrapper (`cplus_*`, carries Clang `format` attribute or overflow guard) | `fread` → `cplus_fread`, `printf` → `cplus_printf` | `build/libc/libcplus_hardened.a` |

`safe_c.h:7-8` instructs callers to link **both** archives (also
`docs/libc/ORDINARY_C_SAFETY.md:15-20`). The explicit Annex-K-style `_s` names
(`memcpy_s`, `strcpy_s`, `fopen_s`, `printf_s`, …) are a *separate* macro layer
defined in the sub-headers (`safe_memory.h:86-121`, `safe_string.h:68-107`,
`safe_io.h:17-26`, `safe_format.h:78-104`) and are guarded by
`CPLUS_SAFE_NO_UNPREFIXED_MACROS`; the ordinary-name mappings in `safe_c.h` are
guarded only by `CPLUS_SAFE_C_ENABLE`. They are not the same thing.

Build facts from `scripts/build_safecrt.sh`: safeclib sources are compiled with
`-D` symbol prefixing (`cplus_sc_*`, line 38) and archived into
`libcplus_safecrt.a` (lines 102-135). Intel safestringlib is retained for
provenance only and **not** compiled or linked (line 126).

**Error-code domains — do not confuse them.** The explicit checked functions
return `errno_t` with **positive** safeclib constraint codes (`ESNULLP` 400,
`ESZEROL` 401, `ESLEMIN` 402, `ESLEMAX` 403, `ESOVRLP` 404, `ESNOSPC` 406,
`ESUNTERM` 407 — `safe_memory.h:24-31`, `safe_string.h:24-31`). The ordinary
mappings mostly **collapse** those internal codes into the ordinary return type
plus platform `errno`: `EINVAL` 22, `ERANGE` 34, `ENOMEM` 12, `EOVERFLOW` 75,
`EFAULT` (UAF), `EMFILE` (stream-cache full). The `CPLUS_SAFE_*` constants are
in `safe_error.h:7-12`. Test `errno_t` results with `!= 0`, never `< 0`.

---

## 2. Allocation

| Ordinary spelling | Checked implementation | Capacity source | Error code domain | What changes for valid input | What is refused for invalid input | ABI impact |
|---|---|---|---|---|---|---|
| `malloc` | `malloc_s` (`safe_c.h:72`; decl `safe_alloc.h:51`; impl `safe_alloc.c:158-181`) | The `size` argument, rejected if `> RSIZE_MAX_MEM` (`SIZE_MAX>>1`, `safe_alloc.h:9-11`). `size == 0` is rewritten to a tracked 1-byte block (`safe_alloc.c:160`). | Platform `errno`: `EINVAL` (oversize), `ENOMEM` (OOM / bookkeeping OOM). Returns `NULL` on failure. No `errno_t`. | Same `void*(size_t)` result as host `malloc`, plus a record `{ptr, byte_size, elem_size=1, generation}` in a thread-safe live table (`safe_alloc.c:180`, `finish_alloc` 133-148). `malloc(0)` now yields a real tracked block. | `size > RSIZE_MAX_MEM` → `NULL`/`EINVAL`; OOM or metadata-table OOM → `NULL`/`ENOMEM`. | Macro only; signature and return type unchanged. Call site binds to `malloc_s` in `libcplus_safecrt.a`. If `CPLUS_MALLOC_TRACKING` is also set (`cplus_libc.h:76-85`), `safe_c.h`'s later `#undef`/`#define` overrides it. |
| `calloc` | `calloc_s` (`safe_c.h:73`; decl `safe_alloc.h:52`; impl `safe_alloc.c:183-215`) | `count * size`, with explicit overflow guard `count > SIZE_MAX/size || product > RSIZE_MAX_MEM` (`safe_alloc.c:193`). Zero count or size → 1×1 tracked block. | Platform `errno`: `EOVERFLOW` (product overflow), `ENOMEM` (OOM/injected). Returns `NULL` on failure. | Same result as host `calloc` (zeroed), plus `elem_size = size` recorded in the live table (`safe_alloc.c:199-200,214`), enabling `cplus_safe_ptr_count`/`elem_size` queries. | Overflow → `NULL`/`EOVERFLOW`; OOM → `NULL`/`ENOMEM`. | Macro `calloc(n,sz)→calloc_s((n),(sz))`; arguments parenthesized, signature preserved; symbol in `libcplus_safecrt.a`. |
| `realloc` | `realloc_s` (`safe_c.h:74`; decl `safe_alloc.h:53`; impl `safe_alloc.c:241-317`) | New `size` vs `RSIZE_MAX_MEM`; `ptr` must be a live block of this allocator (`tab_find_addr`, line 259). | Platform `errno`: `EINVAL` (oversize or unowned ptr), `ENOMEM` (OOM/injected). Returns `NULL` on failure, original block left live and untouched. | `ptr==NULL` acts like `malloc_s(size)`; on success preserves `elem_size`, bumps generation, ABA-guards the host `realloc` (`safe_alloc.c:253-255,299-311`). `realloc(p,0)` yields a 1-byte block, not a host-dependent free. | `size > RSIZE_MAX_MEM` → refused/`EINVAL`; pointer not owned by this allocator (stack, static, interior, raw `malloc` memory) → refused/`EINVAL`, pointer untouched; OOM → `NULL`/`ENOMEM`, pointer untouched. | Macro; signature preserved; behavior change versus host `realloc` on unowned pointers and on size 0. |
| `free` | `free_s` (`safe_c.h:75`; decl `safe_alloc.h:54`; impl `safe_alloc.c:352-360`, `free_checked_s` 319-350) | Live-table membership plus a bounded 1024-slot recent-free cache (`RECENT_SLOTS`, `safe_alloc.c:21,30`). | `void` return (like host `free`); on refusal sets `errno = EINVAL` (`safe_alloc.c:345`). The sibling `free_checked_s` returns `CPLUS_SAFE_OK` 0 / `CPLUS_SAFE_EINVAL` 22 (`safe_error.h:7-8`). | Same no-op-on-NULL behavior; valid tracked blocks are removed from the table and passed to host `free` (`safe_alloc.c:341-343`). | Double free (recent-free cache hit) or invalid/unowned pointer → **refused**; block left alone (leak preferred over heap corruption) and `errno=EINVAL` (`safe_alloc.h:29-31,33-37`). A very old double free may be classed as *invalid* free — still never freed twice. | Macro; signature preserved; **behavior change**: `free()` on a raw-host-`malloc` pointer, previously valid, is now refused. |

---

## 3. Memory operations

Capacity for all four is resolved by `cplus_safe_capacity` (`safe_memory.c:94-106`):
the **minimum** of `cplus_safe_remaining_capacity(ptr)` (remaining bytes of the
containing tracked live block, interior pointers included — `safe_alloc.c:367-393`)
and `CPLUS_SAFE_OBJ_SIZE(p)` = `__builtin_object_size(p, 1)` for stack/global
objects (`safe_memory.h:61-67`). `(size_t)-1` means unknown: default mode
proceeds with a warning and makes **no safety claim** (`safe_memory.c:109-122`);
strict mode via `cplus_safe_set_unknown_capacity_mode(1)` refuses with `EINVAL`.

| Ordinary spelling | Checked implementation | Capacity source | Error code domain | What changes for valid input | What is refused for invalid input | ABI impact |
|---|---|---|---|---|---|---|
| `memcpy` | `cplus_memcpy_s_auto` (`safe_c.h:84-85`; decl `safe_memory.h:50`; impl `safe_memory.c:138-165`) | Destination only: `min(tracked remaining capacity, __builtin_object_size(dest,1))`. Source length is passed as `smax = n` to safeclib (`safe_memory.c:159`); no independent source-capacity proof. | Internal safeclib `errno_t` (400/401/402/403/404/406); caller sees `void*` + `errno`: `EFAULT` (UAF), `EINVAL` (NULL/underlying constraint), `ERANGE` (overflow, logged as `ESNOSPC` 406 — `safe_memory.c:124-136,155-157`). `NULL` on any refusal. | Bytes copied via `cplus_sc_memcpy_s` into `dest`; on success returns `dest`. `n == 0` returns `dest` without calling safeclib. | NULL `dest`/`src` even when `n == 0` → `NULL`/`EINVAL` (`safe_memory.c:143-149`); use-after-free pointer → `NULL`/`EFAULT` + `cplus_runtime_violation`; `n > dmax` or `n > RSIZE_MAX_MEM` → `NULL`/`ERANGE` + violation; strict-mode unknown capacity → `NULL`/`EINVAL`. Overlap is **not** guaranteed detected at this layer — use `memmove` (`safe_c.h:36-37`). | **Caveat:** macro is `((void)cplus_memcpy_s_auto(...))` — return value is cast to `void`, so `void *p = memcpy(...)` no longer compiles (verified with `clang -fsyntax-only`). Normal statement-form calls keep the signature. Symbol in `libcplus_safecrt.a`. |
| `memmove` | `cplus_memmove_s_auto` (`safe_c.h:86-87`; decl `safe_memory.h:51`; impl `safe_memory.c:167-194`) | Same as `memcpy`, destination only. | Same domains as `memcpy` (`safe_memory.c:183-192`). | Same byte result as host `memmove`, including overlap, via `cplus_sc_memmove_s`; returns `dest`, `n==0` → `dest`. | Same refusals as `memcpy`: NULL even at `n==0`, UAF, over-capacity, oversize, strict unknown capacity. | Same `(void)`-cast caveat as `memcpy` (`safe_c.h:86-87`). |
| `memset` | `cplus_memset_s_auto` (`safe_c.h:82-83`; decl `safe_memory.h:52`; impl `safe_memory.c:196-222`) | Destination only: same `min(...)` resolution. | Same domains (`safe_memory.c:211-220`); overflow logged as `ESNOSPC` 406. | Fills `n` bytes via `cplus_sc_memset_s`; returns `dest`; `n==0` → `dest`. This is the overflow case in the header example (`safe_c.h:12`). | NULL `dest` even at `n==0` → `EINVAL`; UAF → `EFAULT` + violation; `n > dmax` or `> RSIZE_MAX_MEM` → `ERANGE` + violation; strict unknown capacity → `EINVAL`. | Same `(void)`-cast caveat (`safe_c.h:82-83`). |
| `memcmp` | `cplus_memcmp_s_auto` (`safe_c.h:88-89`; decl `safe_memory.h:53`; impl `safe_memory.c:224-252`) | **Both** operands: `CPLUS_SAFE_OBJ_SIZE(a)` and `(b)` from the macro, plus tracked capacity checked separately for each (`safe_memory.c:240-241`). | Internal safeclib `errno_t`; caller sees `int`: `diff` (<0/0/>0) on success, `-1` on UAF/overflow/strict refusal, sentinel `0/1/-1` with `errno=EINVAL` for NULL operands (`safe_memory.c:228-249`). | Returns the same sign of comparison as host `memcmp` via `cplus_memcmp_s`; `n==0` → `0` without calling safeclib. | NULL `s1`/`s2` for `n>0` → `errno=EINVAL` + sentinel; UAF → `-1`/`EFAULT`; `n > d1` or `n > d2` or `> RSIZE_MAX_MEM` → `-1`/`ERANGE` + violation; strict unknown capacity → `-1`. | Macro returns `int` (no `void` cast). **Caveat:** `-1` is also a legitimate comparison result, so a refusal is not distinguishable from "less than" at the ordinary call site. |

---

## 4. Strings

Capacity is resolved by `cplus_safe_resolve_str_dmax` (`safe_string.c:119-130`):
`min(tracked remaining capacity, __builtin_object_size(dest,1))`; if unknown,
default mode uses a *caller-derived fallback* (`strlen(src)+1`, `n+1`,
`strlen(dest)+strlen(src)+1`, `strlen(dest)+n+1`) and makes no safety claim;
strict mode refuses with `EINVAL`.

| Ordinary spelling | Checked implementation | Capacity source | Error code domain | What changes for valid input | What is refused for invalid input | ABI impact |
|---|---|---|---|---|---|---|
| `strcpy` | `cplus_strcpy_s_auto` (`safe_c.h:96-97`; decl `safe_string.h:51`; impl `safe_string.c:132-157`) | Destination capacity as above; fallback `strlen(src)+1` (`safe_string.c:144`). | Internal safeclib `errno_t`; caller sees `char*` + `errno`: `EINVAL` (NULL/strict), `EFAULT` (UAF), `ERANGE` when `r==406` or `len+1 > dmax` (`safe_string.c:153`). `NULL` on refusal. | Copies source including NUL via `cplus_sc_strcpy_s`; returns `dest`. Same bytes for valid input; adds live-table/BOS checks. | NULL `dest`/`src` → `EINVAL`; UAF → `EFAULT` + violation; destination too small (`ESNOSPC`) → `ERANGE`; strict unknown capacity → `EINVAL`. An **unterminated `src` is not detected** — the wrapper calls raw `strlen(src)` at line 143. | Macro returns `char*`; standard signature preserved; symbol in `libcplus_safecrt.a`. |
| `strncpy` | `cplus_strncpy_s_auto` (`safe_c.h:98-99`; decl `safe_string.h:52`; impl `safe_string.c:159-187`) | Destination; fallback `n+1` (`safe_string.c:174`). | As `strcpy`; `ERANGE` when `r==406` or `n >= dmax` (`safe_string.c:183`). `n==0` → returns `dest` without safeclib. | Bounded copy via `cplus_sc_strncpy_s(dest, dmax, src, n)`; returns `dest`. | NULL/UAF/over-capacity/oversize/strict as above. Unlike raw `strncpy`, a source not NUL-terminated within the bound is a safeclib constraint violation rather than a silent non-terminated copy. | Macro returns `char*`; signature preserved. |
| `strcat` | `cplus_strcat_s_auto` (`safe_c.h:100-101`; decl `safe_string.h:53`; impl `safe_string.c:189-220`) | Destination; fallback `strlen(dest)+strlen(src)+1` (`safe_string.c:207`). Explicit sum guard `dlen > RSIZE_MAX_STR - slen - 1` → `ERANGE` (lines 202-206). | As `strcpy`; `ERANGE` when `r==406` or `dlen+slen+1 > dmax` (`safe_string.c:216`). | Appends via `cplus_sc_strcat_s`; returns `dest`; same result as host `strcat` for valid input. | NULL/UAF; destination too small for existing + appended string → refused/`ERANGE`; length-sum overflow → `ERANGE`; strict unknown capacity → `EINVAL`. Unterminated `dest`/`src` not detected (raw `strlen` at lines 200-201). | Macro returns `char*`; signature preserved. |
| `strncat` | `cplus_strncat_s_auto` (`safe_c.h:102-103`; decl `safe_string.h:54`; impl `safe_string.c:222-256`) | Destination; fallback `strlen(dest)+n+1` (`safe_string.c:243`). Guard `n >= RSIZE_MAX_STR - dlen` → `ERANGE` (238-242). | As `strcpy`; `ERANGE` when `r==406` or `dlen+n+1 > dmax` (`safe_string.c:252`). `n==0` → `dest` no-op. | Appends at most `n` chars via `cplus_sc_strncat_s`; returns `dest`. | NULL/UAF; destination too small → refused/`ERANGE`; length-sum overflow → `ERANGE`; strict unknown capacity → `EINVAL`. | Macro returns `char*`; signature preserved. |
| `strlen` | **NOT MAPPED.** No `#undef/#define strlen` exists in `safe_c.h` (string section `safe_c.h:91-103`) or in `safe_string.h` (`:101-106`). `cplus_strlen` is merely *declared* in `cplus_libc.h:91`. There is no Annex-K `strlen_s` in this project. | None. | Host CRT behavior; no checked error domain. | Unchanged: calls host `strlen`. | Nothing is diagnosed — `strlen(NULL)` or an unterminated buffer remains UB (noted in `docs/libc/NORMAL_VS_CHECKED.md:61-63`). | None: ordinary spelling binds to the host symbol. |
| `strnlen` | **NOT MAPPED as an ordinary name.** No `#define strnlen` anywhere (preprocessor probe confirms `strnlen("x",1)` survives). The checked `_s` form *does* exist: `strnlen_s` → `cplus_strnlen_s` (`safe_string.h:101-102`; decl `:46`; impl `safe_string.c:85-92`). `cplus_strnlen` is declared in `cplus_libc.h:92` but not macro-bound. | (For the opt-in `strnlen_s`: `smax` argument; UAF check in `safe_string.c:87-88`.) | (For `strnlen_s`: returns a count in `[0,smax]`, `0` on UAF — no `errno_t`.) | Ordinary `strnlen` unchanged; it does not gain the `cplus_strnlen_s` UAF guard. | Nothing is diagnosed at the ordinary call site. | None for the ordinary spelling; callers must opt in by *rewriting* to `strnlen_s`. |

---

## 5. Stream I/O and formatted output

| Ordinary spelling | Checked implementation | Capacity source | Error code domain | What changes for valid input | What is refused for invalid input | ABI impact |
|---|---|---|---|---|---|---|
| `fread` | `cplus_fread` (`safe_c.h:108`; decl `cplus_libc.h:126`; impl `libc/src/stdio/stdio.c:177-182`). **Not** `cplus_fread_s`. | **None** — there is no `bmax` at the ordinary layer. Only the `size * nmemb` product is checked (`nmemb > SIZE_MAX/size`). The checked `cplus_fread_s(buf,bmax,…)` with `bmax` clamping is a separate `_s` API (`safe_io.h:14`, `safe_io.c:196-209`). | Platform `errno`: `EINVAL` (NULL stream, or NULL `ptr` when `size,nmemb>0`), `EOVERFLOW` (product overflow). Returns `size_t` item count, `0` on refusal. | Delegates to host `fread`; identical byte result and return for valid input. Adds overflow/NULL guards only. | NULL stream or NULL buffer (with nonzero size/count) → `0`/`EINVAL`; `size*nmemb > SIZE_MAX` → `0`/`EOVERFLOW`. Buffer capacity is **not** known or checked. | Macro keeps standard signature; call site binds to `cplus_fread` in `libcplus_hardened.a`, so linking only `libcplus_safecrt.a` leaves an unresolved symbol. |
| `fwrite` | `cplus_fwrite` (`safe_c.h:109`; decl `cplus_libc.h:126`; impl `stdio.c:184-189`). **Not** `cplus_fwrite_s`. | None — same as `fread` (product guard only). | Same as `fread` (`stdio.c:186-187`). | Delegates to host `fwrite`; identical result for valid input. | NULL stream/buffer → `0`/`EINVAL`; product overflow → `0`/`EOVERFLOW`. | Macro; symbol `cplus_fwrite` in `libcplus_hardened.a`. |
| `fopen` | **NOT MAPPED.** No `#define fopen` in `safe_c.h` (stream section `safe_c.h:105-109` maps only `fread`/`fwrite`) or in `safe_io.h` (`:17-26` defines only the `_s` aliases). `cplus_fopen` is declared in `cplus_libc.h:125` but not macro-bound. The checked form is the opt-in rewrite `fopen_s` → `cplus_fopen_s` (`safe_io.h:18-19`; impl `safe_io.c:124-151`), which adds a live/closed stream cache. | (For opt-in `fopen_s`: 512-slot live/closed caches, `STREAM_CACHE_SLOTS`, `safe_io.c:26`.) | (For `fopen_s`: `EINVAL` for NULL name/mode or double-close; `EMFILE` when a stream table is full — `safe_io.c:126-147,155-171`.) | Ordinary `fopen` unchanged: raw host call with no stream tracking. | Nothing is diagnosed; a later ordinary `fclose` is not double-close protected. | None for the ordinary spelling. |
| `fclose` | **NOT MAPPED** for the same reason. Checked form is opt-in `fclose_s` → `cplus_fclose_s` (`safe_io.h:20-21`; impl `safe_io.c:153-194`): refuses double close, records the close marker before calling host `fclose`, fails closed when the cache is full. | (For `fclose_s`: live/closed stream caches; bounded 512 slots per table.) | (For `fclose_s`: `errno=EINVAL` + `EOF` on NULL/double close, `EMFILE` + `EOF` if marker cannot be recorded — `safe_io.c:155-181`.) | Ordinary `fclose` unchanged: host call; no double-close detection. | Nothing is diagnosed at the ordinary call site. | None for the ordinary spelling. Mixing ordinary `fopen`/`fclose` with checked `fopen_s`/`fclose_s` gives only partial coverage (a double `fclose_s` is caught, but an ordinary `fclose` after `fclose_s` is not). |
| `printf` | `cplus_printf` (`safe_c.h:114`; decl `cplus_libc.h:135` with `CPLUS_LIBC_FMT_PRINTF(1,2)`; impl `stdio.c:203-207`). **Not** `cplus_printf_s`. | N/A (`stdout`). | Platform `errno`: `EINVAL` and `-1` return for NULL `fmt` (`stdio.c:205`). Compile-time: Clang `format(printf,1,2)` diagnoses literal format/argument mismatches (`safe_c.h:28-29`, `cplus_libc.h:128-135`). | Delegates to `vprintf`; identical output and return for valid input, plus compile-time format checking. **Runtime format safety does not change:** non-literal formats are not refused and `%n` is not blocked at this layer (`safe_c.h:38-40`). The `_s` format entry points reject both (`safe_format.h:15-23`, `safe_c.h:38-40`). | NULL `fmt` → `-1`/`EINVAL`. An untrusted/non-literal format or `%n` is **not** refused — this is explicitly an honest limit, not a checked path. | Variadic macro expands `printf(...)` to `cplus_printf(...)`; symbol in `libcplus_hardened.a`. Format attribute is on the target declaration, so diagnostics are preserved at the ordinary call site. |
| `fprintf` | `cplus_fprintf` (`safe_c.h:115`; decl `cplus_libc.h:135` with `CPLUS_LIBC_FMT_PRINTF(2,3)`; impl `stdio.c:209-213`). **Not** `cplus_fprintf_s`. | N/A. | Platform `errno`: `EINVAL`, `-1` return for NULL `stream` or `fmt` (`stdio.c:211`); compile-time `format(printf,2,3)` attribute. | Delegates to `vfprintf`; identical output/return for valid input, plus compile-time format checking. Non-literal formats and `%n` are not refused at this layer. | NULL `stream` or `fmt` → `-1`/`EINVAL`; untrusted format/`%n` not refused. | Variadic macro; symbol `cplus_fprintf` in `libcplus_hardened.a`. |

---

## 6. Gaps — promised/documented APIs that are **not** actually mapped

1. **Ordinary `strlen` and `strnlen` are not remapped.** The header's stated
   purpose is that "ordinary ISO C calls" be made safer without rewriting
   (`safe_c.h:5-6`), and the string section implies the ordinary string family
   is covered (`safe_c.h:91-103`, `safe_string.h:21-40,101-106`). In fact only
   `strcpy`/`strncpy`/`strcat`/`strncat` are macro-mapped. `strlen` has no
   `_s` counterpart at all; `strnlen` has one (`strnlen_s`, `safe_string.h:101-102`)
   but it requires an explicit source rewrite. Preprocessor evidence: the four
   probe calls `strlen`, `fopen`, `fclose`, `strnlen` pass through unchanged.
   **Consequence:** the untouched call sites get zero UAF/termination diagnosis.

2. **Ordinary `fopen` and `fclose` are not remapped.** `safe_io.h:8-9,12-15`
   describes checked `cplus_fopen_s`/`cplus_fclose_s`/`cplus_fread_s`/
   `cplus_fwrite_s` with stream tracking and double-close protection, and
   `safe_io.h:17-26` exposes them as `fopen_s`/`fclose_s`/`fread_s`/`fwrite_s`.
   But `safe_c.h:105-109` maps only `fread`/`fwrite` — and those map to the
   *hardened-libc* `cplus_fread`/`cplus_fwrite` (overflow guard only), **not**
   to the `bmax`-bearing `fread_s`/`fwrite_s` described at `safe_io.h:13-15`.
   So the ordinary `fopen`/`fclose` calls receive none of the documented
   double-close protection, and ordinary `fread`/`fwrite` receive none of the
   documented `bmax` clamping. A user reading `safe_io.h` in isolation could
   reasonably assume otherwise.

3. **Return-value source incompatibility for the three memory macros.**
   `safe_c.h:82-87` wraps `cplus_mem{set,cpy,move}_s_auto` in `(void)`, although
   the ordinary functions return `void*`. Verified with
   `clang -std=c11 -fsyntax-only`: `void *p = memcpy(d, s, 4);` fails with
   "initializing 'void *' with an expression of incompatible type 'void'".
   Statement-form calls are unaffected, but the "source-compatible" claim is
   not absolute for these three spellings.

4. **`memcmp` refusal can be confused with a valid result.** The auto wrapper
   returns `-1` for UAF, strict-mode refusal, and over-capacity
   (`safe_memory.c:228-245`) while `-1` is also a legitimate "less than"
   comparison. No error code is propagated to the ordinary caller (`errno` is
   set on some paths only, e.g. `EINVAL` at line 235 and `ERANGE` inside
   `cplus_safe_report_overflow`). This is inherent to keeping the standard
   `int` return type.

5. **Untrusted-format and `%n` rejection is promised only for the `_s` format
   API, not delivered for ordinary `printf`/`fprintf`.** `safe_c.h:38-40`
   explicitly says raw `printf` with an untrusted format remains unsafe and
   points to `safe_format.h`; `safe_format.h:15-23` promises literal-format and
   `%n` rejection for `printf_s`/`fprintf_s`/etc. The ordinary macro
   (`safe_c.h:114-115`) reaches `cplus_printf`/`cplus_fprintf` in the hardened
   libc, which only guard NULL (`stdio.c:203-213`). No mapping gap is present
   relative to the *stated* ordinary contract, but the distinction must not be
   blurred: only the compile-time `format` attribute applies to ordinary calls.

6. **`strcmp_s` has no unprefixed macro despite a defined implementation.**
   `cplus_strcmp_s` is declared (`safe_string.h:45`) and implemented
   (`safe_string.c:74-84`) and uses safeclib's `strcmp_s`, but no `strcmp_s`
   alias is emitted in `safe_string.h:68-107`, and ordinary `strcmp` is not
   mapped in `safe_c.h`. Ancillary to the 20 requested names; recorded so no
   reader assumes `strcmp_s` is available after including the header.

7. **Link/archive split is a mapping precondition, not visible in the macro.**
   `safe_c.h:7-8` requires both `libcplus_safecrt.a` and
   `libcplus_hardened.a`; the `fread`/`fwrite`/`printf`/`fprintf` targets live
   in the latter (`scripts/build_safecrt.sh:135` builds only the SafeCRT
   archive; `cplus_libc.h` declares the libc targets). Omitting the hardened
   archive produces unresolved symbols for four of the 20 mappings. Intel
   safestringlib is *not* linked (`build_safecrt.sh:126`), so none of these
   mappings come from it.

---

## 7. Bottom line for callers

- Including `safe_c.h` changes **source text** at preprocessing time; it does
  not modify or interpose any host CRT symbol. Precompiled C libraries are
  unaffected.
- 16 of the 20 audited ordinary names are mapped; **`strlen`, `strnlen`,
  `fopen`, `fclose` are not** — they require rewriting to `strnlen_s`,
  `fopen_s`, `fclose_s` (and `strlen` has no checked form here).
- Of the 16 mapped names, 4 use direct `_s` renames, 8 use `cplus_*_s_auto`
  inference wrappers, and 4 use hardened-libc `cplus_*` wrappers.
- The ordinary mappings surface **platform `errno` + ordinary return types**,
  not the safeclib `errno_t` constraint codes; read those codes only when
  calling the explicit checked API directly.
- Unknown capacity is never treated as verified capacity; default mode warns
  and preserves raw-C behavior, strict mode (`cplus_safe_set_unknown_capacity_mode(1)`)
  refuses. This is runtime diagnosis with tracked provenance, not a proof of
  pointer lifetime, ownership, or alias safety (`safe_memory.h:36-39`,
  `safe_string.h:36-39`).
