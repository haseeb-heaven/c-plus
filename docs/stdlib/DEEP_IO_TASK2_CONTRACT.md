# Deep Task-2 contract for `stdlib/io.hp` (uncovered cases only)

Scope: `stdlib/io.hp` only. This fixture covers **only** the cases in the
parent agent's Task-2 list that were not already exercised by
`tests/stdlib/deep_io_1byte.cp` (1-byte buffer, NULL/zero size, `size > INT_MAX`,
exact-fit 16-char line + newline, CRLF at `fgets` boundary, overlong line + drain)
and `tests/stdlib/deep_io_token.cp` (overlong 70-digit token, overflow/underflow/
malformed/non-finite numeric tokens, alloc-failure no-op, final EOF).

Fixture files:
- `tests/stdlib/deep_io_task2.cp`
- `tests/stdlib/deep_io_task2.stdin`

`io.hp` was **not** modified. `stdlib/file.hp` changes made in parallel by the
parent are unrelated: this test has no file-family dependency.

## 0. Classification vocabulary

| Class | Meaning |
| --- | --- |
| **EXPECTED-SUCCESS** | `STDLIB_OK` (0) with an exact value and a clear `error` record (`.code()==CPLUS_EXIT_OK==0`, `.category()==STDLIB_CATEGORY_OK==0`). |
| **RECOVERABLE-FAILURE** | `STDLIB_FAIL` (-1) with an exact `.code()`/`.category()`; the process must **not** terminate. |
| **CLEAN-EOF** | `STDLIB_EOF` (1) with a clear `error` record and **no write** to the caller's destination. |
| **FATAL-SAFETY** | None: every condition is reported through `error`, never through `cplus_fatal`. |

Constants: `STDLIB_OK=0`, `STDLIB_EOF=1`, `STDLIB_FAIL=-1`;
`CPLUS_EXIT_OK=0`, `CPLUS_EXIT_BOUNDS=101`, `CPLUS_EXIT_UNSUPPORTED=108`;
categories `OK=0`, `INVALID=1`, `RANGE=2`, `BOUNDS=5`.

## 1. Coverage map (tags -> case)

| Tag(s) | Task-2 case |
| --- | --- |
| `c1_exact_eof_ok`, `c1_exact_eof_next` | 1. Exact-fit line at **physical EOF, no terminator**: `abcd` (4 bytes, no newline) into a 5-byte buffer -> `STDLIB_OK`/`"abcd"`, error clear; next read -> `STDLIB_EOF`. |
| `c2_newline_ok`, `c2_newline_next_eof` | 2. Exact-fit then `\n` at EOF: `abcd\n` into a 5-byte buffer -> `STDLIB_OK`/`"abcd"`; next read -> `STDLIB_EOF`, destination untouched (proves **no phantom empty line**). |
| `c3_cr_boundary`, `c3_next_aligned` | 3. CR-only terminator at the exact `fgets` boundary: `abcd\rNEXT\n` into a 5-byte buffer -> `STDLIB_OK`/`"abcd"`, next read -> `"NEXT"` (stream stays aligned). |
| `c4_{i,l,f,d}_malformed`, `c4_{i,l,f,d}_overflow` | 4. Out-param byte-for-byte unchanged on `STDLIB_FAIL` for all four readers (malformed `12xyz` -> `108/INVALID`; overflow -> `101/RANGE`). |
| `c5_{i,l,f,d}_valid_after` | 5. Valid input after a rejected input writes the expected value for all four readers. |
| `c6_{i,l,f,d}_eof` | 6. EOF before any data for all four readers -> `STDLIB_EOF`, error clear, out-param unchanged. |

22 checks total. Final line: `IO_TASK2 22 0`.

## 2. Why `fmemopen` is used for cases 1 and 2

Cases 1 and 2 each require the line under test to be the **last bytes of a
physical stream**, with a different trailing sequence (`abcd` with no terminator
vs `abcd\n`). A single linear stdin fixture cannot end in two different ways, so
those two cases install isolated in-memory streams as `stdin` with `fmemopen()`
(4-byte view of `"abcd"`, 5-byte view of `"abcd\n"`), run the two reads each,
then `fclose` and restore the original `stdin`. The shared fixture
`deep_io_task2.stdin` supplies cases 3-6; its **natural EOF** supplies case 6.
This is a test-harness technique only; it does not alter `io.hp` or the runtime.

## 3. Exact stdin fixture (`od -c` verified)

```
abcd<CR>NEXT<LF>
12xyz
99999999999999999999999999
-77
12xyz
99999999999999999999999999
1234567890123
12xyz
1e400
6.5
12xyz
1e400
-2.25
```

Byte count: 128. The CR in the first line is a literal `0x0D` and lands exactly
one byte past the 5-byte destination, so it is consumed by `read_line`'s peek.

Sequence: case 3 consumes the first line in two reads; each numeric reader then
consumes three tokens (malformed, overflow, valid) in the order int, long, float,
double; case 6 consumes four reads against EOF.

## 4. Exact stdout (all three modes identical)

```
c1_exact_eof_ok 1
c1_exact_eof_next 1
c2_newline_ok 1
c2_newline_next_eof 1
c3_cr_boundary 1
c3_next_aligned 1
c4_i_malformed 1
c4_i_overflow 1
c5_i_valid_after 1
c4_l_malformed 1
c4_l_overflow 1
c5_l_valid_after 1
c4_f_malformed 1
c4_f_overflow 1
c5_f_valid_after 1
c4_d_malformed 1
c4_d_overflow 1
c5_d_valid_after 1
c6_i_eof 1
c6_l_eof 1
c6_f_eof 1
c6_d_eof 1
IO_TASK2 22 0
```

Exit code **0**, **zero bytes on stderr** in every mode.

## 5. Observed discrepancy (test-expectation mistake, NOT an io.hp defect)

The Task-2 text names `99999999999999999999999999` as the "overflow token" for
all four readers. That is correct for `int`/`long` (it exceeds 64-bit range),
but it is **not** a float/double overflow: the token is 10^26 - 1, far inside
IEEE-754 binary32 (~3.4e38) and binary64 (~1.8e308) range. Empirically:

```
# temporary instrumented copy of the fixture, default build
DIAG f_overflow: st=0 code=0 cat=0 val=1.00000003e+26 snap=-2468.125 memcmp=166 errno=0
DIAG d_overflow: st=0 code=0 cat=0 val=1e+26 snap=13579.25 memcmp=53 errno=0
c4_f_overflow 0
c4_d_overflow 0
IO_TASK2 22 2
```

This is **correct, documented behaviour**: `read_float`/`read_double` delegate
to `strtof`/`strtod`, reject only `errno == ERANGE` or non-finite results
(`io.hp` lines 226 and 262), and write the parsed finite value on success.
`1e26` sets neither condition. Asserting `STDLIB_FAIL` for that token would have
required changing the library, which is forbidden.

Resolution: keep `99999999999999999999999999` for `read_int`/`read_long`
(genuine `ERANGE` -> `FAIL / 101 / RANGE`), and use `1e400` for
`read_float`/`read_double` (genuine overflow-to-infinity with `ERANGE` ->
`FAIL / 101 / RANGE`). Both still assert the core case-4 property: the
out-param is byte-for-byte unchanged after the failure. No header was changed.

A parent run against the *pre-fix* fixture reproduced exactly this state
(`IO_TASK2 22 2`, only `c4_f_overflow`/`c4_d_overflow` failing). After the
fixture/source correction below, all checks pass.

## 6. Exact verification commands and results

Flags must be **separate argv operands**; quoting them as one string fails with
`clang: error: unsupported option '--strict --sanitize=none'`.

### 6.1 Requested build: `--strict --sanitize=none`

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
timeout 40 build/c+ build tests/stdlib/deep_io_task2.cp --strict --sanitize=none \
  -o build/deep-io-task2/task2_strict_nosan
timeout 30 build/deep-io-task2/task2_strict_nosan \
  < tests/stdlib/deep_io_task2.stdin
```

Result: build rc=0 (0 bytes stderr); run rc=0 (0 bytes stderr);
`IO_TASK2 22 0`; all 22 tags print `1`.

### 6.2 Default mode

```sh
timeout 40 build/c+ build tests/stdlib/deep_io_task2.cp \
  -o build/deep-io-task2/task2_default
timeout 30 build/deep-io-task2/task2_default \
  < tests/stdlib/deep_io_task2.stdin
```

Result: build rc=0 (0 bytes stderr); run rc=0 (0 bytes stderr);
`IO_TASK2 22 0`; all 22 tags print `1`.

### 6.3 UBSan mode

```sh
timeout 40 build/c+ build --strict --sanitize=undefined \
  tests/stdlib/deep_io_task2.cp -o build/deep-io-task2/task2_ubsan
timeout 30 build/deep-io-task2/task2_ubsan \
  < tests/stdlib/deep_io_task2.stdin
```

Result: build rc=0 (0 bytes stderr); run rc=0 (0 bytes stderr);
`IO_TASK2 22 0`; all 22 tags print `1`.

### 6.4 stdlib harness (as requested by parent)

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
CPLUS_BIN=$PWD/build/c+ \
  sh tests/stdlib/run_stdlib_tests.sh deep_io_task2
```

Exact output:

```
  ok   deep_io_task2

C+ stdlib tests: 1 passed, 0 failed, 0 skipped
```

Exit code 0.

## 7. Defects found in `io.hp`

**None.** Every case in Task 2 passes against the unchanged header. The two
initial failures were traced to an incorrect test expectation (section 5), not
to a library defect. The exact-fit-at-EOF and CR-boundary behaviours previously
fixed in `io.hp` (see `DEEP_IO_CONTRACT.md` §4) are confirmed here by
`c1_*`, `c2_*` and `c3_*`.
