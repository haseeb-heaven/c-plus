# Ordinary C spelling → checked implementation: complete macro-remap audit

**Branch:** `agent/modern-stdlib`  **Date:** 2026-11-23
**Scope:** every ordinary ISO C spelling that is `#undef`/`#define`d to a
checked implementation by the two shipped safe-C layers, plus the complete set
of opt-in `_s` aliases defined in the same headers. Sources read in full:
`stdlib/safe/safe_c.h`, `safe_memory.h`, `safe_string.h`, `safe_io.h`,
`safe_format.h`, `safe_alloc.h`, `libc/install-include/cplus_libc_safe.h`.
Targets were verified with `nm -g` on `build/safe/libcplus_safecrt.a` and
`build/libc/libcplus_hardened.a`; exact nm lines are in `RESULTS.md` §2 and are
reproduced in the tables below. Runtime evidence is in `RESULTS.md` §1.

---

## 0. Mechanism (read before the tables)

1. **This is source-level macro remapping in the including translation unit
   only.** Each header `#undef`s the ordinary token and `#define`s it to a
   checked target. The preprocessor rewrites that call site; no host CRT symbol
   is replaced, wrapped, LD_PRELOADed or interposed.
2. **No ABI change.** No struct layout, calling convention, exported C symbol,
   vtable, or object layout changes. Mapped functions keep their ordinary
   parameter lists and return types, with three documented source-level
   caveats (§5). Linking the two static archives is a build prerequisite, not an
   ABI change.
3. **Precompiled C libraries are unaffected.** An object file or library
   compiled without the header still relocates `malloc`/`memcpy`/`printf` to
   the host CRT. `RESULTS.md` §3 proves this with `nm -u` on a `.o`, on an `ar`
   archive, and by linking a plain TU with a checked TU into one binary.
4. **Opt-out is declarations-only.** `-DCPLUS_SAFE_C_DISABLE` before including
   `safe_c.h` suppresses its mappings (`safe_c.h:14-15,70-74,181`);
   `-DCPLUS_LIBC_SAFE_DISABLE` does the same for `cplus_libc_safe.h`
   (`cplus_libc_safe.h:11,28-30,38`). Verified in `RESULTS.md` §3.
5. **Two independent layers, never mixed in one TU by this audit:**
   - **Layer A** `stdlib/safe/safe_c.h` — SafeCRT/safeclib-backed; link
     `libcplus_safecrt.a` **and** `libcplus_hardened.a`.
   - **Layer B** `libc/install-include/cplus_libc_safe.h` — standalone C+
     libc; link `libcplus_hardened.a` only.
6. **Error domains differ.** The ordinary mappings surface platform `errno`
   (EINVAL 22, ERANGE 34, EOVERFLOW 75, ENOMEM 12, EFAULT) through the ordinary
   return type; the explicit `_s` API returns safeclib `errno_t` codes (ESNULLP
   400, ESZEROL 401, ESLEMIN 402, ESLEMAX 403, ESOVRLP 404, ESNOSPC 406,
   ESUNTERM 407). Test `errno_t` with `!= 0`, never `< 0`.

---

## 1. Layer A — `stdlib/safe/safe_c.h` (16 ordinary names)

All 16 rows below were compiled and executed by `verify.sh` (§1 of
`RESULTS.md`): valid stdout is byte-identical with and without the header, and
every hostile case is refused by the checked build.

| Ordinary spelling | (a) Exact `#define` / `#undef` citation | (b) Target symbol, verified with `nm` | (c) Valid-input semantics preserved | (d) Invalid-input refusal + code | (e) ABI impact |
|---|---|---|---|---|---|
| `malloc` | `safe_c.h:77` `#undef malloc`; `:81` `#define malloc(sz) malloc_s(sz)` | `malloc_s` — `libcplus_safecrt.a` `0000000000000000 T _malloc_s` | Same `void *(size_t)`, same bytes; `size=0` becomes a tracked 1-byte block; block recorded in live table with `elem_size=1`, generation. | `size > RSIZE_MAX_MEM` → `NULL`/`EINVAL`; OOM or metadata OOM → `NULL`/`ENOMEM`. Hostile probe: oversize → exit 70. | Macro only; signature/return unchanged; call site binds to `malloc_s`. |
| `calloc` | `:78` `#undef calloc`; `:82` `#define calloc(n,sz) calloc_s((n),(sz))` | `calloc_s` — safecrt `00000000000001e4 T _calloc_s` | Zeroed block, same result; records `elem_size=size`; zero count/size → tracked 1-byte zeroed block. | `n*sz` overflow or `> RSIZE_MAX_MEM` → `NULL`/`EOVERFLOW`; OOM → `NULL`/`ENOMEM`. Hostile probe → exit 70. | Macro; arguments parenthesized; signature preserved. |
| `realloc` | `:79` `#undef realloc`; `:83` `#define realloc(p,sz) realloc_s((p),(sz))` | `realloc_s` — safecrt `0000000000000470 T _realloc_s` | `p==NULL` acts as `malloc_s`; on success bytes preserved, `elem_size` kept, generation bumped; `sz==0` → tracked 1-byte block. | Oversize or pointer not owned by this allocator (stack/static/interior/raw `malloc`) → `NULL`/`EINVAL`, original block left live and untouched; OOM → `NULL`/`ENOMEM`. Hostile probe (unowned stack ptr) → exit 70. | Macro; signature preserved; behavior differs from host `realloc` on unowned pointers and size 0. |
| `free` | `:80` `#undef free`; `:84` `#define free(p) free_s(p)` | `free_s` — safecrt `0000000000000bf4 T _free_s` | `void` return, `NULL` no-op; live tracked blocks released to host `free` and removed from table. | Double free (bounded recent-free cache) or invalid/unowned pointer → refused, block left alone (leak over corruption), `errno=EINVAL` (`safe_alloc.c:319-350`); sibling `free_checked_s` returns 0/`CPLUS_SAFE_EINVAL` 22. Hostile probe → exit 70. | Macro; signature preserved; `free()` on a raw-host-`malloc` pointer is now refused. |
| `memset` | `:87` `#undef memset`; `:91-92` `#define memset(d,c,n) ((void)cplus_memset_s_auto((d),(c),(n),CPLUS_SAFE_OBJ_SIZE(d)))` | `cplus_memset_s_auto` — safecrt `0000000000000934 T` | Same fill bytes and return in statement form; `n==0` → `dest`. Capacity = min(tracked remaining bytes incl. interior pointers, `__builtin_object_size(d,1)`). | NULL dest (even `n==0`) → `NULL`/`EINVAL`; UAF → `NULL`/`EFAULT`; `n > capacity` or `> RSIZE_MAX_MEM` → `ERANGE` + safeclib ESNOSPC 406 diagnostic + `cplus_runtime_violation` **exit 70** (`safe_memory.c:124-136,196-222`); strict unknown capacity → `EINVAL`. One-over stack probe → exit 70. | **Caveat:** macro casts return to `void`, so `void *p = memset(...)` no longer compiles; statement-form calls keep the signature. No symbol/layout change. |
| `memcpy` | `:88` `#undef memcpy`; `:93-94` `#define memcpy(d,s,n) ((void)cplus_memcpy_s_auto(...))` | `cplus_memcpy_s_auto` — safecrt `00000000000004c8 T` | Same bytes copied via `cplus_sc_memcpy_s`; returns `dest`; `n==0` → `dest`. Overlap is still UB — use `memmove` (`safe_c.h:36-37`). | NULL dest/src (even `n==0`) → `NULL`/`EINVAL`; UAF → `EFAULT`; over-capacity/oversize → `ERANGE`+406+exit 70; strict unknown capacity → `EINVAL`. One-over probe → exit 70. | Same `(void)`-cast caveat; symbol in safecrt. |
| `memmove` | `:89` `#undef memmove`; `:95-96` `#define memmove(d,s,n) ((void)cplus_memmove_s_auto(...))` | `cplus_memmove_s_auto` — safecrt `00000000000007b8 T` | Same result as host `memmove` including overlapping ranges; returns `dest`; `n==0` → `dest`. | Same domains as `memcpy` (`safe_memory.c:167-194`); one-over probe → exit 70. | Same `(void)`-cast caveat; no ABI change. |
| `memcmp` | `:90` `#undef memcmp`; `:97-98` `#define memcmp(a,b,n) cplus_memcmp_s_auto((a),(b),(n),CPLUS_SAFE_OBJ_SIZE(a),CPLUS_SAFE_OBJ_SIZE(b))` | `cplus_memcmp_s_auto` — safecrt `0000000000000b5c T` | Same sign of comparison via `cplus_memcmp_s`; `n==0` → 0; both operands' BOS capacities are passed. | NULL operand with `n>0` → `errno=EINVAL` + sentinel; UAF → `-1`/`EFAULT`; `n > d1` or `n > d2` or oversize → `-1`/`ERANGE`+406+exit 70; strict unknown → `-1`. One-over probe → exit 70. | Returns `int` (no void cast). **Caveat:** `-1` is also a valid "less than" result, so refusal is not distinguishable at the ordinary call site. |
| `strcpy` | `:101` `#undef strcpy`; `:105-106` `#define strcpy(d,s) cplus_strcpy_s_auto((d),(s),CPLUS_SAFE_OBJ_SIZE(d))` | `cplus_strcpy_s_auto` — safecrt `000000000000069c T` | Copies source including NUL, returns `dest`; same bytes for valid input. Capacity from tracked remaining bytes / BOS, fallback `strlen(src)+1`. | NULL → `EINVAL`; UAF → `EFAULT`; destination too small (safeclib 406) → `NULL`/`ERANGE` and IGNORE-handler line `(406) strcpy_s` on stdout (`safe_string.c:132-157`); strict unknown → `EINVAL`. Unterminated `src` not detected (raw `strlen`). One-over probe → refusal. | Macro returns `char*`; standard signature preserved; symbol in safecrt. |
| `strncpy` | `:102` `#undef strncpy`; `:107-108` `#define strncpy(d,s,n) cplus_strncpy_s_auto((d),(s),(n),CPLUS_SAFE_OBJ_SIZE(d))` | `cplus_strncpy_s_auto` — safecrt `00000000000007dc T` | Bounded copy via `cplus_sc_strncpy_s`, returns `dest`; `n==0` → `dest`. | Same domains; `r==406` or `n >= dmax` → `NULL`/`ERANGE`; a source not NUL-terminated within `n` is a constraint violation rather than a silent non-terminated copy. One-over probe → refusal. | Macro returns `char*`; signature preserved. |
| `strcat` | `:103` `#undef strcat`; `:109-110` `#define strcat(d,s) cplus_strcat_s_auto((d),(s),CPLUS_SAFE_OBJ_SIZE(d))` | `cplus_strcat_s_auto` — safecrt `0000000000000920 T` | Appends via `cplus_sc_strcat_s`, returns `dest`; same result as host `strcat` for valid input. | NULL/UAF; `dlen+slen+1 > dmax` or sum overflow → `NULL`/`ERANGE`; strict unknown → `EINVAL`; unterminated operands not detected. One-over probe → refusal. | Macro returns `char*`; signature preserved. |
| `strncat` | `:104` `#undef strncat`; `:111-112` `#define strncat(d,s,n) cplus_strncat_s_auto((d),(s),(n),CPLUS_SAFE_OBJ_SIZE(d))` | `cplus_strncat_s_auto` — safecrt `0000000000000a80 T` | Appends at most `n` chars, returns `dest`; `n==0` → no-op. | NULL/UAF; `dlen+n+1 > dmax` or length-sum overflow → `NULL`/`ERANGE`; strict unknown → `EINVAL`. One-over probe → refusal. | Macro returns `char*`; signature preserved. |
| `fread` | `:115` `#undef fread`; `:117` `#define fread(p,sz,n,f) cplus_fread((p),(sz),(n),(f))` | `cplus_fread` — `libcplus_hardened.a` `0000000000000494 T _cplus_fread` | Delegates to host `fread`; identical bytes and item count for valid input. | NULL stream, or NULL buffer with `size,nmemb>0` → `0`/`EINVAL`; `size*nmemb > SIZE_MAX` → `0`/`EOVERFLOW` (`libc/src/stdio/stdio.c:177-182`). Buffer capacity is **not** known or checked (no `bmax` at ordinary layer). NULL-stream probe → exit 70. | Macro; symbol in hardened archive — linking only safecrt leaves unresolved `cplus_fread`. |
| `fwrite` | `:116` `#undef fwrite`; `:118` `#define fwrite(p,sz,n,f) cplus_fwrite((p),(sz),(n),(f))` | `cplus_fwrite` — hardened `0000000000000530 T _cplus_fwrite` | Delegates to host `fwrite`; identical result. | NULL stream/buffer → `0`/`EINVAL`; product overflow → `0`/`EOVERFLOW` (`stdio.c:184-189`). NULL-stream probe → exit 70. | Macro; symbol in hardened archive. |
| `printf` | `:121` `#undef printf`; `:123` `#define printf(...) cplus_printf(__VA_ARGS__)` | `cplus_printf` — hardened `00000000000006d0 T _cplus_printf` (declared with `CPLUS_LIBC_FMT_PRINTF(1,2)` in `cplus_libc.h:135`) | Delegates to `vprintf`; identical output/return for valid input, **plus compile-time** `format(printf,1,2)` diagnosis of literal format/argument mismatches. | NULL `fmt` → `-1`/`EINVAL` (`stdio.c:203-207`). **Untrusted/non-literal formats and `%n` are NOT refused** — only the `_s` format API does that (`safe_format.h:15-23`). NULL-format probe → exit 70. | Variadic macro; no ABI change; format attribute is on the target declaration so diagnostics survive at the call site. |
| `fprintf` | `:122` `#undef fprintf`; `:124` `#define fprintf(...) cplus_fprintf(__VA_ARGS__)` | `cplus_fprintf` — hardened `0000000000000734 T _cplus_fprintf` (format attribute `(2,3)`) | Delegates to `vfprintf`; identical output/return; compile-time format checking. | NULL stream or fmt → `-1`/`EINVAL` (`stdio.c:209-213`); non-literal format/`%n` not refused. NULL-format probe → exit 70. | Variadic macro; no ABI change. |

### Layer A: mapped `_s` names that are **not** ordinary spellings

`safe_c.h` additionally maps two `_s` names that have compiled targets (verified
in `RESULTS.md` §2). They are opt-in rewrites, not ordinary C spellings:

| `_s` spelling | (a) Citation | (b) Target (nm) | (c) Valid-input semantics | (d) Invalid-input refusal + code | (e) ABI impact |
|---|---|---|---|---|---|
| `memrchr_s` (3 or 4 args) | `safe_c.h:159-161` `#define memrchr_s(...) CPLUS_SAFE_CAT(CPLUS_SAFE_MEMRCHR_S_, CPLUS_SAFE_NARG(__VA_ARGS__))(__VA_ARGS__)`; helpers at `:151-158` | `cplus_sc_upstream_memrchr_s` — hardened `00000000000000d4 T` | 3-arg form passes `CPLUS_SAFE_OBJ_SIZE(dest)` as `dmax`; 4-arg passes caller `dmax` unchanged when within BOS. Result equals ordinary search. | EOK 0; ESNOTFND 409; ESNULLP 400; ESZEROL 401; ESLEMAX 403 (`dmax > RSIZE_MAX_MEM` or `ch>255`); 4-arg `dmax > BOS` → `*resultp=NULL`/ESNOSPC 406. | Macro-only; no new function or exported symbol. |
| `bsearch_s` | `safe_c.h:174-179` `#define bsearch_s(key,base,nmemb,size,compar,context) ...` | `cplus_sc_upstream_bsearch_s` — hardened `0000000000000000 T` | 6-arg signature unchanged; `context=NULL` gives ordinary `bsearch` shape; same element returned for in-bounds search. | BOS guard: `nmemb*size > BOS(base)` → `NULL`/`errno=ESNOSPC 406`; NULL key/base/compar → 400; oversize → 403; clean search sets `errno=0`. | Macro-only; no new function or exported symbol. |

---

## 2. Layer B — `libc/install-include/cplus_libc_safe.h` (16 ordinary names)

Same ordinary spellings, different checked targets (all in
`libcplus_hardened.a`, all nm-verified in `RESULTS.md` §2). This layer does not
need the SafeCRT archive. `CPLUS_LIBC_OBJ_SIZE(p)` is `__builtin_object_size(p,1)`
(`cplus_libc_safe.h:32-36`); tracked-block metadata comes from `cplus_malloc`
and family (`libc/src/stdlib/alloc.c`, `libc/src/string/bounded.c`).

| Ordinary spelling | (a) Exact `#define` / `#undef` citation | (b) Target symbol, verified with `nm` | (c) Valid-input semantics preserved | (d) Invalid-input refusal + code | (e) ABI impact |
|---|---|---|---|---|---|
| `malloc` | `:41` `#undef`; `:45` `#define malloc(sz) cplus_malloc(sz)` | `cplus_malloc` — hardened `0000000000000354 T` | Same `void *(size_t)`; `size==0` → 1-byte tracked block; metadata recorded (`alloc.c:232-256`). | OOM / injected failure → `NULL`/`ENOMEM`. No explicit `RSIZE_MAX_MEM` guard at this layer (unlike Layer A). | Macro only; signature preserved. |
| `calloc` | `:42` `#undef`; `:46` `#define calloc(n,sz) cplus_calloc((n),(sz))` | `cplus_calloc` — hardened `0000000000000440 T` | Same zeroed result; `elem_size=size` recorded; zero count/size → tracked 1-byte block. | `nmemb != 0 && size > SIZE_MAX/nmemb` → `NULL`/`ENOMEM` (`alloc.c:262-265`); OOM → `ENOMEM`. | Macro; signature preserved. |
| `realloc` | `:43` `#undef`; `:47` `#define realloc(p,sz) cplus_realloc((p),(sz))` | `cplus_realloc` — hardened `0000000000000554 T` | `p==NULL` → `cplus_malloc`; `size==0` → 1-byte block; success preserves elem metadata. | Pointer not in live table → `NULL`/`EINVAL`, block untouched; OOM → `ENOMEM`. | Macro; behavior differs from host on unowned pointers/size 0. |
| `free` | `:44` `#undef`; `:48` `#define free(p) cplus_free(p)` | `cplus_free` — hardened `0000000000000acc T` | `void` return, `NULL` no-op, live blocks released. | Double/invalid free → refused, `errno=EINVAL`, block left alone. | Macro; signature preserved. |
| `memset` | `:51` `#undef`; `:55-56` `#define memset(d,c,n) ((void)cplus_memset_bounded((d),(c),(n),CPLUS_LIBC_OBJ_SIZE(d)))` | `cplus_memset_bounded` — hardened `0000000000000014 T` | Same fill; capacity = min(tracked remaining, BOS). | NULL → `EINVAL`; over-capacity → `NULL`/`ERANGE`; `n > RSIZE_MAX_MEM` → `NULL`/`EOVERFLOW`; strict unknown → `EINVAL` (`bounded.c:41-78`). | `(void)`-cast caveat; no ABI change. |
| `memcpy` | `:52` `#undef`; `:57-58` `#define memcpy(d,s,n) ((void)cplus_memcpy_bounded(...))` | `cplus_memcpy_bounded` — hardened `0000000000000168 T` | Same bytes; returns `dest` internally. | NULL → `EINVAL`; over-capacity → `ERANGE`; absurd `n` → `EOVERFLOW`; strict unknown → `EINVAL`. | `(void)`-cast caveat; no ABI change. |
| `memmove` | `:53` `#undef`; `:59-60` `#define memmove(d,s,n) ((void)cplus_memmove_bounded(...))` | `cplus_memmove_bounded` — hardened `00000000000001fc T` | Same result incl. overlap. | Same domains as `memcpy`. | `(void)`-cast caveat; no ABI change. |
| `memcmp` | `:54` `#undef`; `:61-62` `#define memcmp(a,b,n) cplus_memcmp_bounded((a),(b),(n),CPLUS_LIBC_OBJ_SIZE(a),CPLUS_LIBC_OBJ_SIZE(b))` | `cplus_memcmp_bounded` — hardened `0000000000000290 T` | Same comparison sign; `n==0` → 0. | NULL → `errno=EINVAL`, returns 0; over-capacity → `ERANGE`, returns 0; absurd `n` → `EOVERFLOW`. **Refusal returns 0 ("equal"), indistinguishable from a legitimate equal result.** | Returns `int`; no ABI change; same ambiguity caveat as Layer A (different sentinel). |
| `strcpy` | `:64` `#undef`; `:68` `#define strcpy(d,s) cplus_strcpy_bounded((d),(s),CPLUS_LIBC_OBJ_SIZE(d))` | `cplus_strcpy_bounded` — hardened `0000000000000348 T` | Same copy incl. NUL, returns `dest`; fallback `strlen(src)+1`. | NULL → `EINVAL`; `strlen(src)+1 > capacity` → `NULL`/`ERANGE`; strict unknown → `EINVAL`. Unterminated source still caller UB. | Macro returns `char*`; signature preserved. |
| `strncpy` | `:65` `#undef`; `:69` `#define strncpy(d,s,n) cplus_strncpy_bounded((d),(s),(n),...)` | `cplus_strncpy_bounded` — hardened `00000000000003d8 T` | Bounded copy, returns `dest`; effective length capped to resolved capacity. | NULL → `EINVAL`; `n > capacity` → `NULL`/`ERANGE`; `n > RSIZE_MAX_MEM` → `NULL`/`EOVERFLOW`. | Macro returns `char*`; signature preserved. |
| `strcat` | `:66` `#undef`; `:70` `#define strcat(d,s) cplus_strcat_bounded((d),(s),...)` | `cplus_strcat_bounded` — hardened `0000000000000480 T` | Same append result, returns `dest`. | NULL → `EINVAL`; combined length > capacity → `NULL`/`ERANGE`; strict unknown → `EINVAL`. | Macro returns `char*`; signature preserved. |
| `strncat` | `:67` `#undef`; `:71` `#define strncat(d,s,n) cplus_strncat_bounded((d),(s),(n),...)` | `cplus_strncat_bounded` — hardened `000000000000051c T` | Same bounded append, returns `dest`. | NULL → `EINVAL`; `dlen+n+1 > capacity` → `NULL`/`ERANGE`; oversize → `EOVERFLOW`. | Macro returns `char*`; signature preserved. |
| `fread` | `:74` `#undef`; `:76` `#define fread(p,sz,n,f) cplus_fread((p),(sz),(n),(f))` | `cplus_fread` — hardened `0000000000000494 T` | Delegates to host `fread`; identical result. | NULL stream/buffer → `0`/`EINVAL`; product overflow → `0`/`EOVERFLOW` (`stdio.c:177-182`). | Macro; symbol in hardened archive. |
| `fwrite` | `:75` `#undef`; `:77` `#define fwrite(p,sz,n,f) cplus_fwrite((p),(sz),(n),(f))` | `cplus_fwrite` — hardened `0000000000000530 T` | Delegates to host `fwrite`; identical result. | NULL stream/buffer → `0`/`EINVAL`; product overflow → `0`/`EOVERFLOW`. | Macro; symbol in hardened archive. |
| `printf` | `:78` `#undef`; `:80` `#define printf(...) cplus_printf(__VA_ARGS__)` | `cplus_printf` — hardened `00000000000006d0 T` | Delegates to `vprintf`; identical output/return plus compile-time format attribute. | NULL fmt → `-1`/`EINVAL`; untrusted format/`%n` not refused. | Variadic macro; no ABI change. |
| `fprintf` | `:79` `#undef`; `:81` `#define fprintf(...) cplus_fprintf(__VA_ARGS__)` | `cplus_fprintf` — hardened `0000000000000734 T` | Delegates to `vfprintf`; identical output/return plus format attribute. | NULL stream/fmt → `-1`/`EINVAL`. | Variadic macro; no ABI change. |

---

## 3. Complete opt-in `_s` alias layer (these are **not** ordinary spellings)

These macros are included by `safe_c.h` through its sub-headers and are guarded
by `CPLUS_SAFE_NO_UNPREFIXED_MACROS`, *not* by `CPLUS_SAFE_C_ENABLE`. All targets
below are defined symbols in `build/safe/libcplus_safecrt.a` (nm-verified). They
are listed because the brief names `fopen`/`fclose`: those ordinary names are
**not** remapped; only their `_s` forms are.

| Alias | (a) Citation | (b) Target (nm, safecrt) | (c) Valid-input semantics | (d) Refusal + code | (e) ABI impact |
|---|---|---|---|---|---|
| `memcpy_s` (3/4 args) | `safe_memory.h:91-93` (helpers `:87-90`) | `cplus_memcpy_s_auto` `00000000000004c8 T` / `cplus_memcpy_s` `0000000000000000 T` | 3-arg → auto BOS; 4-arg → explicit dmax/smax. | `errno_t`: 400/401/403/404/406; caller sees `NULL`/platform errno. | Macro-only. |
| `memmove_s` | `safe_memory.h:99-101` (`:95-98`) | `cplus_memmove_s_auto` `00000000000007b8 T` / `cplus_memmove_s` `00000000000001a4 T` | As above, overlap-safe. | 400/401/403/404/406. | Macro-only. |
| `memset_s` | `safe_memory.h:107-109` (`:103-106`) | `cplus_memset_s_auto` `0000000000000934 T` / `cplus_memset_s` `0000000000000270 T` | As above; zero-length kept defined. | 400/401/403/406. | Macro-only. |
| `memcmp_s` | `safe_memory.h:115-117` (`:111-114`) | `cplus_memcmp_s_auto` `0000000000000b5c T` / `cplus_memcmp_s` `0000000000000328 T` | Ordering via `diff` out-param. | 400/401/403/406. | Macro-only. |
| `memzero_s` | `safe_memory.h:119-120` | `cplus_memzero_s` `0000000000000408 T` | Zeroes `dmax` bytes. | 400/403. | Macro-only. |
| `strcpy_s` | `safe_string.h:73-75` | `cplus_strcpy_s_auto` `000000000000069c T` / `cplus_strcpy_s` `0000000000000000 T` | 2-arg auto BOS, 3-arg explicit dmax. | 400/401/402/403/406/407. | Macro-only. |
| `strncpy_s` | `safe_string.h:81-83` | `cplus_strncpy_s_auto` `00000000000007dc T` / `cplus_strncpy_s` `0000000000000194 T` | Bounded copy. | 400/401/403/406/407. | Macro-only. |
| `strcat_s` | `safe_string.h:89-91` | `cplus_strcat_s_auto` `0000000000000920 T` / `cplus_strcat_s` `0000000000000260 T` | Bounded append. | 400/402/403/406/407. | Macro-only. |
| `strncat_s` | `safe_string.h:97-99` | `cplus_strncat_s_auto` `0000000000000a80 T` / `cplus_strncat_s` `000000000000031c T` | Bounded append. | 400/403/406/407. | Macro-only. |
| `strnlen_s` | `safe_string.h:101-102` (`#undef strnlen_s` / `#define strnlen_s cplus_strnlen_s`) | `cplus_strnlen_s` `00000000000004bc T` | Count in `[0,smax]`; 0 on UAF. | No `errno_t`; 0 on violation. | Macro-only. |
| `strerror_s` | `safe_string.h:103-104` | `cplus_strerror_s` `00000000000005f4 T` | Message with `...` truncation. | 400/401/402/403. | Macro-only. |
| `strtok_s` | `safe_string.h:105-106` | `cplus_strtok_s` `000000000000053c T` | Next token pointer or NULL. | 400/401 via errno. | Macro-only. |
| `fopen_s` | `safe_io.h:18-19` | `cplus_fopen_s` `0000000000000000 T` | Ordinary `fopen` result plus live-stream tracking. | `EINVAL` for NULL name/mode, `EMFILE` when 512-slot cache full. | Macro-only. |
| `fclose_s` | `safe_io.h:20-21` | `cplus_fclose_s` `000000000000033c T` | Same close result; records close marker first. | NULL/double close → `EINVAL`/`EOF`; cache full → `EMFILE`/`EOF`. | Macro-only. |
| `fread_s` | `safe_io.h:22-23` | `cplus_fread_s` `0000000000000544 T` | Adds `bmax` Annex-K parameter; clamps buffer. | Constraint violation → 0 / errno. | Macro-only. |
| `fwrite_s` | `safe_io.h:24-25` | `cplus_fwrite_s` `0000000000000618 T` | Adds `bmax`; clamps buffer. | Constraint violation → 0 / errno. | Macro-only. |
| `printf_s` | `safe_format.h:79-81` | `cplus_printf_s_checked` `000000000000077c T` | Literal format check, `%n` refused; same output. | Non-literal/`%n` → `-1`/`EINVAL` (or trap exit 70). | Macro-only. |
| `fprintf_s` | `safe_format.h:82-84` | `cplus_fprintf_s_checked` `0000000000000aac T` | As above on a stream. | As above. | Macro-only. |
| `snprintf_s` | `safe_format.h:85-87` | `cplus_snprintf_s_checked` `0000000000000434 T` | Adds BOS of dest + literal check. | Truncation/overflow/`%n` → `-1`/`EINVAL`. | Macro-only. |
| `sprintf_s` | `safe_format.h:88-90` | `cplus_sprintf_s_checked` `0000000000001794 T` | Adds BOS of dest + literal check. | As above. | Macro-only. |
| `scanf_s` | `safe_format.h:91-93` | `cplus_scanf_s_checked` `0000000000000f44 T` | Literal check; width enforcement for `%s`/`%[`. | Unbounded `%s`/`%n` → `-1`/`EINVAL`. | Macro-only. |
| `fscanf_s` | `safe_format.h:94-96` | `cplus_fscanf_s_checked` `00000000000011dc T` | As above on a stream. | As above. | Macro-only. |
| `sscanf_s` | `safe_format.h:97-99` | `cplus_sscanf_s_checked` `00000000000014c0 T` | As above on a string. | As above. | Macro-only. |
| `vsnprintf_s` | `safe_format.h:100-101` | `cplus_vsnprintf_s` `0000000000000000 T` | `va_list` form. | NULL fmt/dest → `-1`/`EINVAL`. | Macro-only. |
| `vsprintf_s` | `safe_format.h:102-103` | `cplus_vsprintf_s` `000000000000165c T` | `va_list` form. | NULL fmt/dest → `-1`/`EINVAL`. | Macro-only. |

---

## 4. Names deliberately **not** remapped (evidence, not omissions)

`verify.sh` compiles `unmapped_probe.c` with `-include stdlib/safe/safe_c.h` and
checks its undefined symbols with `nm -u` (all PASS in `RESULTS.md` §3):

| Ordinary spelling | Status | Evidence / how to opt in |
|---|---|---|
| `fopen` | **NOT mapped** — no `#define fopen` in `safe_c.h` or `safe_io.h`. `nm -u` still shows `_fopen`. | Use `fopen_s` → `cplus_fopen_s` (`safe_io.h:18-19`). |
| `fclose` | **NOT mapped** — only `fclose_s` exists (`safe_io.h:20-21`); `nm -u` shows `_fclose`. | Use `fclose_s` → `cplus_fclose_s`. Mixing ordinary and `_s` gives only partial double-close coverage. |
| `strlen` | **NOT mapped** — no checked `strlen_s` in this project. `nm -u` shows `_strlen`. | No checked form; `cplus_strlen` is only declared in `cplus_libc.h`. |
| `strnlen` | **NOT mapped as an ordinary name** — `nm -u` shows `_strnlen`. | Rewrite to `strnlen_s` → `cplus_strnlen_s` (`safe_string.h:101-102`). |
| `memchr_s`, `qsort_s`, `strcmp_s` | **No macro** and no compiled symbol for the first two; `cplus_strcmp_s` exists but has no unprefixed alias. | See `docs/libc/SAFE_C_NEW_MAPPINGS.md` for the nm audit. |

Consequently the brief's example list should be read as: `fread`/`fwrite` **are**
mapped; `fopen`/`fclose` **are not** at the ordinary spelling.

---

## 5. Honest caveats that are source-compatibility (not ABI) issues

1. `memcpy`/`memmove`/`memset` macros cast their result to `void`
   (`safe_c.h:91-96`), so `void *p = memcpy(d,s,n);` does not compile in a
   mapping-enabled TU. Statement-form calls are unaffected.
2. `memcmp` returns `-1` on refusal, which is also a legitimate comparison
   result (`safe_memory.c:224-252`). Layer B returns `0` on refusal, which is
   also a legitimate "equal" result (`bounded.c:96-104`).
3. Unknown capacity is never treated as verified: default mode preserves raw-C
   behavior with a warning; `cplus_safe_set_unknown_capacity_mode(1)` /
   `cplus_libc_set_unknown_capacity_mode(1)` makes it fail closed.
4. Overlapping raw `memcpy` is still UB; untrusted formats and `%n` are still
   accepted by ordinary `printf`/`fprintf`; unterminated string sources remain
   caller responsibility. These are runtime-diagnosis limits, not ABI changes.
5. No live-game or injected-DLL behavior is claimed: this is header, `nm`, and
   host C program verification only.
