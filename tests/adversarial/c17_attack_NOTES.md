# c17 attack — adversarial findings (agent 12/12)

Test: `tests/libc/test_c17_attack.c`. Targets: `cplus_aligned_alloc`,
`cplus_quick_exit`, `cplus_at_quick_exit`, `cplus__Exit`
(`libc/src/stdlib/stdlib_c17.c`) and `cplus_wcstoimax` / `cplus_wcstoumax`
(`libc/src/stdlib/inttypes_wide.c`).

## Result

**No wrapper defects found.** Zero `FINDING:` lines in both the plain and the
UBSan run. The guard order in `cplus_aligned_alloc` correctly avoids the
`size % alignment` modulo-by-zero for `alignment == 0`, and every documented
constraint violation returns `NULL` with `errno == EINVAL`.

## Attacks executed

1. **Alignment torture** — `0, 1, 2, 3, 7, 12, 24, SIZE_MAX-1, SIZE_MAX,
   (1<<40)+8`, with `size` both valid and invalid. All invalid values returned
   `NULL`/`EINVAL`, no crash, no SIGFPE. The `(alignment & (alignment-1))`
   short-circuit prevents `alignment - 1` underflow from being observed and
   prevents `% 0`.
2. **Size torture** — `size 0`; `size=100` with `align=64`; `SIZE_MAX` with
   `align=8` (rejected: `SIZE_MAX % 8 == 7`); `SIZE_MAX-7` with `align=8`
   (passes the guard and is delegated to the host, which rejects it — accepted
   either way); `align=SIZE_MAX, size=SIZE_MAX` (rejected as non-power-of-two,
   no huge allocation attempted).
   Valid cases `8/8`, `16/64`, `64/128`, `128/256`, `4096/8192`: pointer
   exactly aligned, full-range byte write/read verified.
3. **Allocator provenance** — `cplus_aligned_alloc` pointer is NOT live via
   `cplus_alloc_is_live` and is released with **host `free()`**. A
   `cplus_malloc(32)` pointer IS live and released with `cplus_free()`. No
   cross-free performed (that would be UB in the test).
4. **Repeated fail/retry** — 10,000 `aligned_alloc`/`free` cycles alternating
   `align=16`/`align=64`; `cplus_alloc_live_count()` unchanged; every pointer
   aligned; no crash.
5. **Termination (POSIX fork, guarded `#ifndef _WIN32`)** — 9 handlers
   registered via `cplus_at_quick_exit` (1 pipe-marker + 8 no-ops), all returned
   0. Child `cplus_quick_exit(42)` exited with **exactly 42** and the pipe
   received the `'Q'` marker, proving `at_quick_exit` handlers run before
   termination. Child `cplus__Exit(17)` exited with **exactly 17** and wrote no
   marker, proving `_Exit` skips those handlers. These are never called in the
   parent.
6. **wcstoimax/wcstoumax** — base `1, 37, INT_MIN, INT_MAX`; `L""`,
   whitespace-only, `L"+"`, `L"-"`, `L"0x"`, `L"0xGG"`; 40-digit positive and
   negative overflow; `NULL` string; `NULL` endptr; full differential vs the
   host oracle on all valid inputs (return value, `errno`, and `endptr`
   identity). Overflow: `wcstoimax` saturates to `INTMAX_MAX` / `INTMAX_MIN`
   with `ERANGE`; `wcstoumax` saturates to `UINTMAX_MAX` with `ERANGE`.
   `NULL` string gives `EINVAL`, return 0 and `*endptr = NULL` when supplied.
7. **Logging on/off** — the entire non-terminating battery is run twice
   (logging disabled and enabled) into two result records and compared
   element-by-element; identical.

## Exact observed behavior worth documenting (intentional / host-delegated)

These are **not** wrapper defects — they are the documented host semantics or
the deliberate hardening extension, and the differential check confirms the
wrapper matches the host byte-for-byte:

- `cplus_aligned_alloc(8, 0)` and `(64, 0)` return a non-NULL, correctly
  aligned block with `errno == 0` (implementation-defined C11 size-0 case,
  delegated to host `aligned_alloc`). Freed with host `free()`.
- `cplus_aligned_alloc(1|2|4, n)` is rejected (`EINVAL`) because the hardening
  guard additionally requires the alignment to be a multiple of
  `sizeof(void *)` (8 on this host). This is stricter than bare C11 and is
  documented in `stdlib_c17.c`.
- On this macOS host, `wcstoimax/wcstoumax` set `errno = EINVAL` (22) and
  `endptr == nptr` for no-conversion inputs (`L""`, whitespace, `L"+"`,
  `L"-"`, `L"0x"`, `L"0xGG"`, and invalid bases). Notably `L"0xGG"` base 0
  leaves `endptr` at offset 0 rather than offset 1; Apple's CRT diagnoses the
  malformed hex prefix. The wrapper delegates, so this is exact host behavior
  and matches the host oracle used in the differential check.
- `wcstoumax(L"-1", 10)` returns `UINTMAX_MAX` with `errno == 0` and
  `endptr` at offset 2 (C-mandated negation into unsigned), delegated to host.

## Toolchain notes

- Plain build: no compiler warnings at `-std=c99 -Wall -Wextra -pedantic`.
- UBSan (`-fsanitize=undefined -fno-sanitize-recover=undefined`): clean, no
  diagnostics.
- ASan could not be evaluated: this environment's ASan aborts during runtime
  init with `AddressSanitizer: CHECK failed: sanitizer_malloc_mac.inc:189
  "((!asan_init_is_running)) != (0)"` even for a trivial `int main(void){return 0;}`.
  This is an environment/toolchain issue, not a target defect.
- Runtime of the test binary is ~0.00s wall (well under the 5s budget).
