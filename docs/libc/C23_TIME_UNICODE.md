# C23_TIME_UNICODE — three C23 time/Unicode APIs implemented

Scope: the three entries still classified `MISSING` in
`docs/libc/iso_c_inventory.json`:

| API | Header | Standard signature (project prefix added) |
| --- | --- | --- |
| `timespec_getres` | `<time.h>` | `int timespec_getres(struct timespec *ts, int base);` |
| `mbrtoc8` | `<uchar.h>` | `size_t mbrtoc8(char8_t *restrict pc8, const char *restrict s, size_t n, mbstate_t *restrict ps);` |
| `c8rtomb` | `<uchar.h>` | `size_t c8rtomb(char *restrict s, char8_t c8, mbstate_t *restrict ps);` |

This task created ONLY the four owned files (no build script, runtime/,
compiler/, third_party/ or pre-existing source was modified; no git
add/commit was performed):

| File | Role |
| --- | --- |
| `libc/src/stdlib/c23_time_unicode.c` | Implementations (3 external symbols) |
| `libc/install-include/cplus_c23_time_unicode.h` | Standalone public header |
| `tests/libc/test_c23_time_unicode.c` | Deterministic test, prints `c23_time_unicode: <checks> <failures>` |
| `docs/libc/C23_TIME_UNICODE.md` | This document |

The public names use the repository-wide `cplus_` prefix, but every signature
has the **exact C23 shape** (return type, parameter types, `restrict`
placement). `char8_t` is modelled as `unsigned char` in C modes, exactly as in
`libc/install-include/cplus_uchar.h`.

---

## 1. Host and toolchain

- `/usr/bin/clang` — Apple clang version 16.0.0 (clang-1600.0.26.6), target
  `arm64-apple-darwin27.2.0`.
- SDK: `/Library/Developer/CommandLineTools/SDKs/MacOSX.sdk` (MacOSX15.2.sdk).
- `sizeof(mbstate_t) == 128`, `sizeof(wchar_t) == 4`, `MB_LEN_MAX == 6`,
  `TIME_UTC == 1`, `setlocale(LC_ALL, "en_US.UTF-8")` succeeds.
- Date of probes: 2026-11-23.

## 2. Exact host probes (all three proved absent)

Probe source (stdin, `-x c -`):

```c
#include <time.h>
#include <wchar.h>
int main(void){
  struct timespec ts; char8_t c8; char buf[8]; mbstate_t ps = {0};
  (void)timespec_getres(&ts, TIME_UTC);
  (void)mbrtoc8(&c8, "x", 1, &ps);
  (void)c8rtomb(buf, (char8_t)'x', &ps);
  return 0;
}
```

Command:
```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -x c - -o /dev/null
```

Exact diagnostics (abridged to the relevant lines, reproduced verbatim):

```
error: unknown type name 'char8_t'; did you mean 'wchar_t'?
error: call to undeclared function 'timespec_getres'; ISO C99 and later do
      not support implicit function declarations [-Wimplicit-function-declaration]
      note: did you mean 'timespec_get'? (_time.h:199)
error: call to undeclared function 'mbrtoc8'; ISO C99 and later do not
      support implicit function declarations [-Wimplicit-function-declaration]
      note: did you mean 'mbrtowc'? (_wchar.h:108)
error: call to undeclared function 'c8rtomb'; ISO C99 and later do not
      support implicit function declarations [-Wimplicit-function-declaration]
      note: did you mean 'wcrtomb'? (_wchar.h:123)
```

`-std=c2x` fails similarly (`error: use of undeclared identifier ...`). SDK-wide
grep of `time.h`/`wchar.h` finds no declaration of the three names.

## 3. `timespec_getres` resolution evidence

Probe (200000 calls):

```c
#include <stdio.h>
#include <time.h>
int main(void){
  struct timespec a,b,r; int i; long min=1000000000L; int mod=0;
  timespec_get(&a,TIME_UTC);
  for(i=0;i<200000;i++){
    timespec_get(&b,TIME_UTC);
    long d=(long)(b.tv_sec-a.tv_sec)*1000000000L+(b.tv_nsec-a.tv_nsec);
    if(d>0 && d<min) min=d;
    if(b.tv_nsec%1000L) mod=1;
    a=b;
  }
  printf("min positive delta ns = %ld\n", min);
  printf("any nsec not multiple of 1000 = %d\n", mod);
  clock_getres(CLOCK_REALTIME,&r);
  printf("clock_getres(CLOCK_REALTIME) = {%ld,%ld}\n",
         (long)r.tv_sec,(long)r.tv_nsec);
  return 0;
}
```

Output (verbatim):

```
min positive delta ns = 1000
any nsec not multiple of 1000 = 0
clock_getres(CLOCK_REALTIME) = {0,1000}
```

Implementation: call the host's `clock_getres(CLOCK_REALTIME, ts)` when it
succeeds with a positive sub-second value; otherwise use the documented
constant `tv_sec = 0, tv_nsec = 1000` (1 microsecond). `ts == NULL` and
`base != TIME_UTC` return 0 with `errno == EINVAL`.

**Documented limitation:** the constant reflects macOS's `gettimeofday(2)`
microsecond granularity. If a future host changes `timespec_get(TIME_UTC)`
resolution, this constant must be revisited. On glibc >= 2.38 (or with the
opt-in `CPLUS_C23_HOST_HAS_TIMESPEC_GETRES`) this file emits **no**
`cplus_timespec_getres` definition, so the host-first wrapper in
`libc/src/c23_small_gaps.c` remains the single provider (no duplicate symbol).

## 4. `mbrtoc8` / `c8rtomb` design and semantics

- **Backends.** `mbrtowc` decodes multibyte input to a `wchar_t` code point;
  `wcrtomb` produces multibyte output from a decoded code point. The mapping
  between a code point and its 1–4 UTF-8 code units is a **from-standard**
  RFC 3629 / Unicode encoder written here (no vendored code).
- **State.** The opaque `mbstate_t` (128 bytes here) is owned entirely by this
  implementation — it is never passed to `mbrtowc`/`wcrtomb` (those get a
  fresh local state). A private struct (with a magic word for
  initial-state detection) is `memcpy`'d into/out of the caller's object;
  zeroing the caller's state resets it. `ps == NULL` selects a dedicated
  static state. A compile-time assertion requires
  `sizeof(private state) <= sizeof(mbstate_t)`.
- **Return conventions** (mirror `mbrtoc16`/`c16rtomb`):
  - `mbrtoc8`: `0` null char; positive = input bytes consumed; `(size_t)-2`
    incomplete input; `(size_t)-3` next buffered UTF-8 code unit (no input
    consumed); `(size_t)-1` + `errno == EILSEQ` encoding error.
  - `c8rtomb`: `1` null char; positive = multibyte bytes stored;
    `(size_t)-2` incomplete UTF-8 sequence; `(size_t)-1` + `EILSEQ` error.
- **Surrogate handling:** UTF-8 never encodes surrogate code points; U+D800–
  U+DFFF (e.g. `ED A0 80`) are rejected with `EILSEQ`. There are no UTF-16
  surrogate *pairs* in UTF-8, so this is the correct analogue.

**Documented limitations:** (a) UTF-8 code-unit accumulation is performed
locally, so copying an `mbstate_t` mid-conversion does not duplicate the
private state (the usual restriction on conversion-state copies); (b) the
`wcrtomb` backend uses a fresh state, which is correct for the stateless
UTF-8/C locales this host provides but would need adaptation for a stateful
locale; (c) a `cplus_libc_log` entry call is emitted per API, matching the
hardened-libc convention.

## 5. Provenance / honesty

- **No upstream source existed to copy.** `libc/upstream/musl` at the pinned
  commit `7813e9119eb5fe681b6750b8a033a0490bca0941` has no `src/multibyte/`
  directory; tag `v1.2.6` and `master` also lack `mbrtoc8.c`/`c8rtomb.c`
  (`docs/libc/UCHARC23_UPSTREAM.md`). No `third_party/` tree contains them,
  and no pinned source implements `timespec_getres`
  (`docs/libc/C23_SMALL_UPSTREAM.md`). The only musl-affiliated source is an
  **unmerged 2023 mailing-list patch** that depends on musl-internal
  `mbstate_t` encoding and has a maintainer-flagged C-locale defect; it was
  **not** copied.
- Therefore these three bodies are **from-standard originals**, not a
  verbatim vendor import and not a disguised copy. Their behaviour was
  derived from the ISO C contracts and RFC 3629, with the host `mbrtowc` /
  `wcrtomb` / `clock_getres` used only as runtime backends.
- No algorithm is fabricated: every non-trivial conversion step is delegated
  to a host backend or to the published UTF-8 bit layout.
- Earlier blocker documents (`C23_REMAINDERS_CLOSURE.md`,
  `UCHARC23_UPSTREAM.md`, `cplus_c23_remainders.h`) still accurately describe
  the *host SDK*; this change adds an explicitly from-standard backend and does
  not edit those files (outside this task's ownership).

## 6. Build / verification commands and exact results

Standalone strict compile (source + logger + test), as required:

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic \
  -Ilibc/install-include -Ilibc \
  libc/src/stdlib/c23_time_unicode.c libc/log/log.c \
  tests/libc/test_c23_time_unicode.c -o /tmp/test_c23_time_unicode
timeout 30 /tmp/test_c23_time_unicode
```

Result (verbatim):

```
c23_time_unicode: 102 0
```

Exit status: `0`. With an intentional failure the binary returns `1` (the
`return failures ? 1 : 0` contract in `main`).

Strict compiles across standards, all with `-Wall -Wextra -pedantic`
(no diagnostics):

```
c99 source: OK      c11 source: OK      c17 source: OK      c2x source: OK
c99 header: OK      c11 header: OK      c17 header: OK      c2x header: OK
```

C++ header standalone checks also pass (`clang++ -std=c++17` and
`-std=c++20`, `-Wall -Wextra -pedantic`).

UBSan (`-fsanitize=undefined`) run: `c23_time_unicode: 102 0`, exit `0`.
ASan could not be used: `/usr/bin/clang -fsanitize=address` aborts before
`main` with `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189`
— an environment/toolchain issue unrelated to this code (the non-ASan and
UBSan runs both pass).

Archive-integration simulation (parent will wire the source into the build):

```sh
# all libc sources from tests/libc/build_libc.sh plus log.c and this new file
# compiled into /tmp/c23tu_build/libcplus.a, then:
/usr/bin/clang -std=c99 -Wall -Wextra -pedantic -Ilibc/install-include -Ilibc \
  tests/libc/test_c23_time_unicode.c /tmp/c23tu_build/libcplus.a -lm -pthread \
  -o /tmp/test_archive
timeout 30 /tmp/test_archive
```

Result: `c23_time_unicode: 102 0`, link OK. `nm -gU` of the archive reports
exactly one definition each of `_cplus_timespec_getres`, `_cplus_mbrtoc8`,
`_cplus_c8rtomb` — no duplicate symbols.

## 7. `nm` evidence — new symbols only

```
$ nm -gU /tmp/final_c99.o
0000000000000830 T _cplus_c8rtomb
0000000000000108 T _cplus_mbrtoc8
0000000000000000 T _cplus_timespec_getres
```

Only these three global text symbols are emitted by
`libc/src/stdlib/c23_time_unicode.c`; all helpers are `static`.

## 8. Test coverage

`tests/libc/test_c23_time_unicode.c` runs 102 deterministic checks covering:

- `timespec_getres`: `TIME_UTC` success, exact `{0,1000}` resolution,
  `NULL` destination, unsupported base.
- ASCII roundtrip both directions; null character; length-only (`pc8 == NULL`,
  `s == NULL`) queries; `n == 0`.
- 2-byte (`C3 A9`), 3-byte (`E2 82 AC`) and 4-byte (`F0 9F 98 80`) UTF-8:
  incremental and single-shot decoding, `(size_t)-2` incomplete input,
  `(size_t)-3` buffered code units.
- Invalid bytes (`FF`), encoded surrogates (`ED A0 80`), `EILSEQ`.
- State reset by zeroing `mbstate_t`; multiple characters in one call;
  `ps == NULL` internal state.
- Full `c8rtomb` → `mbrtoc8` roundtrip for 2/3/4-byte sequences.

Multibyte cases require `en_US.UTF-8`; the test verifies `setlocale` and prints
a clear note (and skips only those cases) if it is unavailable. On this host
all cases run.
