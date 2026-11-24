# RESULTS — EDGE100 safecrt (agent 5/10)

## Final line

```
EDGE100 safecrt 113 402 402 0
```

## Method

- `harness.c` enumerates all 113 public entry points across the 13
  `stdlib/safe/*.h` headers (see `API_MANIFEST.md`).
- Every (API, vector) case is executed in a **forked child**. Buffers are
  placed at the end of an mmap'd RW page followed by a `PROT_NONE` guard page;
  a 64-byte `0xC3` canary separates the declared capacity from the guard.
- Children set `CPLUS_SAFE_CONSTRAINT_IGNORE` and
  `cplus_runtime_set_violation_trap(0)` so refusals are observable as return
  codes rather than process exits, run under `alarm(2)`, and exit 0 (pass),
  1 (unsafe result) or 2 (canary corruption). A signal is a crash.
- Built with `-std=c99 -Wall -Wextra` (no warnings) and linked against
  `build/safe/libcplus_safecrt.a` plus `build/libc/libcplus_hardened.a`.
- `run.sh` applies `timeout 60` to the harness and writes `results/cases.log`,
  `results/summary.txt`, `results/stderr.txt` and `results/digests.sha256`.

## Coverage

11 edge families: valid, NULL, zero, exact-fit, one-over, SIZE_MAX, overlap,
unterminated, double-free, closed stream, `%n`. Applicability is per API; the
per-case verdicts are in `results/cases.log`.

Per-header API counts: safe_memory 11, safe_string 12, safe_alloc 23,
safe_io 12, safe_format 16, safe_error 2, safe_except 4 (3 macros + init),
safe_log 3, safe_platform 1, safe_stats 4, safe_runtime 17, safe_ext_mem 2,
safe_c 6 (compiled macro targets).

## Verdicts

All 402 cases PASS: the API either produced the documented success for a
valid/zero-length call, or refused the hostile input with a documented
error/sentinel, and no canary or guard page was touched. No crash, no
corruption, no unexpected success on a hostile case.

Expected refusals observed include `ESZEROL` (401) for `memcmp_s`/`memzero_s`
with `dmax == 0`, `ESNOSPC` (406) for destination overflow, `ESOVRLP` (404)
for overlapping copies, `ESUNTERM` (407) for unterminated strings,
`EINVAL`/`-1` for `%n` and non-literal formats, and use-after-close refusal
for closed `FILE*` streams.

## Informational notes (not harness failures)

These are documented boundary behaviors, not regressions, and are reflected
in the harness expectations:

- Zero-length calls to `memcpy_s`/`memmove_s`/`memset_s` return `EOK` (C11
  allows zero length); `memcmp_s`/`memzero_s` return `ESZEROL` (401) for
  `dmax == 0`. A truthful `bmax` is required for I/O adapters; SIZE_MAX is
  injected through `count` (or `size` for the hardened `cplus_fwrite`) so the
  documented `size*count` overflow guard is exercised rather than an
  out-of-contract read.
- Bounded string copies (`strncpy_s`/`strncat_s`, and their `_auto` wrappers)
  tolerate an unterminated source *safely*: reads are capped by `slen/dmax`
  and the result is NUL-terminated. The case still asserts canary integrity.
- `cplus_runtime_report()` reports only; the longjmp that unwinds a guard is
  performed by the sanitizer hook. A guard body that returns OK therefore
  yields `CPLUS_RUNTIME_OK`; a NULL body is refused with
  `CPLUS_RUNTIME_ERROR`.
- The vendored `cplus_sc_timingsafe_bcmp`/`cplus_sc_timingsafe_memcmp` do not
  document NULL handling. Passing NULL with length 0 is exercised (defined);
  NULL with a positive length is outside their contract (a raw dereference)
  and is deliberately not asserted as a supported case.
- `cplus_safe_set_unknown_capacity_mode(99)` selects strict mode (any
  non-zero value), unlike `cplus_safe_set_constraint_mode`, which falls back
  to IGNORE for invalid values.

No source, header, archive or git state was modified; all artifacts are
confined to `tests/edge100/safecrt/`.
