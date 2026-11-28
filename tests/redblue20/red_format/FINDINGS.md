# RED20 format red-team findings (agent 6/20)

Target: `stdlib/safe/safe_format.h` / `src/safe_format.c` and
`stdlib/safe/safe_io.h` / `src/safe_io.c` on branch `agent/modern-stdlib`.

Environment: macOS arm64, Apple clang 16.0.0 (clang-1600.0.26.6),
`build/safe/libcplus_safecrt.a`, `build/libcplus_core.a`.  Runtime violation
trap disabled with `cplus_runtime_set_violation_trap(0)` so refusals return
`-1`/`EINVAL` instead of `_exit(70)`.

Run: `sh tests/redblue20/red_format/run.sh` (builds two object-size variants,
forks one child per case, 60 s cap per variant).  Full run: 7 200 cases in
~22 s, exit 0.

```
RED20 format 7200 0 440 56 6704
```

Per variant: `format-first RED20 format 3600 0 40 28 3532`,
`io-first RED20 format 3600 0 400 28 3172`.

Classification is `CRASH > CORRUPT > UNEXPECTED_OK > REFUSED`; `UNEXPECTED_OK`
means a guard was bypassed, `CORRUPT` means a canary past the declared
capacity was modified.  No source/header files were edited; all artifacts are
under `tests/redblue20/red_format/`.

---

## F1 (HIGH) — `%n` filter bypass: length modifier placed before the positional index

**Genuine finding, reproducible.**  `cplus_fmt_has_percent_n()` calls
`parse_pos_dollar()` only immediately after `%` and after a `*` width.  When a
length modifier precedes the positional marker (e.g. `%h1$n`), the length
modifier is consumed, then `1$` is *not* recognized as a positional marker;
the `$` and the trailing `n` are scanned as ordinary literals.  The function
returns 0 (safe) even though libc parses `1$` as a positional argument index
and `n` as a `%n` conversion, writing through the caller-supplied pointer.

This defeats the documented Annex K guarantee ("`%n` is refused in all format
functions") in **every** printf-family adapter, including the `_checked`
entry points: `snprintf_s`, `sprintf_s`, `printf_s`, `fprintf_s`,
`vsnprintf_s`, and their checked forms.

Minimal reproducer (from `results/format_first.tsv`, case 0; same as probe 5):

```c
#include "safe_format.h"
extern void cplus_runtime_set_violation_trap(int);
int main(void) {
    volatile int target = 0x11223344;
    char buf[64];
    cplus_runtime_set_violation_trap(0);
    int rc = cplus_snprintf_s(buf, sizeof buf, "%h1$n", &target);
    /* rc = 0 (ACCEPTED), errno = 0, target = 0x11220000  <-- WRITE */
    return 0;
}
```

Confirmed accepted-and-written formats (all indices 1, 2, 3 and 10):

```
%h1$n  %hh1$n  %l1$n  %ll1$n  %z1$n  %t1$n  %j1$n  %L1$n
%h2$n  %hh2$n  %l2$n  %ll2$n  %z2$n  %t2$n  %j2$n  %L2$n
%h3$n  %hh3$n  %l3$n  %ll3$n  %z3$n  %t3$n  %j3$n  %L3$n
%h10$n %hh10$n %l10$n %ll10$n %z10$n %t10$n %j10$n %L10$n
```

Observed sentinel values: `%h1$n` → `0x11220000` (16-bit write),
`%hh1$n` → `0x11223300` (8-bit write), `%l1$n`/`%ll1$n`/`%z1$n`/`%t1$n`/
`%j1$n`/`%L1$n` → `0x00000000` (full write).  Raw `snprintf()` on the same
formats writes the same values, confirming the write is libc's `%n`
semantics, not a harness artifact.  By contrast `%1$n`, `%1$hn`, `%1$lln`,
`%n`, `%hhn` are all correctly refused (`-1`, `EINVAL`).

Evidence: 28 `UNEXPECTED_OK` cases in each variant (4 formats × 7 APIs),
e.g.:

```
0	PN	snprintf_s	cls=2	ret=0	err=0	sentinel=11220000	note=%h1$n
2	PN	sprintf_s	cls=2	ret=0	err=0	sentinel=11220000	note=%h1$n
4	PN	printf_s	cls=2	ret=0	err=25	sentinel=11220000	note=%h1$n
6	PN	vsnprintf_s	cls=2	ret=0	err=0	sentinel=11220000	note=%h1$n
8	PN	snprintf_s_checked	cls=2	ret=0	err=0	sentinel=11223300	note=%hh1$n
```

Suggested fix: after consuming length modifiers, also call
`parse_pos_dollar()` (and reject `N$` there), or simply reject a `$` anywhere
in the specifier — the current code only checks `$` at two fixed positions.
The same parser shape exists in `cplus_scanf_fmt_check()`; scanf on this libc
returns `-1` for these formats so no write was observed there, but the
scanner has the identical blind spot and should be fixed too.

---

## F2 (MEDIUM/HIGH) — header include order silently downgrades the struct-member capacity guard

**Genuine finding, reproducible.**  Both headers define the same macro
`CPLUS_SAFE_OBJ_SIZE` under `#ifndef` with *different* object-size types:

* `safe_format.h`: `__builtin_object_size(p, 1)` (nearest enclosing object —
  documented as the fix for the historical struct-member overflow),
* `safe_io.h`: `__builtin_object_size(p, 0)` (whole enclosing object).

Whichever header is included first wins for the whole translation unit.  A
file that includes `safe_io.h` before `safe_format.h` therefore gets type 0
for **both** the format and I/O macros: for a pointer to a struct member, the
compiler reports the size of the entire struct, so a `dmax`/`n` larger than
the member is accepted and the adjacent member is overwritten.

Minimal reproducer (io-first build; `results/io_first.tsv` cases 3, 11, …):

```c
#include "safe_io.h"        // defines CPLUS_SAFE_OBJ_SIZE = BOS(p,0)
#include "safe_format.h"    // #ifndef -> keeps type 0
struct S { char a[12]; char trail[16]; } s;
memset(&s, 0x5A, sizeof s);
int rc = snprintf_s(s.a, 13, "%s", "abcdefghijklmnopqrstuvwxyz");
/* io-first:  rc = 36 (>0), s.trail[0] overwritten  <-- CORRUPT */
/* format-first (safe_format.h first): rc = -1, errno = EINVAL, trail intact */
```

`fgets_s(member, n, f)` shows the same: with type 0 a read of `n = 13` into a
12-byte member is accepted and the trailing member is overwritten; with type
1 the call is refused with `ERANGE`.

Evidence: **360 CORRUPT in io-first, 0 in format-first**:

```
3	MEMBER	snprintf_s(macro/member)	cls=1	ret=36	err=0	canary=4	note=macro/member/snprintf
11	MEMBER	fgets_s(macro/member)	cls=1	ret=1	err=0	canary=4	note=macro/member/fgets
...
```

180 cases per API (snprintf_s and fgets_s).  `canary=4` = `CAN_MEMB`, i.e.
`trail[]` no longer matches `0x5A`.

Suggested fix: make both headers use the same strongest definition (type 1,
or `min(BOS(p,0), BOS(p,1))`), or rename the I/O macro so it cannot shadow
the format one.

---

## F3 (LOW/MEDIUM, design caveat) — raw unprefixed `snprintf_s`/`sprintf_s` accept a `dmax` larger than the real stack/struct object

The unprefixed entry points `cplus_snprintf_s` / `cplus_sprintf_s` pass
`known_dmax = (size_t)-1` into `fmt_check_dest_bounds()`.  For non-allocator
buffers, `cplus_safe_ptr_size()` is 0, so `cap` becomes 0 and no capacity
check is performed: a caller who passes `dmax` one or more bytes beyond the
real object gets an out-of-bounds write even though the API is documented as
"bounded".  The macro form (which supplies `__builtin_object_size`) does
refuse the same call.

Reproducer:

```c
struct S { char a[12]; char trail[16]; } m;
memset(&m, 0x5A, sizeof m);
int rc = cplus_snprintf_s(m.a, 14, "%s", "abcdefghijklmnopqrstuvwxyz");
/* rc = 36 (>0), 2 bytes written into m.trail[] */
/* snprintf_s(m.a, 14, ...) with the macro and BOS type 1 -> -1 / EINVAL */
```

Evidence: 40 CORRUPT per variant (20 `snprintf_s`, 20 `sprintf_s`,
`canary=4`, `note=capacity`).  This is inherent to the raw API's lack of
object-size information (the header assigns that role to the macro), so it is
reported with that caveat rather than as a silent contract violation.

---

## Negative results (attacks that were correctly refused)

* Non-literal formats: `cplus_snprintf_s_checked(0, ...)`,
  `cplus_sprintf_s_checked(0, ...)`, `cplus_sscanf_s_checked(0, ...)`, and
  NULL/runtime formats all returned `-1`/`EINVAL`; no bypass of
  `CPLUS_SAFE_IS_LITERAL` was found on this toolchain (`__builtin_constant_p`
  returns 0 for pointers to mutable buffers).
  `__builtin_constant_p` can be 1 for `const char *const p = "literal"`, but
  the content is then a genuine literal, so no breach.
* Positional-first `%n` (`%1$n`, `%1$hn`, `%1$lln`, `%n`, `%hhn`, `%lln`,
  `%zn`, `%jn`, `%Ln`, `%*n`, `%1$*2$n`, …) all refused.
* Flag/width tricks (`%In`, `%I'n`, `%'In`, `%I10n`, `%I*n`, `%I.3n`,
  `%lIn`, `%I1$n`, …) are accepted on Darwin, but Darwin libc treats `I` as
  an ordinary conversion, so **no write** occurred — not a finding on this
  platform (it would be one on glibc, where `I` is a flag).  Reported here
  for completeness.
* Unbounded scanf conversions: `%s`, `%[a-z]`, `%0s`, `%1$s`, `%*s`,
  `%1$*s`, `%*1$s`, `%2$*1$s`, `%999999999s`, unclosed `%[` all refused or
  performed no assignment; no canary damage.
* Streams: NULL streams, double `fclose_s`, and use-after-close
  `fprintf_s`/`fwrite_s_checked` were all refused (`EOF`/`-1`) with canaries
  intact.  No crash and no double-`fclose` reached the host.
* One-byte-too-small heap (`malloc_s(24)`) and guarded stack buffers were
  correctly refused when the checked macro/`_checked` entry point supplied
  the real capacity; only the raw-API case in F3 overflowed.
* No crashes (SIGSEGV/SIGABRT/SIGALRM) in 7 200 forked cases; no core files.

## Reproduce

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh scripts/build_safecrt.sh >/dev/null
sh tests/redblue20/red_format/run.sh
# final line: RED20 format 7200 0 440 56 6704
# details: tests/redblue20/red_format/results/{format_first,io_first}.tsv
```
