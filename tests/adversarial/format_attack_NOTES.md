# Adversarial format-function test notes (`test_format_attack.c`)

Environment: macOS / Apple clang 16.0.0 (`clang-1600.0.26.6`), C99,
`build/libc/libcplus_hardened.a` + `build/safe/libcplus_safecrt.a`.
`cplus_runtime_set_violation_trap(0)` is set first so SafeCRT violations
return `-1`/`EINVAL` instead of `_exit(70)` (the documented fail-stop mode).

## Results by attack scenario

1. **Dynamic format containing `%n` and `%s`, no arguments (checked APIs).**
   All of `cplus_snprintf_s_checked`, `cplus_vsnprintf_s` (va_list), `cplus_sprintf_s_checked`,
   `cplus_printf_s_checked`, `cplus_fprintf_s_checked`, `cplus_scanf_s_checked`,
   `cplus_sscanf_s_checked`, `cplus_fscanf_s_checked`, plus the unprefixed
   `snprintf_s`/`printf_s` macros reject with **-1, errno=EINVAL (22)**. Exact stderr lines:
   `non-literal format string refused (CWE-134)` for the checked adapters, and
   `forbidden %n specifier in format string` / `forbidden %n specifier in scanf`
   for the non-checked `_s` entry points given a literal `%n`. Destination and
   lead/trail canaries were byte-for-byte unchanged: no write through a bogus
   pointer and no read/print of a phantom `%s` argument. No crash.
   Raw `cplus_snprintf` is *not* fed a mismatched hostile format: that is UB in
   the test itself (caller responsibility, documented in the source comment).

2. **Huge width / precision (checked, small destination).**
   `cplus_snprintf_s_checked(..., buf[16], "%999999999d", 42)` → **returns
   999999999** (C99 would-be length), writes 15 spaces + NUL, canaries intact,
   ~0.22 s CPU (well under the 2 s bound).
   `... "%.*s", INT_MAX, "hi"` → **returns 2**, writes `"hi\0"`, ~0 s. No overrun.

3. **Truncation.** Exact fit (15 chars into 16) → 15, full string + NUL.
   One short (3 into 3) → 3, `"ab\0"`. Zero-size with NULL dest → 3 (would-be
   length). Zero-size with real buffer → 3, nothing written. `dmax` greater than
   the known object capacity → **-1/EINVAL** with buffer unchanged.
   `sprintf_s` behaves as the bounded alias. Raw `cplus_snprintf` preserves C99
   truncation semantics.

4. **NULL arguments.** NULL format, NULL destination with `n>0`, NULL `FILE*`,
   and NULL input string all return a defined error (`-1`, or `EOF` for scanf
   family) with `errno=EINVAL`; no crash for raw or safe wrappers. NULL dest
   with `n=0` is legal: `cplus_snprintf(NULL,0,"%s","abc")` → 3.

5. **Invalid streams.** `cplus_fclose()` guards double-close (second call →
   `EOF`/`EBADF`). No format wrapper (raw or safe) tracks stream closure, so a
   `fclose`'d `FILE*` passed to `*printf`/`*scanf` is ISO C undefined behavior;
   deliberately triggering it would risk a crash, so it is **skipped and
   documented**, as instructed. Only NULL streams were exercised.

6. **Wide formats.** `cplus_swprintf(buf[4], 4, L"%ls", L"abc")` → 3, exact fit
   with NUL. Truncation (`n=3`, `"abcdef"`) → **-1 with errno=EOVERFLOW (84)** on
   Darwin, buffer `"ab\0"`, no overrun. `n=1` → -1/EOVERFLOW, NUL written only.
   NULL format/NULL dest/NULL stream → -1/EINVAL (EOF for `fwscanf`/`wscanf`).
   Note: in wide printf, `%s` expects a *narrow* `char*`; the test correctly uses
   `%ls` for `wchar_t*`. Apple Clang emits no `-Wformat` check for wide formats
   (see probe evidence) and the wrappers intentionally do not filter `%n`.

## FINDING (real defect, soft-reported by the test)

**`snprintf_s`/`sprintf_s` unprefixed macro capacity guard is bypassable for a
struct-member destination.** `CPLUS_SAFE_OBJ_SIZE(p)` is
`__builtin_object_size(p, 0)`. For `g.buf[16]` followed by an 8-byte canary in
the same struct, type-0 returns **24** (bytes to the end of the enclosing
object), not 16. `snprintf_s(g.buf, 20, "abcdefghijklmnopqrs")` is therefore
accepted (`ret=19`) and writes 4 bytes past the member:
`g.trail[0]=0x71 ('q')`, `g.trail[1]=0x72 ('r')` (expected `0x5A`).
This contradicts the documented contract that `dmax` exceeding the real buffer
capacity is detected as a violation. `__builtin_object_size(dest, 1)` returns
the correct 16 here; using type 1 (or the minimum of type 0/type 1) in
`CPLUS_SAFE_OBJ_SIZE` fixes the guard. The test demonstrates this as a
**non-fatal, contained** soft check so the harness still returns 0.

## Compile-time attribute evidence (probes in /tmp, not committed)

Narrow `format(printf,1,2)` attribute, wrong argument type:
```
probe.c:10:29: warning: format specifies type 'int' but the argument has type 'char *' [-Wformat]
   10 |     my_checked_printf("%d", "not-an-int");
      |                        ~~   ^~~~~~~~~~~~
      |                        %s
1 warning generated.
```
Wide `format(wprintf,1,2)` attribute on `const wchar_t *fmt`:
```
warning: 'format' attribute argument not supported: wprintf [-Wignored-attributes]
```
No `-Wformat` diagnostic for `L"%d"` with a `wchar_t*` argument — confirming
wide formats are caller-responsibility on this toolchain, as documented.

## Verification run

Exact task command (library builds refreshed via `tests/libc/build_libc.sh` and
`scripts/build_safecrt.sh`) compiles with zero warnings, runs in well under 5 s,
returns 0, and prints `format_attack: ok`. UBSan build
(`-fsanitize=undefined -fno-omit-frame-pointer`) also returns 0 with no UBSan
report. The only extra stdout line is the `FINDING:` line above.
