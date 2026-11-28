# Memory/string adversarial findings

Test: `tests/libc/test_memory_attack.c` (agent 12/12, branch `agent/modern-stdlib`).

## FINDING 1 — `cplus_strnlen_s` reads one byte past the declared bound (hard defect)

**Severity: real out-of-bounds read.** Reproduces deterministically with a guard page.

- Setup: `mmap` two pages, `mprotect(page2, PROT_NONE)`, fill the last 64 bytes of
  page 1 with `'Z'` (no NUL), `src = page1_end - 64`.
- `cplus_strnlen(src, 64)` (raw) returns 64 and does **not** fault.
- `cplus_strnlen_s(src, 64)` (checked) faults with **SIGBUS (signal 10)** at
  exactly `src + 64`, i.e. the first byte of the `PROT_NONE` page.

Root cause: `third_party/safeclib/src/str/strnlen_s.c`, `_strnlen_s_uchk()`:

```c
while (*str && smax) {   /* evaluates *str BEFORE testing smax */
    count++; smax--; str++;
}
```

C short-circuit evaluation is left-to-right, so on the final iteration (when
`smax == 0`) `*str` is still read at `src[smax]`. Disassembly of
`_cplus_sc__strnlen_s_chk` confirms a loop that reads `src[1+count]` while
`count != smax-1`, i.e. indices `0..smax` inclusive (`smax+1` bytes).

Impact: the checked API's documented promise — "refusing to read past smax",
"at most the first smax characters are accessed" — is violated. Any caller that
passes a non-NUL-terminated buffer of exactly `smax` bytes at the end of an
allocation/page can crash or read adjacent memory. Internal safeclib callers of
`strnlen_s` (e.g. `CHK_SLEN_MAX_CLEAR` on `dest`) inherit the same read.

The test runs this probe in a forked child so the crash is reported as
`FINDING: checked strnlen_s bound crashed with signal 10 (read past declared bound)`
instead of killing the suite. Under
`-fsanitize=undefined -fno-sanitize-recover=all` UBSan confirms it precisely:
`SUMMARY: UndefinedBehaviorSanitizer: BUS (...) in cplus_sc__strnlen_s_chk+0x8c`,
which is the `ldrb w11, [x8, x9]` loop read. Fix would be `while (smax && *str)`
(or hoist the bound check) in the vendored safeclib source.

## OBSERVATION (not a defect) — `cplus_strncpy_s` ESNOSPC leaves a partial copy

`cplus_strncpy_s(dst, 64, src, 64)` with an unterminated 64-byte `src` returns
`ESNOSPC` (406), and the resulting buffer is:

```
dst[0] = 0x00, dst[1..63] = 0x5A ('Z'), dst[64] = 0xCC (canary, unchanged)
```

Reason: `third_party/safeclib/src/str/safe_str_constraint.h`, `handle_error()`
only clears the whole destination under `#ifdef SAFECLIB_STR_NULL_SLACK`;
this build does not define it, so only `*dest = '\0'` is written. C11
K.3.7.1.4 requires only `dest[0] = '\0'` on a constraint violation, so this is
conforming. Upstream safeclib's own comment says "zeros dest", which is the
STR_NULL_SLACK extension — a documentation ambiguity, not a memory-safety bug.
The test prints `OBSERVATION:` for this case and does not count it as a failure.

## Held (no findings)

- Exact-size raw/checked memory copies, ESNOSPC boundary for `memcpy_s`,
  `memset_s`, `memmove_s`, `strcpy_s`: correct defined codes, canaries intact;
  safeclib clears/clamps the first `dmax` bytes as documented.
- Overlap: raw `memmove` correct in both directions; `memmove_s` succeeds and is
  correct; `memcpy_s` rejects with `ESOVRLP` (404), canary intact.
- Unterminated-source bounds for raw `strnlen`, `memchr`, `memrchr`, `strncpy`
  and checked `strncpy_s` (both success and ESNOSPC): no read/write past the
  bound, canaries intact.
- NULL / zero-length checked APIs: `ESNULLP` (400), `ESZEROL` (401), and
  `slen==0 -> EOK`; never crash; canaries intact. `cplus_strlen(NULL)` is not
  called (documented why a wrapper cannot make it safe).
- `SIZE_MAX`: `ESLEMAX` (403) for `memcpy_s`, `memmove_s`, `memset_s`,
  `strcpy_s`, `strncpy_s`, `strcat_s`, `strncat_s`; `strnlen_s` returns 0.
  Canaries intact; memory `_s` APIs clear/clamp only the first `dmax` bytes.
- Differential (1000 fixed-seed pseudorandom valid strings, including high-bit
  bytes): `strcmp`, `strncmp`, `strstr`, `memcmp`, `strchr`, `strrchr`, `memchr`,
  `memrchr` all match host libc / reference expectations.

## Build / toolchain notes

- The vendored safeclib's IGNORE constraint handler prints
  `IGNORE CONSTRAINT HANDLER: (code) ...` lines to stderr in this build (DEBUG
  enabled). Harmless noise; not a defect.
- Compile produced no warnings after removing an unused variable.
- RSIZE_MAX_MEM/RSIZE_MAX_STR compiled into safeclib are 16384/4096, not
  `SIZE_MAX >> 1` as the C+ headers default to; this is why `SIZE_MAX` maps to
  `ESLEMAX` rather than being handled by the C+ auto wrappers.
