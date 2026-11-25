# Safe CRT memory/string API contract — P0 audit

Date: 2026-11-23. Branch: `agent/modern-stdlib`. Scope: the "plain libc" memory/string
layer and the SafeCRT (safeclib-backed) memory/string adapters, plus the allocation
entry points that feed their capacity metadata. This document records the **observed**
contract of each API (not an aspiration), the concrete defect reproduced during this
audit, the minimal fix, and exact verification evidence.

Strict file ownership was honoured: only the files listed in §7 were touched. No
compiler/**, runtime/**, test, or runner file was modified.

## 0. Legend and shared constants

| Term | Meaning |
|---|---|
| `RSIZE_MAX_MEM` | Plain libc and `safe_memory.h`: `(size_t)(SIZE_MAX >> 1)`. Upstream safeclib build config (`scripts/build_safecrt.sh:67-71`): `RSIZE_MAX_STR = 4 KiB`, `RSIZE_MAX_MEM = 16 KiB`. `safe_string.c` compiles with `RSIZE_MAX_STR = SIZE_MAX >> 1` because `stdlib/safe/safe_string.h:13-15` self-defines it. |
| Known capacity | `min(cplus_alloc_remaining_capacity(p), __builtin_object_size(p,1))` (plain) or `min(cplus_safe_remaining_capacity(p), CPLUS_SAFE_OBJ_SIZE(p))` (SafeCRT). Interior pointers and the one-past-end address are handled. |
| Unknown capacity | Neither source can size the object. Default mode: operation proceeds **unchecked** (raw-C semantics, warning); strict mode (`cplus_libc_set_unknown_capacity_mode(1)` / `cplus_safe_set_unknown_capacity_mode(1)`): refuse with `errno=EINVAL`. |
| Constraint codes | Positive non-`errno` values: `ESNULLP` 400, `ESZEROL` 401, `ESLEMIN` 402, `ESLEMAX` 403, `ESOVRLP` 404, `ESNOSPC` 406, `ESUNTERM` 407. Test with `!= 0`, never `< 0`. |
| Atomicity | "No write on refusal" means the destination is byte-for-byte unchanged. safeclib string family instead resets the destination to `""` on constraint failure; the two behaviours are recorded per tier. |

## 1. Plain libc memory/string layer

Sources: `libc/src/string/string.c`, `bounded.c`, `extra.c`, `extra2.c`,
`musl_extra.c`; raw memory primitives `libc/src/string/memory.c` (audited only —
**not** in this agent's ownership, so no changes were made there).

| API (source) | Null handling | Source length validation | Destination capacity | Overflow guard | Overlap behavior | Truncation behavior | Error codes | Failure atomicity |
|---|---|---|---|---|---|---|---|---|
| `cplus_memcpy` (`memory.c:6`) | `n==0` returns `dest` before null test; null → `EINVAL`, returns `dest` | caller-owned (`n`) | none | none | **not checked**; `restrict` makes overlap caller UB | exact `n` bytes | `EINVAL` | null refusal: no write |
| `cplus_memmove` (`memory.c:15`) | `n==0`/`d==s` early return; null → `EINVAL` | caller-owned (`n`) | none | none | **correct** — direction chosen by address comparison | exact `n` bytes | `EINVAL` | null refusal: no write |
| `cplus_memset` (`memory.c:25`) | `n==0` returns `s`; null → `EINVAL` | caller-owned | none | none | N/A | exact `n` bytes | `EINVAL` | null refusal: no write |
| `cplus_memcmp` (`memory.c:33`) | `n==0` → 0 before null test; null → `EINVAL`, returns `0`/`1`/`-1` sentinel | caller-owned | none | none | N/A | compares ≤`n` | `EINVAL` | read-only |
| `cplus_strlen` (`string.c:13`) | null → 0 | scans to NUL | N/A | N/A | N/A | N/A | none | read-only |
| `cplus_strcpy` (`string.c:17`) | null → `EINVAL`, returns `dest` | scans source to NUL | none | none | `restrict` → caller UB | copies through NUL | `EINVAL` | null refusal: no write |
| `cplus_strncpy` (`string.c:22`) | `n==0` returns `dest` before null test; null → `EINVAL` | reads ≤`n` (NUL stops copy) | none | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (`string.c:30`) | `restrict` → caller UB | copies ≤`n`, zero-fills remainder; **no NUL if no NUL in first `n` bytes** (ISO behaviour) | `EINVAL`, `EOVERFLOW` | guard/null refusal: no write |
| `cplus_strcat` (`string.c:35`) | null → `EINVAL`, returns `dest` | scans both to NUL | none | none | `restrict` → caller UB | appends through source NUL | `EINVAL` | null refusal: no write |
| `cplus_strncat` (`string.c:40`) | null → `EINVAL`, returns `dest` | loop bounded by source NUL, so an absurd `n` cannot overrun a **valid** string | none | not needed for valid strings | `restrict` → caller UB | appends `min(n, strlen(src))` chars, always terminates | `EINVAL` | null refusal: no write |
| `cplus_strcmp` (`string.c:45`) | null → `EINVAL`, returns `0`/`1`/`-1` sentinel | scans to first difference/NUL | N/A | N/A | N/A | N/A | `EINVAL` | read-only |
| `cplus_strncmp` (`string.c:51`) | `n==0` → 0 before null test; null → `EINVAL`, same sentinel | compares ≤`n`, stops at NUL | N/A | N/A | N/A | N/A | `EINVAL` | read-only |
| `cplus_strnlen` (`extra.c:48`) | null → 0 | reads ≤`n`, stops at NUL (absurd `n` safe with a valid string) | N/A | N/A | N/A | returns `min(n, strlen)` | none | read-only |
| `cplus_memchr` (`extra.c:9`) | null → `EINVAL`, returns NULL | scans ≤`n` | N/A | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (`extra.c:16`) | N/A | N/A | `EINVAL`, `EOVERFLOW` | read-only |
| `cplus_memrchr` (`extra2.c:13`) | null → `EINVAL`, returns NULL | scans downward from `n-1` | N/A | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (`extra2.c:21`) | N/A | N/A | `EINVAL`, `EOVERFLOW` | read-only |
| `cplus_memccpy` (`musl_extra.c:34`, musl MIT) | no guard (caller-owned) | copies until `c` or `n` | none | **none** — see §5 note | `restrict` → caller UB | stops at first `c` | none | caller-owned |
| `cplus_stpncpy` (`musl_extra.c:88`, musl MIT) | null → `EINVAL`, returns NULL | copy bounded by source NUL/`n` | none | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (`musl_extra.c:97`) | `restrict` → caller UB | zero-fills to `n`; no NUL if source unterminated | `EINVAL`, `EOVERFLOW` | guard/null refusal: no write |
| `cplus_memmem` (`musl_extra.c:209`, musl MIT) | no guard (caller-owned) | two-way search within `k`,`l` | N/A | none | N/A | N/A | none | read-only |
| `cplus_strcasecmp`/`strncasecmp` (`extra2.c:27,39`) | no guard (caller-owned) | `strncasecmp` stops at NUL | N/A | N/A | N/A | N/A | none | read-only |
| `cplus_strdup` (`extra2.c:51`) | null → NULL | scans to NUL | allocates `n+1` via `cplus_malloc` | none (`n+1` wrap only for unterminated source = caller UB) | N/A | duplicates through NUL | `NULL` on OOM | no partial allocation |

### 1.1 Capacity-checked plain layer (`libc/src/string/bounded.c`, `cplus_libc_safe.h`)

These are the entry points the `cplus_libc_safe.h:51-71` macros redirect ordinary
`memset`/`memcpy`/`memmove`/`memcmp`/`strcpy`/`strncpy`/`strcat`/`strncat` to.
`cplus_libc_safe.h:41-48` likewise maps `malloc`/`calloc`/`realloc`/`free` to the
tracked `cplus_*` allocators. `CPLUS_LIBC_OBJ_SIZE` is `__builtin_object_size(p,1)`.

| API (source) | Null handling | Source length validation | Destination capacity | Overflow guard | Overlap behavior | Truncation behavior | Error codes | Failure atomicity |
|---|---|---|---|---|---|---|---|---|
| `cplus_memset_bounded` (`bounded.c:72`) | null → `EINVAL`, NULL | caller-owned `n` | `min(tracked, known)`; `n>cap` → `ERANGE`; unknown strict → `EINVAL` | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (`bounded.c:68-76`, new) | N/A | exact `n` bytes | `EINVAL`, `ERANGE`, `EOVERFLOW` | no write on any refusal |
| `cplus_memcpy_bounded` (`bounded.c:80`) | null → `EINVAL`, NULL | **source capacity not checked** (documented gap MEMSTR-24) | same as memset | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (new) | **not checked** (documented gap MEMSTR-24) | exact `n` bytes | `EINVAL`, `ERANGE`, `EOVERFLOW` | no write on any refusal |
| `cplus_memmove_bounded` (`bounded.c:88`) | null → `EINVAL`, NULL | source capacity not checked (MEMSTR-25) | same as memset | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (new) | safe (delegates to `cplus_memmove`) | exact `n` bytes | `EINVAL`, `ERANGE`, `EOVERFLOW` | no write on any refusal |
| `cplus_memcmp_bounded` (`bounded.c:96`) | null → `EINVAL`, return **0** | source capacity checked for both operands | both operands vs `min(tracked, known)` | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (new) | N/A | compares ≤`n` | `EINVAL`, `ERANGE`, `EOVERFLOW` | read-only; **refusal sentinel 0 is indistinguishable from "equal"** (MEMSTR-26) |
| `cplus_strcpy_bounded` (`bounded.c:106`) | null → `EINVAL`, NULL | `strlen(src)` runs before capacity decision (unterminated src = caller UB, MEMSTR-27) | `need = strlen(src)+1` vs capacity | no explicit max-size clamp on `need` | not checked | copies through NUL | `EINVAL`, `ERANGE` | no write on refusal |
| `cplus_strncpy_bounded` (`bounded.c:114`) | null → `EINVAL`, NULL | reads ≤`n` | `n` vs capacity; known overflow → `ERANGE` | **`n > RSIZE_MAX_MEM` → `EOVERFLOW`** (`bounded.c:122`); `n` also clamped to resolved `cap` | not checked | ISO `strncpy` truncation | `EINVAL`, `ERANGE`, `EOVERFLOW` | no write on refusal |
| `cplus_strcat_bounded` (`bounded.c:130`) | null → `EINVAL`, NULL | `strlen` of both before capacity (unterminated = caller UB, MEMSTR-29) | `need = strlen(d)+strlen(s)+1` | no explicit addition-overflow clamp | not checked | appends through NUL | `EINVAL`, `ERANGE` | no write on refusal |
| `cplus_strncat_bounded` (`bounded.c:138`) | null → `EINVAL`, NULL | `strlen` of both before capacity (MEMSTR-30) | `need = strlen(d)+min(n,strlen(s))+1` | absurd `n` naturally bounded by `min(n, strlen(s))` | not checked | appends `min(n, strlen(s))`, terminates | `EINVAL`, `ERANGE` | no write on refusal |

## 2. SafeCRT direct adapters (safeclib-backed)

Sources: `stdlib/safe/src/safe_memory.c:35-81`, `stdlib/safe/src/safe_string.c:38-114`.
All call `cplus_safe_reject_uaf` on every pointer first (UAF → `errno=EFAULT`,
return 400, violation report, no dereference), then delegate to the upstream
safeclib `cplus_sc_*` symbol. Capacity is the caller-supplied `dmax`/`smax`.

| API (source) | Null handling | Source length validation | Destination capacity | Overflow guard | Overlap behavior | Truncation behavior | Error codes | Failure atomicity |
|---|---|---|---|---|---|---|---|---|
| `cplus_memcpy_s` (`safe_memory.c:35`) | `ESNULLP` 400; zero-length NULL operands are a defined no-op (safeclib) | `smax` | `dmax` | `ESLEMAX` 403 when `smax > RSIZE_MAX_MEM` | `ESOVRLP` 404 (upstream) | copies `smax` bytes | 400/401/403/404/406 | dest left unchanged on refusal (`test_memfam_*`) |
| `cplus_memmove_s` (`safe_memory.c:44`) | same as memcpy_s | `smax` | `dmax` | `ESLEMAX` 403 | overlap **allowed** (memmove semantics) | `smax` bytes | 400/401/403/406 | dest unchanged on refusal |
| `cplus_memset_s` (`safe_memory.c:53`) | NULL dest always `ESNULLP` 400, even `n==0` | `n` | `dmax` | `ESLEMAX` 403 | N/A | fills `n` bytes | 400/401/403/406 | dest unchanged on refusal |
| `cplus_memcmp_s` (`safe_memory.c:62`) | NULL → 400; `diff==NULL` → 400 | `smax` | `dmax` both | `ESLEMAX` 403 | N/A | compares ≤`smax` | 400/401/403 | `*diff` set to -1 on UAF refusal |
| `cplus_memzero_s` (`safe_memory.c:73`) | NULL → 400 | `dmax` | `dmax` | `ESLEMAX` 403 | N/A | zeroes `dmax` bytes | 400/401/403 | dest unchanged on refusal |
| `cplus_strcpy_s` (`safe_string.c:38`) | NULL dest/src → 400 | source read to NUL by safeclib | `dmax` | `ESLEMAX` 403 | `ESOVRLP` 404 | copies through NUL, `ESNOSPC` 406 if no room | 400/401/403/404/406 | **dest reset to `""`** on refusal (`test_strcpy_strncat_overflow.c:73`) |
| `cplus_strncpy_s` (`safe_string.c:47`) | NULL → 400 | `slen` | `dmax` | `ESLEMAX` 403 | `ESOVRLP` 404 | copies `min(slen, strlen)`, pads/NUL; unterminated → `ESUNTERM` 407 | 400/401/403/404/406/407 | dest reset on refusal |
| `cplus_strcat_s` (`safe_string.c:56`) | NULL → 400 | source to NUL | `dmax` | `ESLEMAX` 403 | `ESOVRLP` 404 | appends, `ESNOSPC` 406 if no room | 400/403/404/406 | dest reset on refusal |
| `cplus_strncat_s` (`safe_string.c:65`) | NULL → 400 | `slen` | `dmax` | `ESLEMAX` 403 | `ESOVRLP` 404 | appends `min(slen,strlen(src))` | 400/401/403/404/406 | dest reset on refusal |
| `cplus_strcmp_s` (`safe_string.c:74`) | NULL → 400, `*indicator=0` on UAF | source to NUL | `dmax` | `ESLEMAX` 403 | N/A | compares ≤`dmax` | 400/403/407 | read-only |
| `cplus_strnlen_s` (`safe_string.c:85`) | UAF → 0; delegates | bounded by `smax` | N/A | `ESLEMAX` 403 upstream | N/A | returns ≤`smax` | 403 | read-only |
| `cplus_strerror_s` (`safe_string.c:106`) | NULL → 400 | errnum | `dmax` | `ESLEMAX` 403 | N/A | truncates to `dmax-1`+NUL | 400/403/406 | dest reset on refusal |

## 3. SafeCRT `_auto` adapters (auto-resolved capacity)

Sources: `stdlib/safe/src/safe_memory.c:138-252`, `stdlib/safe/src/safe_string.c:132-256`.
These are the strongest tier: UAF check, NULL check (even at `n==0`, except
`memcmp_s_auto`), capacity resolution, `RSIZE_MAX_*` clamp, and safeclib
constraint handling. The macro forms in `safe_memory.h`/`safe_string.h` pass
`CPLUS_SAFE_OBJ_SIZE(p)` as `known_*`.

| API (source) | Null handling | Source length validation | Destination capacity | Overflow guard | Overlap behavior | Truncation behavior | Error codes | Failure atomicity |
|---|---|---|---|---|---|---|---|---|
| `cplus_memcpy_s_auto` (`safe_memory.c:138`) | NULL → `EINVAL`, NULL even at `n==0` | source not independently bounded (`smax=n`) | `min(tracked, known)`; unknown strict → EINVAL | `n>RSIZE_MAX_MEM` → `ERANGE` (`cplus_safe_report_overflow`, ESNOSPC 406 diagnostic) | upstream `ESOVRLP` 404 | `n` bytes | `EINVAL`, `ERANGE` | no write on refusal |
| `cplus_memmove_s_auto` (`safe_memory.c:167`) | same as memcpy_s_auto | same | same | same | overlap-safe | `n` bytes | `EINVAL`, `ERANGE` | no write on refusal |
| `cplus_memset_s_auto` (`safe_memory.c:196`) | NULL → `EINVAL`, NULL even at `n==0` | `n` | same | same | N/A | `n` bytes | `EINVAL`, `ERANGE` | no write on refusal |
| `cplus_memcmp_s_auto` (`safe_memory.c:224`) | **`n==0` returns 0 before NULL check**; otherwise NULL → `EINVAL`, returns `0`/`1`/`-1` | capacity checked for both operands | both via `min(tracked,known)` | `n>RSIZE_MAX_MEM` → `ERANGE` | N/A | compares ≤`n` | `EINVAL`, `ERANGE` | read-only; refusal → **-1** |
| `cplus_strcpy_s_auto` (`safe_string.c:132`) | NULL → `EINVAL`, NULL | `strlen(src)` before capacity (MEMSTR-27 applies) | fallback `len+1`; known cap wins | `RSIZE_MAX_STR` fallback bound | upstream `ESOVRLP` | through NUL | `EINVAL`, `ERANGE` (406→ERANGE) | dest reset on refusal |
| `cplus_strncpy_s_auto` (`safe_string.c:159`) | NULL → `EINVAL`; `n==0` → dest | `n` | fallback `n+1` | `RSIZE_MAX_STR` fallback bound | upstream | ISO `strncpy` + NUL | `EINVAL`, `ERANGE` | dest reset on refusal |
| `cplus_strcat_s_auto` (`safe_string.c:189`) | NULL → `EINVAL` | `strlen` of both before capacity | fallback `dlen+slen+1`, overflow-guarded (`safe_string.c:202`) | `dlen > RSIZE_MAX_STR-slen-1` → `ERANGE` | upstream | appends through NUL | `EINVAL`, `ERANGE` | dest reset on refusal |
| `cplus_strncat_s_auto` (`safe_string.c:222`) | NULL → `EINVAL`; `n==0` → dest | `strlen(dest)` before capacity | fallback `dlen+n+1` | `n >= RSIZE_MAX_STR-dlen` → `ERANGE` (`safe_string.c:238`) | upstream | appends `min(n,strlen(src))` | `EINVAL`, `ERANGE` | dest reset on refusal |

## 4. `snprintf` / `vsnprintf` (audited, not modified — outside this agent's ownership)

Plain: `libc/src/stdio/stdio.c:191` (`cplus_vsnprintf`), `:197` (`cplus_snprintf`).
Checked: `stdlib/safe/src/safe_format.c:136` (`cplus_vsnprintf_s`), `:150`
(`cplus_snprintf_s`), `:158` (`cplus_snprintf_s_checked`).

| API | Null handling | Capacity | Overflow guard | Truncation | Error codes | Atomicity |
|---|---|---|---|---|---|---|
| `cplus_snprintf`/`cplus_vsnprintf` | null `fmt`, or null `str` with `size>0` → `EINVAL`, -1; null `str` with `size==0` is the ISO size-query | `size` | none beyond host `vsnprintf` | writes ≤`size-1` chars + NUL; **returns would-be length** (ISO C) | `EINVAL` | host `vsnprintf` |
| `cplus_vsnprintf_s` | null `fmt` → `EINVAL`, -1; UAF fmt → violation; null `dest` with `dmax!=0` → `EINVAL`, -1 | `dmax` via `fmt_check_dest_bounds` | `dmax` bound + `%n` refusal | host `vsnprintf` within `dmax` | `EINVAL`/violation, -1 | host |
| `cplus_snprintf_s` | delegates to `vsnprintf_s` | `dmax` | same | same | -1 on violation | host |
| `cplus_snprintf_s_checked` | additionally refuses non-literal format (CWE-134) | `dmax` + `known_dmax` | same | same | -1 on violation | host |

Call sites *inside the owned files* use `snprintf(msg, sizeof(msg), ...)` in
`safe_memory.c:26,132` and `safe_string.c:29` — destination capacity is the exact
object size, so these are safe and were left untouched.

## 5. Allocation/deallocation entry points (audited, not modified)

Plain (`libc/src/stdlib/alloc.c:216` `cplus_malloc`, `:242` `cplus_calloc`, `:278`
`cplus_realloc`, `:340` `cplus_free_checked`, `:373` `cplus_free`); SafeCRT
(`stdlib/safe/src/safe_alloc.c:158` `malloc_s`, `calloc_s`, `realloc_s`, `:352`
`free_s`, `free_checked_s`, `aligned_alloc_s`; contracts at
`stdlib/safe/safe_alloc.h:40-61`). These feed `cplus_(safe_)remaining_capacity`,
which is why a pointer from these allocators gets a *known* capacity in §1.1/§3.

| API | Null handling | Overflow guard | Capacity metadata | Error codes | Atomicity |
|---|---|---|---|---|---|
| `cplus_malloc` / `malloc_s` | `size==0` → defined 1-byte block | `size > RSIZE_MAX_MEM` → NULL, `EINVAL` | registers byte_size, generation | `EINVAL`, `ENOMEM` | no block on failure |
| `cplus_calloc` / `calloc_s` | zero count/size → defined 1-byte block | `count*size` overflow → NULL; plain `ENOMEM`, `calloc_s` `EOVERFLOW` | registers element/size metadata | `ENOMEM`/`EOVERFLOW` | no block on failure |
| `cplus_realloc` / `realloc_s` | `ptr==NULL` → acts like malloc; `size==0` variant | `size > RSIZE_MAX_MEM` → NULL | updates existing slot | `EINVAL`, `ENOMEM` | original block left intact on refusal |
| `cplus_free` / `free_s` | NULL is a no-op | double/invalid free **refused** (generation + recent-free table) | slot freed on success | `EINVAL` (checked variants) | refused free leaves block live (leak is safer than UAF) |
| `cplus_free_checked` / `free_checked_s` | NULL → OK and clears handle | double/unowned → `EINVAL`; `free_checked_s` leaves handle, plain `free_checked` nulls `*inout` (documented ALLOC-11) | metadata cleared on success | `EINVAL` | as above |
| `aligned_alloc_s` | invalid alignment → NULL | `size > RSIZE_MAX_MEM` → NULL | tracked | `EINVAL` | no block on failure |

## 6. Reproduced defect and fix

### 6.1 Defect (CONCRETE, reproduced)

The four plain bounded memory wrappers — `cplus_memset_bounded` (`bounded.c:62`),
`cplus_memcpy_bounded` (`:69`), `cplus_memmove_bounded` (`:76`) and
`cplus_memcmp_bounded` (`:83`, pre-fix line numbers) — passed `n` to `check_capacity`
and, when capacity was **unknown**, delegated an unvalidated `n` to the raw loops.
Default mode deliberately leaves unknown capacity unchecked, so a caller-supplied
`n == SIZE_MAX` reached `cplus_memset`/`cplus_memcpy`/`cplus_memmove`/`cplus_memcmp`.
Every other length-taking API in these files already rejected `n > RSIZE_MAX_MEM`
with `EOVERFLOW` — including the wrappers' own sibling `cplus_strncpy_bounded`
(`bounded.c:122`) — and the SafeCRT `_auto` tier applies the same clamp
(`safe_memory.c:155,184,212,242`). This asymmetry is the defect.

**Reproducer** (temporary file, outside the repository, guard page via `mmap` +
`mprotect` so the fault is deterministic):

```c
region = mmap(NULL, 2*pagesize, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
mprotect(region + pagesize, pagesize, PROT_NONE);
d = region + pagesize - 8;  s = region;          /* pointer variables -> unknown BOS */
cplus_libc_set_unknown_capacity_mode(0);         /* default: unchecked unknown cap */
fork(); child: cplus_memset_bounded (d,'A',SIZE_MAX,(size_t)-1); ...
```

Observed **before** the fix (child terminated by signal):

```
memset  SIGSEGV=1
memcpy  SIGSEGV=1
memmove SIGSEGV=1
memcmp  SIGSEGV=1
```

(An earlier variant with `d == s` did not fault for memcpy/memmove because Clang
lowered the byte loop to a platform `memcpy` call whose fast path returned early
for identical pointers; using distinct addresses removes that artifact.)

**After** the fix: `SIGSEGV=0` for all four, `errno == EOVERFLOW`, guard page and
destination intact.

### 6.2 Minimal fix

Added one local helper in `libc/src/string/bounded.c:62-69` and a one-line call in
each of the four wrappers (`bounded.c:76,84,92,101`). The guard is placed **after**
`check_capacity`, preserving the established precedence: known-capacity overflow
still reports `ERANGE`; only the previously-unchecked unknown-capacity path now
reports `EOVERFLOW`. Valid operations and all other semantics are unchanged.

No upstream code was copied for this fix — the guard mirrors this project's own
existing idiom (`string.c:30`, `bounded.c:122`). No defect was reproduced in
`safe_memory.c`, `safe_string.c`, `string.c`, `extra.c`, `extra2.c`,
`musl_extra.c`, or `cplus_libc_safe.h`, so those files were **not modified**.

### 6.3 Upstream provenance (unchanged by this audit)

- `libc/src/string/musl_extra.c`: musl libc @ commit
  `7813e9119eb5fe681b6750b8a033a0490bca0941`, MIT; see
  `libc/upstream/musl/PROVENANCE.md`. Existing local adaptations: `cplus_*` names,
  C+ logging, and substitution of `cplus_strspn`/`cplus_strcspn`/`cplus_memchr`/
  `cplus_strncasecmp`. No new adaptation was introduced.
- SafeCRT adapters delegate to safeclib 3.9.1 (`VERSION "3.9.1-cplus"` in
  `scripts/build_safecrt.sh:29`), MIT; symbol-prefixed at build time. No upstream
  source was copied or modified.

## 7. Files changed by this audit

| File | Change |
|---|---|
| `libc/src/string/bounded.c` | Added `cplus_refuse_absurd_length()` (`RSIZE_MAX_MEM` guard) and one call in each of `memset_bounded`, `memcpy_bounded`, `memmove_bounded`, `memcmp_bounded`. |
| `docs/libc/SAFE_CRT_MEMORY_CONTRACT.md` | New — this document. |

No other file was created or modified. No test runner, test, `compiler/**` or
`runtime/**` file was touched. No git add/commit was performed.

## 8. Verification (exact commands and results)

Environment note: this host's `/usr/bin/clang` is Apple clang 16.0.0 and looks for
a non-existent `MacOSX27.sdk`; the literal commands therefore fail with
`'stdio.h' file not found` during the safeclib build. Prefixing
`SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk` (the installed
15.2 SDK) makes `/usr/bin/clang` work. All results below use that override and
`CPLUS_CLANG=/usr/bin/clang`; no sanitizers were enabled.

Baseline (before the fix), same commands:
`libc tests: 85 passed, 0 failed`; `crt unit tests: 80 passed, 0 failed`.

After the fix:

```
CD=/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib; cd $CD
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk CPLUS_CLANG=/usr/bin/clang
sh tests/libc/build_libc.sh
  -> .../build/libc/libcplus_hardened.a
sh scripts/build_safecrt.sh
  -> .../build/safe/libcplus_safecrt.a
sh tests/libc/run_libc_tests.sh
  -> libc tests: 85 passed, 0 failed
sh tests/crt/unit/run_unit_tests.sh
  -> crt unit tests: 80 passed, 2 failed
```

The two unit-runner failures are **not** caused by this change: they are
`test_p0_alloc_contract.c` and `test_p0_safe_crt.c`, two files created
concurrently by another agent at 05:48 during this run. They `#include
"cplus_libc.h"`, but `tests/crt/unit/run_unit_tests.sh:16-19` only adds
`-Ilibc -Ilibc/install-include` for `safe_c_api` and `audit_high`, so those two
new tests hit `fatal error: 'cplus_libc.h' file not found`. Compiled with the
missing include dirs and both libraries, they pass their own suites:

```
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 -I. -Istdlib/safe \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  -Ilibc -Ilibc/install-include tests/crt/unit/test_p0_alloc_contract.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread -o /tmp/...
  -> P0_ALLOC 85 0
  (same for test_p0_safe_crt.c) -> P0_SAFE_CRT 138 0
```

Per instruction, the runner and those tests were not modified.

Strict / no-sanitizer compile of the relevant tests (`-O2 -Wall -Wextra
-pedantic -std=c99 -Werror`, plain libc only, no `-fsanitize=*`):

```
test_string_extra.c        COMPILE_WERROR+RUN pass
test_string_extra2.c       COMPILE_WERROR+RUN pass
test_libc_safe_api.c       COMPILE_WERROR+RUN pass
verify_fix.c (temp)        verify_fix: ok   / COMPILE_WERROR+RUN pass
```

`verify_fix.c` asserts: unknown-cap + `SIZE_MAX` → `EOVERFLOW` for all four
wrappers with no destination write; known-cap + `SIZE_MAX` → `ERANGE` (precedence
preserved); and exact-fit/valid `memset`/`memcpy`/`memmove`/`memcmp` results
unchanged.
