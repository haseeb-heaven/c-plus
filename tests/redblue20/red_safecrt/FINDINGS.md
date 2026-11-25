# RED20 safecrt — FINDINGS

## 0. Scope, method, honesty

Attacked the checked SafeCRT layer in `build/safe/libcplus_safecrt.a` linked with
`build/libc/libcplus_hardened.a`. 28 APIs were covered: the raw and `_auto`
memory adapters (`memcpy/memmove/memset/memcmp/memzero`), the raw and `_auto`
string adapters (`strcpy/strncpy/strcat/strncat/strcmp/strnlen/strtok/strerror`),
and the checked allocator / query surface (`malloc_s`, `calloc_s`, `realloc_s`,
`free_s`, `free_checked_s`, `free_checked_generation_s`, `aligned_alloc_s`, the
length/meta queries).

`harness.c` runs **3136 deterministic fork-per-case tests** (28 APIs × 8 attack
families × 14 parameter variants). Each child gets a 2-page mmap: page 0 RW with
the last 256 bytes past the declared capacity filled with `0xC3` canaries, page 1
`PROT_NONE`. A small overflow is CORRUPT (canary changed), a large one is CRASH
(signal, normally SIGBUS=10 on this macOS/arm64 host). Violation trap is disabled
and the constraint handlers are no-ops so refusals return codes instead of
`_exit(70)`; this is required to *observe* corruption. Real execution time was
~13 s (well inside the 60 s cap).

**Exact output (two consecutive runs byte-identical):**

```
RED20 safecrt 3136 30 33 1175 1898
```

| class | count | genuine finding? |
|---|---:|---|
| CRASH (signal) | 30 | **YES — gap, caller-supplied bound (§3)** |
| CORRUPT (canary changed) | 33 | **YES — gap, caller-supplied bound (§2)** |
| UNEXPECTED_OK | 1175 | **14 are a genuine logic bug (§1); the rest benign (§4)** |
| REFUSED | 1898 | — |

No source/header was edited; no git operation performed.

---

## 1. GENUINE LOGIC BUG — `cplus_memcmp_s_auto` treats NULL operands as "equal" at `n == 0`

**Severity:** medium (semantic contract violation / wrong success result).
**File:** `stdlib/safe/src/safe_memory.c`, `cplus_memcmp_s_auto()`.

In `cplus_memcmp_s_auto()` the `n == 0` early return is placed **before** the
`!s1 || !s2` null check:

```c
if (cplus_safe_reject_uaf(s1, ...) || cplus_safe_reject_uaf(s2, ...)) return -1;
if (n == 0) { cplus_safe_stat_record("memcmp_s", 1); return 0; }   /* <-- here */
if (!s1 || !s2) { errno = EINVAL; ... }
```

Every sibling auto wrapper refuses NULL even at `n == 0`
(`cplus_memcpy_s_auto`, `cplus_memmove_s_auto`, `cplus_memset_s_auto`,
`cplus_strncpy_s_auto`, `cplus_strncat_s_auto` all check NULL first, see the
explicit comment at `safe_memory.c:275`), and the raw `cplus_memcmp_s` returns
400 (ESNULLP) for NULL operands. This one returns **0 ("equal") with errno
untouched**, so a caller that compares two absent buffers believes they match.

**Evidence in the campaign:** ids 897–910, all `api=memcmp_s_auto att=NULL_ARG`,
are `ok=1` with intact canaries. The NULL cases force `n=0`, which is exactly
the buggy path.

**Reproducer:**

```sh
cd tests/redblue20/red_safecrt && ./run.sh >/dev/null   # builds repro
./repro null
```

Observed:

```
memcmp_auto(NULL,NULL,n=0) = 0 (errno=0)
memcmp_auto(buf,NULL,n=0)  = 0 (errno=0)
```

Expected: non-zero / error sentinel and `errno=EINVAL`.

**Fix direction (not applied — out of my file ownership):** move the `!s1 ||
!s2` block above the `n == 0` early return in `cplus_memcmp_s_auto()`.

---

## 2. GAP — checked APIs report SUCCESS while writing past the declared capacity when `dmax > real capacity`

**Severity:** medium (documented boundary, but the API lies about success).
**APIs:** `cplus_memset_s`, `cplus_memzero_s`, `cplus_strerror_s`.

33 CORRUPT cases. In every one the first canary byte (`d[cap]`) is overwritten
and the API still returns 0:

```
239 memset_s   ONE_OVER cap=41  n=42 dm=42 CORRUPT [rc=0 ok=1 first_bad=0]
463 memzero_s  ONE_OVER cap=67  n=67 dm=68 CORRUPT [rc=0 ok=1 first_bad=0]
1808 strerror_s ONE_OVER cap=12 n=5  dm=13 CORRUPT [rc=0 ok=1 first_bad=0]
```

(`cap`/`n`/`dm` printed from the generated case; for ONE_OVER the child sets
`dm=cap+1`, `n=cap+1`.) `cplus_safe_refuse_unproven_dmax()` in
`safe_memory.c:57` only clamps when the capacity is *provable* (tracked
`malloc_s` metadata or `__builtin_object_size`); for a raw/stack/foreign
destination it is unknown, so safeclib is called with the caller's `dmax` and
writes it. The error-path zeroing that the 2026-11-23 patch removed
(`safe_mem_constraint.h::handle_mem_error`) is not involved — this is the
**success** path.

**Caveat (honest):** triggering this requires the caller to pass `dmax` (and for
memset, `n`) larger than the real object, i.e. a caller-side bound lie. This is
the "BOS unknown" boundary already documented in `safe_memory.h:45` and
`HARDENING.md`. It is reported because the checked API returns **success** while
memory is destroyed, which is more dangerous than an explicit refusal.

**Reproducer:** `./repro oob`

```
memset_s(dmax=65,n=65, real cap=64) rc=0 canary_intact=0 first_bad_byte=41
```

---

## 3. GAP — legal-but-oversized scan bound over-reads an unterminated source into a guard page (30 CRASH)

**Severity:** medium (caller-supplied bound, but no protection from the "checked" layer).
**APIs:** `strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`, `strcmp_s`, `strnlen_s`, `strtok_s`.

All 30 crashes are `signal=10` (SIGBUS on macOS arm64 `PROT_NONE`) from children
where the source has no NUL before the guard page and the child supplies
`dmax = 4096` — which is `RSIZE_MAX_STR` in this safeclib build, i.e. the
**largest bound the layer accepts** (larger values are refused with 403). The
layer accepts the in-range bound and the bounded scan walks straight off the
object:

```
1066 strcpy_s      UNTERMINATED cap=190 n=32 dm=45  CRASH [signal=10]
1075 strcpy_s      UNTERMINATED cap=27  n=23 dm=9   CRASH [signal=10]
...  strnlen_s / strtok_s UNTERMINATED with dm forced to 4096 ...
```

`safe_string.h:38` advertises that the `_auto` wrappers derive a scan ceiling
from the destination capacity so unterminated input is "refused (NULL+ERANGE)
instead of being over-read"; the raw `cplus_str*_s` adapters have no such
ceiling at all, and the `_auto` ceiling is capped by the *destination*
capacity, not the *source's* proven capacity. An unterminated source therefore
still over-reads here.

**Caveat (honest):** the caller supplied a bound larger than the real source.
This is a caller bound-lie, not a defect in the constraint arithmetic; it is
listed because the task explicitly targets unterminated sources and because the
"checked" label creates a misleading expectation.

**Reproducer:** `./repro unterm`

```
strnlen_s(unterminated, smax=4096) -> SIGBUS (exit 138 = 128+10)
```

---

## 4. UNEXPECTED_OK triage (1175 cases)

Nearly all are mechanical: the attack family is simply not applicable to that
API, so the API's normal success is correct.

| group | examples | why benign |
|---|---|---|
| allocator no-op / valid calls | `malloc_s` (98), `calloc_s` (84), `aligned_alloc_s` (84), `realloc_s` (56), `free_checked_s` (28) | `malloc_s(0)`, `calloc_s(0,0)`, `realloc_s(NULL,n)`, `free_checked_s(NULL)` are defined successful operations; `UNTERMINATED`/`OVERLAP`/`FOREIGN_PTR` have no meaning for an allocator. |
| query APIs | `strnlen_s` (72), `strcmp_s` (65), `memcmp_s` (42) | `strnlen_s` returns a count; `strcmp/memcmp` of two equal buffers returns 0 ("equal"), which the mechanical classifier counts as success. |
| valid overlapping memmove / unterminated-but-bounded copies | `memmove_s_auto`, `memset_s_auto`, `strcpy_s_auto` | memmove overlap is legal; bounded short copies terminate safely. |
| **14 × `memcmp_s_auto NULL_ARG`** | ids 897–910 | **not benign — genuine finding §1.** |

The 14 §1 cases are the only UNEXPECTED_OK entries judged to be a real defect.

---

## 5. Areas with NO finding in this campaign

- **Double-free / UAF detection:** 100% of `DOUBLE_FREE_UAF` cases for the memory
  and string adapters were REFUSED — `cplus_safe_ptr_was_freed()` fired (the
  `[CPLUS-SAFE] ... use-after-free refused` diagnostics in `harness.stderr`).
  `free_checked_s`, `free_checked_generation_s` and `realloc_s` refused repeat
  frees, foreign pointers, interior pointers, zero generations and stale
  generations without touching the heap.
- **Foreign pointers:** `free_s`/`realloc_s`/query APIs refused stack, raw
  `malloc()`, interior and one-past-end pointers with EINVAL / NULL / 0.
- **Alignment lies:** `aligned_alloc_s(0|3|6|SIZE_MAX, n)` returned NULL; every
  non-NULL result satisfied `p % alignment == 0` (the harness explicitly checks
  this and would force CORRUPT otherwise).
- **Tracked-capacity clamping:** `_auto` wrappers with an honest `known_dmax`
  refused `ONE_OVER` and `SIZE_MAX` early (`buffer overflow refused ...
  ESNOSPC 406`), confirming the early-clamp fix for the older
  `tests/redteam/memstr` finding is effective when a capacity is provable.
- The 2026-11-23 safeclib patch that removed the error-path `memset(dest,0,dmax)`
  holds: oversized-`dmax` + constraint-violation cases no longer corrupt memory.

---

## 6. Files / reproduction

```sh
cd tests/redblue20/red_safecrt
./run.sh
```

- `harness.c` — fork-per-case harness, canary + `PROT_NONE` guard page, 8 attack
  families, classification and per-case log.
- `run.sh` — symbol check, deterministic build, `timeout 60` run, triage helpers,
  standalone reproducers, digest.
- `repro.c` — `./repro null | oob | unterm`.
- `results/scenarios.log` — all 3136 rows (`id api attack cap n dm outcome [observed]`).
- `results/harness.{stdout,stderr}`, `results/digests.sha256` — raw evidence.

**Exit status:** the harness exits 0 (it ran to completion; findings are reported,
not hidden). No source/header edits, no git add/commit.
