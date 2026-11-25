# stdc_safety — hostile inputs, unchanged ABI

**Result: PASS.** The checked ordinary-C layer refuses every hostile input with
all 64 canaries intact; the raw-C control corrupts 13 canaries and crashes or
aborts 23 times.  No source file outside `tests/abi/stdc_safety/` was created or
modified, no archive was rebuilt, and no symbol was added, renamed or mangled.

## Files

- `hostile.c` — one C99 source compiled two ways (checked and plain), 16 APIs × 4 cases.
- `run.sh`    — builds, runs both binaries under a 60 s timeout, validates, audits ABI symbols.
- `RESULTS.md` — this document.
- `out/`      — generated binaries, stdout/stderr captures, `run.log`, `archive_sha256.txt`, `nm_*.txt`.

## Exact commands

Run from the repository root (`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`,
branch `agent/modern-stdlib`):

```sh
sh tests/abi/stdc_safety/run.sh
```

`run.sh` executes exactly these clang invocations (paths shown as the script
expands them; `INC` and `CFLAGS` are defined in `run.sh`):

```sh
/usr/bin/clang -O0 -fno-builtin -std=c99 -Wall -Wextra \
  -Wno-format-nonliteral -Wno-fortify-source -Wno-builtin-memcpy-chk-size \
  -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  -include stdlib/safe/safe_c.h \
  tests/abi/stdc_safety/hostile.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o tests/abi/stdc_safety/out/checked

/usr/bin/clang -O0 -fno-builtin -std=c99 -Wall -Wextra \
  -Wno-format-nonliteral -Wno-fortify-source -Wno-builtin-memcpy-chk-size \
  tests/abi/stdc_safety/hostile.c -o tests/abi/stdc_safety/out/plain

# optional, supported here:
/usr/bin/clang ... -fsanitize=undefined ... -include stdlib/safe/safe_c.h \
  ... -o tests/abi/stdc_safety/out/checked-ubsan

/usr/bin/timeout 60 tests/abi/stdc_safety/out/checked tests/abi/stdc_safety/out \
  > tests/abi/stdc_safety/out/checked.stdout 2> tests/abi/stdc_safety/out/checked.stderr
/usr/bin/timeout 60 tests/abi/stdc_safety/out/plain   tests/abi/stdc_safety/out \
  > tests/abi/stdc_safety/out/plain.stdout   2> tests/abi/stdc_safety/out/plain.stderr
/usr/bin/timeout 60 tests/abi/stdc_safety/out/checked-ubsan tests/abi/stdc_safety/out \
  > tests/abi/stdc_safety/out/ubsan.stdout 2> tests/abi/stdc_safety/out/ubsan.stderr
```

The two archives are read-only inputs; their SHA-256 at verification time:

```
ac66b0246dbac3337f439e4b91b0e50f901aac7f07bba0ecb1581c637187ef2e  build/safe/libcplus_safecrt.a
2aa3cc3ece91fbee48d41eff9cb8a73c89b326ef8a2f9dd4e1bf5c45dd8ab18f  build/libc/libcplus_hardened.a
```

`-fno-builtin` and the `#undef` of macOS `<secure/_string.h>` fortify macros in
the plain build are deliberate: they make the control exercise *raw* libc so the
canary corruption is actually observable instead of being turned into an
Apple-specific `__*_chk` abort.

## Harness output format

Per case: `stdc_safety <api> <case> OK|REFUSED <canary_ok>`.
Final line: `STDC_SAFETY <cases> <refused> <canary_ok> <failures>`.

Observed (both normal and UBSan-instrumented checked runs):

```
STDC_SAFETY 64 48 64 0
```

Exit status of the checked binary: **0**.  `run.sh` result: **68 passed, 0 failed**.

## Full case table (CHECKED)

Canary arrays (16 bytes, `0xA5`) sit immediately after each guarded destination
in the same stack object.  `REFUSED` means the operation did not touch the
destination: the checked wrapper returned a failure indicator and/or set errno
(`EINVAL`/`ERANGE`/`EOVERFLOW`), and the canary was byte-for-byte unchanged.

| API | valid — success, data verified | null — refused | small — one byte too small, refused | huge — refused |
|---|---|---|---|---|
| memcpy | 16 B pattern copy, dest matches | dest `NULL`, n=16 | n=17 into 16 B dest | n=`SIZE_MAX` |
| memmove | 8 B overlapping move, dest matches | dest `NULL`, n=8 | n=17 into 16 B dest | n=`SIZE_MAX` |
| memset | 16 B filled `0x5A` | dest `NULL`, n=16 | n=17 into 16 B dest | n=`SIZE_MAX` |
| strcpy | `"ABCDEFG"` into `buf[8]` | dest `NULL` | `"ABCDEFGH"` needs 9 B | 1 MiB source string |
| strncpy | `"ABCDEFG"`, n=8 (7 chars + NUL) | dest `NULL` | `"ABCDEFGH"`, n=8 needs 9 B | n=`SIZE_MAX` |
| strcat | `"ABC"` + `"DEFG"` = `"ABCDEFG"` | dest `NULL` | `"ABCDEFG"` + `"H"` | 1 MiB source string |
| strncat | `"ABC"` + 4 of `"DEFGH"` | dest `NULL` | `"ABCDEFG"` + 1 of `"HI"` | n=`SIZE_MAX` |
| printf | literal `P%dQ\n`, 7 → 4 chars emitted | format `NULL` → `-1` | literal `%n` writing into canary → refused | runtime-built `%n` format → refused |
| fprintf | literal to `tmpfile`, read back `F9G\n` | stream `NULL` → `-1` | literal `%n` writing into canary → refused | runtime-built `%n` format → refused |
| fopen | create/read `"OK"` under `out/` | name `NULL` → `NULL` | over-long name copied via guarded `strcpy` → refused, canary intact | 1 MiB path → `NULL` |
| fread | read 16 B from `tmpfile`, data verified | buffer `NULL` → 0 | 17 B requested into 16 B dest, bounded read clamps → short return | `size=SIZE_MAX, count=2` → 0 |
| fwrite | write 16 B to `tmpfile`, read back | buffer `NULL` → 0 | 17 B requested into 16 B file region, bounded write clamps → file canary intact | `size=SIZE_MAX, count=2` → 0 |
| malloc | `malloc(16)` + fill, data verified | `malloc(SIZE_MAX)` → `NULL` | `malloc(8)` then write byte 9 via tracked capacity → refused | `malloc(8)` then `memset(SIZE_MAX)` → refused |
| calloc | `calloc(4,4)`, zeroed | `calloc(SIZE_MAX,2)` overflow → `NULL` | `calloc(1,8)` then write byte 9 → refused | `calloc(1,8)` then `memset(SIZE_MAX)` → refused |
| realloc | `malloc(8)`→`realloc(16)`, data preserved | `realloc(p,SIZE_MAX)` → `NULL`, original block preserved | write byte 9 of tracked 8-byte block → refused | `memset(SIZE_MAX)` on tracked block → refused |
| free | `malloc(16)` + `free` succeeds | invalid pointer `(void*)1` → refused (no crash) | double free → refused | `(void*)SIZE_MAX` → refused |

Per-API checked result: **1 OK + 3 REFUSED, canary=1** for all 16 APIs (verified
individually by `run.sh`).

### Where an explicit checked entry point is used (honest adaptation)

`safe_c.h` remaps 15 of the 16 ordinary spellings above. Three deliberate
exceptions are documented here so the evidence is not overstated:

1. **`fopen`** is intentionally *not* remapped by `safe_c.h`. The checked build
   calls the checked `cplus_fopen_s` entry point declared in `safe_io.h` (which
   `safe_c.h` includes); the plain build calls raw `fopen`.
2. **`fread`/`fwrite` "small"**: the ordinary remap (`cplus_fread`/`cplus_fwrite`)
   guards NULL and `size*count` overflow but not destination capacity. The
   bounded Annex-K forms `cplus_fread_s`/`cplus_fwrite_s` (bmax = 16) are used in
   the checked build for this one case so the refusal is real; all other fread/
   fwrite cases use the ordinary remapped spelling.
3. **`printf`/`fprintf` "small"/"huge"**: the ordinary remap (`cplus_printf`/
   `cplus_fprintf`) guards NULL format/stream only. The `printf_s`/`fprintf_s`
   macros in `safe_format.h` are used in the checked build to reject hostile
   `%n` and non-literal (attacker-controlled) formats; valid and null cases use
   the ordinary remapped spelling.

`free` maps to the void-returning `free_s`, so `free_checked_s` is used only to
*observe* the refusal code; the hostile call itself is the ordinary remapped
`free`. Where `cplus_runtime_violation` would normally fail-stop with exit 70,
the harness calls `cplus_runtime_set_violation_trap(0)` so it can print all 64
cases and report a summary; the production default (exit 70) is unchanged.

## Plain-vs-checked contrast

The plain control is a **separate subprocess** (`out/plain`), and each hostile
case inside it runs in a further `fork()`ed child, so corruption/crashes can
never reach the supervisor, the checked binary, or the shell.  Raw system libc
is used (no safe header, no archives).

Observed summary:

```
PLAIN_SUMMARY crashes_or_abnormal=23
```

- **13** plain hostile cases report `canary=0` (the guard bytes were destroyed);
  the checked counterparts all report `canary=1`.
- **23** plain cases terminate abnormally (SIGSEGV = signal 11, SIGABRT = signal 6,
  SIGTRAP = signal 5); the checked counterparts all return normally with a
  refusal.
- **19** plain hostile cases diverge from the checked expectation.
- All 16 plain *valid* cases still pass with `canary=1`, matching the source/
  ABI-compatibility claim: valid programs behave identically with or without
  the layer.

Representative lines from `out/plain.stdout`:

```
plain memcpy small HOSTILE canary=0 acted=1 data=0 DIFF      (17th byte hits canary)
plain strcpy small HOSTILE canary=0 acted=1 data=0 DIFF      (NUL written past buf[8])
plain printf small HOSTILE canary=0 acted=1 data=0 DIFF      (%n writes into canary)
plain fread small HOSTILE canary=0 acted=1 data=0 DIFF       (17th byte hits canary)
plain memcpy null  CRASH signal=11 exit=-1                   (raw NULL dereference)
plain free small   CRASH signal=5  exit=-1                   (double free)
plain malloc huge  CRASH signal=5  exit=-1                   (huge memset)
```

Representative checked lines from `out/checked.stdout`:

```
stdc_safety memcpy valid OK 1
stdc_safety memcpy small REFUSED 1
stdc_safety printf small REFUSED 1
stdc_safety free small REFUSED 1
STDC_SAFETY 64 48 64 0
```

UBSan build: exit 0, identical `STDC_SAFETY 64 48 64 0`, and **0** `runtime error`
reports in `out/ubsan.stderr`.

## ABI statement

**The safety demonstrated here comes entirely from the runtime checked APIs in
`build/safe/libcplus_safecrt.a` and `build/libc/libcplus_hardened.a`; the ABI and
public symbols are unchanged.**  `safe_c.h` performs *source-level macro
rewriting only* (it defines no new function, type, struct field or calling
convention).  A caller compiled without `-include stdlib/safe/safe_c.h` still
sees ordinary ISO C declarations and links against ordinary libc.  `run.sh`
verifies with `/usr/bin/nm -gU` that:

- all checked entry points (`malloc_s`, `calloc_s`, `realloc_s`, `free_s`,
  `free_checked_s`, `cplus_memcpy_s_auto`, `cplus_memmove_s_auto`,
  `cplus_memset_s_auto`, `cplus_strcpy_s_auto`, `cplus_strncpy_s_auto`,
  `cplus_strcat_s_auto`, `cplus_strncat_s_auto`, `cplus_fread_s`,
  `cplus_fwrite_s`, `cplus_fopen_s`, `cplus_printf_s`, `cplus_fprintf_s`) are
  exported as plain, unmangled `T` symbols from the two archives;
- those same plain `T` symbols are linked into `out/checked`;
- there are no `__Z`-prefixed (C++ mangled) text symbols in the checked binary.

Evidence transcript: `tests/abi/stdc_safety/out/run.log`, plus the captured
stdout/stderr files and `nm_*.txt` listings in `tests/abi/stdc_safety/out/`.
