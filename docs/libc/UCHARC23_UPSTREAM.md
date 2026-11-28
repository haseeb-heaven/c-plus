# UCHARC23_UPSTREAM — C23 UTF-8 conversions (`mbrtoc8` / `c8rtomb`)

Status: **BLOCKED — no pinned musl source exists for these two functions.**
Do not fabricate an implementation. See `tests/libc/test_uchar_c23.c` (SKIP
path) and `libc/src/uchar/uchar_c23.c` (blocker marker translation unit).

This file records (a) the exact upstream investigation and its negative result,
(b) the only musl-affiliated source that exists, verbatim, for later integration
decisions, and (c) the intended `cplus_*` declarations. Per the agent contract,
the declarations are recorded **here only**; the orchestrator owns adding them
to `libc/install-include/cplus_uchar.h`.

---

## 1. Upstream investigation (exact revisions checked)

All checks performed 2026-11-22. "404" means the cgit `plain/<path>?h=<rev>`
endpoint returned HTTP 404 for that revision.

| Revision | URL checked | Result for `src/multibyte/mbrtoc8.c` / `c8rtomb.c` |
|---|---|---|
| pinned commit `7813e9119eb5fe681b6750b8a033a0490bca0941` | `https://git.musl-libc.org/cgit/musl/plain/src/multibyte/mbrtoc8.c?h=7813e9119eb5fe681b6750b8a033a0490bca0941` (and `c8rtomb.c`) | 404 / 404 (absent) |
| tag `v1.2.6` | `https://git.musl-libc.org/cgit/musl/plain/src/multibyte/mbrtoc8.c?h=1.2.6` (and `c8rtomb.c`) | 404 / 404 (absent) |
| branch `master` | `https://git.musl-libc.org/cgit/musl/tree/src/multibyte?h=master` | tree has `mbrtoc16.c`, `c16rtomb.c`, `mbrtoc32.c`, `c32rtomb.c` but **no** `mbrtoc8.c` / `c8rtomb.c` |
| branch `master` raw | `https://raw.githubusercontent.com/bminor/musl/master/src/multibyte/mbrtoc8.c` (and `c8rtomb.c`) | 404 / 404 (absent) |
| commit-message search | `https://git.musl-libc.org/cgit/musl/log/?qt=grep&q=mbrtoc8` | zero commits |
| pinned vendored tree | `libc/upstream/musl/` (`UPSTREAM_COMMIT` = `7813e9119eb5fe681b6750b8a033a0490bca0941`) | no `src/multibyte/` directory at all |

`master` HEAD at check time: "regex: avoid int overflow in regcomp" (Szabolcs
Nagy, ~6 weeks old); newest tag `v1.2.6` (~8 months old). musl's `<uchar.h>`
on master still declares only the C11 UTF-16/UTF-32 family.

**Conclusion:** there is no commit or tag of musl that ships these two
functions. The task's "copy pinned musl source" method cannot be satisfied for
this pair, exactly as the pre-existing `libc/src/uchar/uchar.c` comment already
records ("no musl source at the pinned commit").

---

## 2. The only musl-affiliated source that exists (unmerged patch)

The implementations were proposed to the musl mailing list by Jens Gustedt in
2023 but were **never merged** into musl. They are the authoritative musl
implementation of these functions, MIT-licensed as part of musl, but are not a
"pinned musl revision".

- Cover letter `[C23 string conversion 0/3]`, 2023-05-26,
  Message-Id `<cover.1684932960.git.Jens.Gustedt@inria.fr>` —
  https://www.openwall.com/lists/musl/2023/05/26/2
- Patch `[C23 string conversion 2/3]`, 2023-05-26 11:25:44 +0200,
  Message-Id `<a4faa3c9cef8b48d685f059ab8425ec3fd5087db.1684932960.git.Jens.Gustedt@inria.fr>` —
  https://www.openwall.com/lists/musl/2023/05/26/4
- Resend `[C23 string conversion 1/2]`, 2023-05-31 16:01:43 +0200,
  Message-Id `<976b592b08c82bd6b1f8834904071db6602f3885.1685534402.git.Jens.Gustedt@inria.fr>` —
  https://openwall.com/lists/musl/2023/05/31/33
- Review with maintainer objections (C-locale defect, `_Alignas` portability),
  2023-06-25/26 — https://www.openwall.com/lists/musl/2023/06/25/5 and
  https://www.openwall.com/lists/musl/2023/06/26/1
- Git blob ids from the patch: `src/multibyte/c8rtomb.c` = `8645e112…`,
  `src/multibyte/mbrtoc8.c` = `0c73fa10…`.
- License: MIT (musl `COPYRIGHT`, vendored at `libc/upstream/musl/COPYRIGHT`).
  The patch diff itself carries no separate copyright header.

The two identical patch revisions are reproduced verbatim in §4.

---

## 3. Intended `cplus_*` declarations (add to `cplus_uchar.h` by orchestrator)

```c
/* C23 <uchar.h> UTF-8 conversions (currently BLOCKED -- see this document).
 * Return conventions mirror mbrtoc16/c16rtomb: (size_t)-1 + errno==EILSEQ on
 * encoding error, -2 incomplete input, -3 buffered output under mbrtoc8, 0 a
 * null character. ps == NULL selects the internal/host state. char8_t is the
 * unsigned 8-bit character type (unsigned char). */
size_t cplus_mbrtoc8(char8_t *CPLUS_UCHAR_RESTRICT pc8,
                     const char *CPLUS_UCHAR_RESTRICT s,
                     size_t n, mbstate_t *CPLUS_UCHAR_RESTRICT ps);
size_t cplus_c8rtomb(char *CPLUS_UCHAR_RESTRICT s,
                     char8_t c8, mbstate_t *CPLUS_UCHAR_RESTRICT ps);
```

`CPLUS_UCHAR_RESTRICT` expands to `restrict` in C and to nothing in C++ (as in
the existing header). The test uses local externs with `unsigned char` in place
of `char8_t` (the header defines `char8_t` as `unsigned char`), so it compiles
in C99 mode.

---

## 4. Why the unmerged patch was not copied verbatim

Copying the patch and applying only the four sanctioned integration edits
(rename, `cplus_mbrtowc`/`cplus_wcrtomb`, `cplus_uchar.h`, `cplus_libc_log`)
would still produce an incorrect build here:

1. **Not a pinned musl revision.** It exists only as a mailing-list patch that
   upstream declined; the task requires pinned musl source and forbids
   fabrication.
2. **Depends on musl-internal state encoding.** `mbrtoc8_slow` casts the
   `mbstate_t *` to `unsigned *`, relies on musl `mbrtowc` storing an incomplete
   character with the high bit set, and on musl `wcrtomb` ignoring its state
   argument. The repo's `cplus_mbrtowc`/`cplus_wcrtomb` delegate to the host
   libc, whose `mbstate_t` layout and state encoding are not guaranteed to
   match. (`cplus_mbrtoc16` already makes a similar assumption, but that is
   pre-existing orchestrator code, not a licence to extend it.)
3. **Non-conforming NULL source.** `__mbrtoc8` reads `s[0]` before any NULL
   check, so `mbrtoc8(pc8, NULL, n, ps)` (ISO C: equivalent to `s == ""`)
   would fault. C23 also needs a defined zero-width reset path.
4. **Build- and portability-fixes beyond "minimal integration".** The patch
   needs `weak_alias` (musl-only), `__attribute__((__noinline__))`, `internal.h`
   for `INT_MIN`, C11 `_Alignas` (build uses `-std=c99`), and a union rewrite
   flagged by the maintainer. Adding those is algorithm/host adaptation, not a
   rename.
5. **Known upstream defects.** Rich Felker identified a C-locale correctness
   bug (arbitrary bytes accepted as UTF-8) and questioned `_Alignas`; the patch
   was never finalised.

Therefore no `cplus_mbrtoc8` / `cplus_c8rtomb` body is compiled. This matches
the repo's existing C23-gap policy (`tests/libc/test_c23_small_gaps.c`):
print the exact blocker, SKIP, and fabricate nothing.

---

## 5. Verbatim upstream patch source (reference only, not compiled)

### 5.1 `src/multibyte/c8rtomb.c` (blob `8645e112…`)

```c
#include <uchar.h>
#include <wchar.h>

__attribute__((__noinline__))
static size_t c8rtomb_slow(char *__restrict s, unsigned char c8, mbstate_t *__restrict st)
{
	// We need an internal state different from mbrtowc.
	static mbstate_t internal_state;
	if (!st) st = &internal_state;
	wchar_t wc;

	// mbrtowc has return values -2, -1, 0, 1.
	size_t res = mbrtowc(&wc, (char const*)&c8, 1, st);
	switch (res) {
	// our implementation of wcrtomb ignores the state
	case  1: res = wcrtomb(s, wc, 0); break;
	case  0: res = 1; if (s) *s = 0;  break;
	}
	return res;
}

static size_t __c8rtomb(char *__restrict s, unsigned char c8, mbstate_t *__restrict st)
{
        if (st && !*(unsigned*)st && (c8 < 0x80)) {
		if (s) *s = c8;
		return 1;
	}
	return c8rtomb_slow(s, c8, st);
}

weak_alias(__c8rtomb, c8rtomb);
```

### 5.2 `src/multibyte/mbrtoc8.c` (blob `0c73fa10…`)

```c
#include <stdlib.h>
#include <wchar.h>
#include <uchar.h>
#include <errno.h>
#include "internal.h"

__attribute__((__noinline__))
static size_t mbrtoc8_slow(unsigned char *__restrict pc8, const char *__restrict src, size_t n, mbstate_t *__restrict st)
{
	static unsigned internal_state;
	wchar_t wc;
	unsigned* pending = st ? (void*)st : &internal_state;

	// The high bit is set if there is still missing input. If it
	// is not set and the value is not zero, a previous call has
	// stacked the missing output bytes withing the state.
	if ((-*pending) > INT_MIN) {
		if (pc8) *pc8 = *pending;
		(*pending) >>= 8;
		return -3;
	}

	// mbrtowc has return values -2, -1, 0, 1, ..., 4.
	size_t res = mbrtowc(&wc, src, n, (void*)pending);
	if (res <= 4) {
		_Alignas(unsigned) unsigned char s[8] = { 0 };
		// Write the result bytes to s over the word boundary
		// as we need it. Our wcrtomb implementation ignores
		// the state variable, there will be no errors and the
		// return value can be ignored.
		wcrtomb((void*)(s+3), wc, 0);
		// Depending on the endianess this maybe a single load
		// instruction. We want to be sure that the bytes are
		// in this order, and that the high-order byte is 0.
		*pending = (0u|s[4])<<000 | (0u|s[5])<<010 | (0u|s[6])<<020 | (0u|s[7])<<030;
		if (pc8) *pc8 = s[3];
	}
	return res;
}

static size_t __mbrtoc8(unsigned char *__restrict pc8, const char *__restrict src, size_t n, mbstate_t *__restrict st)
{
	unsigned char *s = (void*)src;
	if (st && !*(unsigned*)st && (s[0] < 0x80u)) {
		if (pc8) *pc8 = s[0];
		return s[0]>0;
	}
	return mbrtoc8_slow(pc8, src, n, st);
}

weak_alias(__mbrtoc8, mbrtoc8);
```

---

## 6. Verification performed by the closing agent

- Direct compile of `libc/src/uchar/uchar_c23.c` with
  `-std=c99 -Wall -Wextra -pedantic`: clean (blocker-marker TU only).
- `tests/libc/test_uchar_c23.c` compiles and runs: prints the SKIP blocker and
  `uchar_c23: ok`; logger parity is exercised even in the SKIP case.
- UBSan build/run of both: no diagnostics.
- No `cplus_mbrtoc8` / `cplus_c8rtomb` symbol is emitted (nothing fabricated).

**Closed vs blocked: BLOCKED.** Re-close only if a pinned musl revision that
contains these functions appears, or if the orchestrator explicitly authorises
integrating the unmerged patch with the host-adaptation edits listed in §4.
