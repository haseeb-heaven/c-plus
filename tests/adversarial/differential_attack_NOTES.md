# Differential attack harness — notes

`tests/libc/test_differential_attack.c` is a deterministic differential fuzzer.
It uses a fixed-seed xorshift32 generator (`0x13579BDF` by default, optional
`argv[1]` seed override) and never calls libc `rand()`. Same seed always yields
the same input stream, results, and canary checks.

## Results (branch `agent/modern-stdlib`)

- **Iterations:** 13,256 (string 3000, memory 3000, ctype 256, math 3000,
  conversions 3000, wchar 1000).
- **Divergences found:** none across the default seed, a second seed
  (`0xDEADBEEF`), 200 additional seed sweeps, and a full UBSan rebuild of both
  the harness and the static library (`-fsanitize=undefined
  -fno-sanitize-recover=all`).
- **Exit status:** 0 on success, prints `differential_attack: ok (...)`.
- **Runtime:** ~0.02 s, well under the 10 s budget.

## Validity rules / deliberately excluded cases

Every generated input is NUL-terminated, fitted to a correctly sized buffer,
and free of UB:

- Random `char` bytes are drawn from `1..255` (no embedded NUL), so every
  generated `char[]` is a well-formed C string. The terminator is written
  explicitly.
- `strncpy` destinations are 64 bytes with `n <= 32`; `strcat` destinations are
  128 bytes with `prefix <= 16` and appended `<= 32`. Canary words surround
  every destination on both sides and are checked after each call, so silent
  overruns/underruns are treated as divergences.
- `memcpy` uses two distinct objects (non-overlapping). `memmove` deliberately
  exercises both overlap directions, which is legal for that API.
- `memcmp`/`memchr`/`memrchr` read exactly 128 initialized bytes from one
  object.
- ctype covers all 256 byte values `0..255` only (not `EOF`, which the wrappers
  deliberately normalize differently).
- Math inputs are finite; the result classifier compares NaN/Inf cases by
  category and finite results by relative tolerance plus signed-zero check.
- Conversion strings are random decimal/hex/octal literals with optional sign,
  whitespace, overflow padding, invalid trailing junk, `inf`/`nan`/hex-float
  forms; `errno` is reset before each call and compared exactly along with the
  value and `endptr` offset.
- The "huge bound" `strnlen`/`strncmp` cases use `SIZE_MAX`, which is valid
  because the strings are NUL-terminated.

Excluded because validity cannot be guaranteed / would be intentional hardening:

- NULL pointer arguments. Several wrappers return `EINVAL`/0 for NULL where the
  host function would fault; this is a **documented intentional hardening
  difference**, not a defect, and is therefore not fed to the fuzzer.
- Undersized destinations, unterminated buffers, and overlapping `memcpy`
  operands are UB for the host API and are excluded by construction.
- Locale-dependent ctype/strcoll behavior is not varied; the process stays in
  the default `"C"` locale so both sides see identical classification tables.

## Host-symbol caveat

On this macOS SDK/libSystem, `memrchr` is **not declared** in `<string.h>` even
with `_DARWIN_C_SOURCE`/`_GNU_SOURCE`, and no `memrchr` symbol is exported by
libSystem. That single family compares `cplus_memrchr` against a local
reference implementation (`ref_memrchr` in the harness) with exact BSD/ISO
semantics (scan backwards over `n` bytes, compare as `unsigned char`). This is
a **reference implementation**, not a host-symbol comparison; all other
families compare directly against the identically named host function.

## Classification of any reported divergence

The harness prints `DIFF [family/function] ...` lines with the exact generated
input (hex bytes, numeric value in `%a`, conversion string, base, endptr
offset, errno) and both results. There were none in any run. If one appears,
it must be triaged as either:

1. **Real defect** — wrapper changes a result for a valid input.
2. **Documented hardening** — wrapper rejects an input the host would accept
   but only in a way described as intentional (e.g. NULL guard). No such case
   is currently generated.
