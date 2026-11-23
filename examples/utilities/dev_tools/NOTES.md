# Developer utilities with runtime refusal evidence

Five deterministic, argv-driven developer utilities built with the C+
toolchain in `--strict --sanitize=none` mode.

> **Scope of the "safety proof":** every `REFUSED` line below is **runtime
> refusal evidence** — an oversized/overflowing/malformed/out-of-range input is
> rejected with a nonzero exit code instead of being processed. These are **not**
> formal proofs of memory safety, and no sanitizer was used (`--sanitize=none`
> is required). The guards are ordinary explicit checks that are visible in the
> source; the negative controls in the last section show that removing a guard
> makes the hostile input succeed, so the refusals are not hardcoded strings.

## Files

| File | Purpose |
| --- | --- |
| `hexdump.cp` | bounded 256-byte hex + ASCII dump; refuses missing/oversized files |
| `numconv.cp` | dec/hex/bin conversion with overflow-safe parsing |
| `sha_lite.cp` | deterministic FNV-1a 32-bit hash with a 64-byte input cap |
| `envfilter.cp` | filters `KEY=VALUE` argv lines by key prefix; validates keys |
| `timeconv.cp` | epoch seconds -> UTC date with integer arithmetic only |
| `run.sh` | builds all five under the exact command, runs 33 fixed cases under `timeout 30`, prints `UTIL_DEV <tools> <passed> <failed> <refused>` |

## Exact build commands (from the repository root)

Each binary is produced with exactly the required command shape:

```sh
./build/c+ build examples/utilities/dev_tools/hexdump.cp   --strict --sanitize=none -I. -o examples/utilities/dev_tools/hexdump
./build/c+ build examples/utilities/dev_tools/numconv.cp   --strict --sanitize=none -I. -o examples/utilities/dev_tools/numconv
./build/c+ build examples/utilities/dev_tools/sha_lite.cp  --strict --sanitize=none -I. -o examples/utilities/dev_tools/sha_lite
./build/c+ build examples/utilities/dev_tools/envfilter.cp --strict --sanitize=none -I. -o examples/utilities/dev_tools/envfilter
./build/c+ build examples/utilities/dev_tools/timeconv.cp  --strict --sanitize=none -I. -o examples/utilities/dev_tools/timeconv
```

All five builds succeed with no diagnostics. `run.sh` issues these commands
itself (after deleting any old binary), so a stale binary cannot mask a build
failure.

## How refusals are counted

`run.sh` uses two helpers:

- `check_ok` passes only when stdout matches **exactly** and the exit code is
  `0`.
- `check_refused` passes only when stdout matches **exactly** and the exit code
  is **nonzero** (each hostile case exits `1`).

A mismatch in either output or exit code increments `<failed>`. The final line
is `UTIL_DEV <tools> <passed> <failed> <refused>`; the script exits `0` only if
`<failed>` is `0`.

## Exact command/output pairs (observed)

### hexdump — `hexdump <path>`

Fixed fixtures (created by `run.sh` in a temp dir): `small.bin` is the 12 bytes
`Hello, IGI!\n`; `bin.bin` is `00 01 7f 80 ff 41`; `big.bin` is 257 `A` bytes.

```text
$ timeout 30 hexdump "$TMP/small.bin"
HEXDUMP 12 bytes
HEXDUMP 0000  48 65 6c 6c 6f 2c 20 49 47 49 21 0a              |Hello, IGI!.|
exit=0

$ timeout 30 hexdump "$TMP/bin.bin"
HEXDUMP 6 bytes
HEXDUMP 0000  00 01 7f 80 ff 41                                |.....A|
exit=0

$ timeout 30 hexdump "$TMP/nope"
HEXDUMP REFUSED cannot-open
exit=1

$ timeout 30 hexdump "$TMP/big.bin"
HEXDUMP REFUSED oversized
exit=1
```

The file is read one byte at a time through the checked `file::read` API with an
`error*` out-parameter. At 256 bytes the next read is a probe: EOF means exact
fit (accept), one more byte means `oversized` (refuse, never stored).

### numconv — `numconv <value> <from> <to>`

`from`/`to` are `dec`, `hex`, or `bin`. Parsing is bounded to `UINT32_MAX`.

```text
$ timeout 30 numconv 255 dec hex
NUMCONV 255 dec hex ff
exit=0

$ timeout 30 numconv ff hex bin
NUMCONV ff hex bin 11111111
exit=0

$ timeout 30 numconv 101101 bin dec
NUMCONV 101101 bin dec 45
exit=0

$ timeout 30 numconv 0 dec bin
NUMCONV 0 dec bin 0
exit=0

$ timeout 30 numconv 4294967295 dec hex
NUMCONV 4294967295 dec hex ffffffff
exit=0

$ timeout 30 numconv 4294967296 dec hex
NUMCONV REFUSED overflow
exit=1

$ timeout 30 numconv 102 bin dec
NUMCONV REFUSED invalid-digit
exit=1

$ timeout 30 numconv 10 oct hex
NUMCONV REFUSED unknown-base
exit=1

$ timeout 30 numconv '' dec hex
NUMCONV REFUSED invalid-digit
exit=1

$ timeout 30 numconv <65-char value> dec hex
NUMCONV REFUSED input-too-long
exit=1
```

The overflow guard is `v > (UINT_MAX - d) / base`. Because strict mode rejects
an unproven variable divisor, the division is performed by a shift-subtract
helper (`nc_udiv`) using only constant-width shifts.

### sha_lite — `sha_lite <input-string>`

FNV-1a 32-bit: offset basis `0x811c9dc5`, prime `0x01000193`. The `abc` and 64
byte results were cross-checked against an independent Python implementation.

```text
$ timeout 30 sha_lite ''
SHA_LITE 0 811c9dc5
exit=0

$ timeout 30 sha_lite hello
SHA_LITE 5 4f9f2cab
exit=0

$ timeout 30 sha_lite abc
SHA_LITE 3 1a47e90b
exit=0

$ timeout 30 sha_lite <64 x '1'>
SHA_LITE 64 9c96df05
exit=0

$ timeout 30 sha_lite <65 x '1'>
SHA_LITE REFUSED input-too-long
exit=1
```

### envfilter — `envfilter <prefix> <KEY=VALUE> [...]`

Uses argv lines only — the real process environment is never read, so results
are deterministic. Keys must match `[A-Za-z_][A-Za-z0-9_]*`.

```text
$ timeout 30 envfilter APP_ APP_NAME=igi APP_MODE=test OTHER=x APP_DEBUG=1
ENVFILTER match APP_NAME=igi
ENVFILTER match APP_MODE=test
ENVFILTER match APP_DEBUG=1
ENVFILTER APP_ 3
exit=0

$ timeout 30 envfilter Z_ A=1 B=2
ENVFILTER Z_ 0
exit=0

$ timeout 30 envfilter APP_ BADLINE
ENVFILTER REFUSED malformed
exit=1

$ timeout 30 envfilter APP_ =VALUE
ENVFILTER REFUSED empty-key
exit=1

$ timeout 30 envfilter APP_ A-B=1
ENVFILTER REFUSED bad-key
exit=1

$ timeout 30 envfilter APP_ <130 x '1'>=1
ENVFILTER REFUSED oversized
exit=1
```

### timeconv — `timeconv <epoch-seconds>`

No `time_t`, wall clock, or libc date routine. Howard Hinnant's
`civil_from_days` algorithm with integer arithmetic. Accepted range:
`0 .. 253402300799` (`9999-12-31T23:59:59Z`).

```text
$ timeout 30 timeconv 0
TIMECONV 0 1970-01-01T00:00:00Z
exit=0

$ timeout 30 timeconv 951782400
TIMECONV 951782400 2000-02-29T00:00:00Z
exit=0

$ timeout 30 timeconv 1767225600
TIMECONV 1767225600 2026-01-01T00:00:00Z
exit=0

$ timeout 30 timeconv 253402300799
TIMECONV 253402300799 9999-12-31T23:59:59Z
exit=0

$ timeout 30 timeconv 2147483647
TIMECONV 2147483647 2038-01-19T03:14:07Z
exit=0

$ timeout 30 timeconv -1
TIMECONV REFUSED out-of-range
exit=1

$ timeout 30 timeconv 253402300800
TIMECONV REFUSED out-of-range
exit=1

$ timeout 30 timeconv 12x
TIMECONV REFUSED invalid
exit=1

$ timeout 30 timeconv 99999999999999999999
TIMECONV REFUSED invalid
exit=1
```

The leap-day, Y2K38, and upper-bound cases are independent calendar
cross-checks, not values copied from the implementation.

## Aggregate run

```text
$ examples/utilities/dev_tools/run.sh
UTIL_BUILD hexdump OK
UTIL_BUILD numconv OK
UTIL_BUILD sha_lite OK
UTIL_BUILD envfilter OK
UTIL_BUILD timeconv OK
UTIL_CASE hexdump_small PASS
UTIL_CASE hexdump_binary PASS
UTIL_CASE hexdump_missing REFUSED rc=1
UTIL_CASE hexdump_oversized REFUSED rc=1
UTIL_CASE numconv_dec_hex PASS
UTIL_CASE numconv_hex_bin PASS
UTIL_CASE numconv_bin_dec PASS
UTIL_CASE numconv_zero PASS
UTIL_CASE numconv_max PASS
UTIL_CASE numconv_overflow REFUSED rc=1
UTIL_CASE numconv_bad_digit REFUSED rc=1
UTIL_CASE numconv_bad_base REFUSED rc=1
UTIL_CASE numconv_empty REFUSED rc=1
UTIL_CASE numconv_toolong REFUSED rc=1
UTIL_CASE sha_empty PASS
UTIL_CASE sha_hello PASS
UTIL_CASE sha_abc PASS
UTIL_CASE sha_boundary64 PASS
UTIL_CASE sha_toolong65 REFUSED rc=1
UTIL_CASE envfilter_prefix PASS
UTIL_CASE envfilter_none PASS
UTIL_CASE envfilter_malformed REFUSED rc=1
UTIL_CASE envfilter_empty_key REFUSED rc=1
UTIL_CASE envfilter_bad_key REFUSED rc=1
UTIL_CASE envfilter_oversized REFUSED rc=1
UTIL_CASE time_epoch PASS
UTIL_CASE time_leap PASS
UTIL_CASE time_2026 PASS
UTIL_CASE time_max PASS
UTIL_CASE time_y2k38 PASS
UTIL_CASE time_negative REFUSED rc=1
UTIL_CASE time_over_max REFUSED rc=1
UTIL_CASE time_nonnum REFUSED rc=1
UTIL_CASE time_big REFUSED rc=1
UTIL_DEV 5 18 0 16
```

`tools=5`, `passed=18`, `failed=0`, `refused=16`; script exit code `0`.
`run.sh` resolves the repository root from its own path, so it works from any
working directory.

## Negative controls (exit codes and refusals are real)

1. A temporary copy of `numconv.cp` with the line
   `if (v > nc_udiv(UINT_MAX - d, base)) return -1;` deleted was built with the
   same command and run with the hostile input:

```text
$ timeout 30 _negctl 4294967296 dec hex      # overflow guard removed
NUMCONV 4294967296 dec hex 0
exit=0

$ timeout 30 numconv 4294967296 dec hex      # real tool
NUMCONV REFUSED overflow
exit=1
```

   Without the guard the value wraps to `0` and the tool succeeds; the refusal
   is therefore caused by the explicit guard, not by a fixed output string.

2. A temporary copy of `run.sh` with the expected hash for `sha_hello` changed
   from `SHA_LITE 5 4f9f2cab` to `SHA_LITE 5 DEADBEEF` produced:

```text
UTIL_CASE sha_hello FAIL rc=0 got=[SHA_LITE 5 4f9f2cab] want=[SHA_LITE 5 DEADBEEF]
UTIL_DEV 5 17 1 16
NEG_SCRIPT_RC=1
```

   The script detects a wrong expectation and exits nonzero; it is not a rubber
   stamp. All temporary copies (`_negctl.cp`, `_negctl`, `_negrun.sh`,
   `_negrun.out`) were deleted afterwards.

## Strict-mode authoring notes

- argv pointers are never dereferenced directly: each argument is copied into a
  fixed local buffer via `string::substr`, which also NUL-terminates empty
  strings. This satisfies strict mode's "unverifiable extent" rule.
- strict mode rejects a variable divisor and variable-width shifts. `numconv`
  uses a shift-subtract divide helper and a constant-width bit mask instead.
- out-parameters passed by address (`tc_civil_from_days`'s `year`, `mon`, `day`)
  are initialized at declaration, as strict lifetime analysis requires.
- `--sanitize=none` is used exactly as required; the evidence here is explicit
  runtime refusal, not sanitizer diagnostics.
- No files outside `examples/utilities/dev_tools/` were created or modified.
