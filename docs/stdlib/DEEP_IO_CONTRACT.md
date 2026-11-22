# Deep boundary/adversarial contract for `stdlib/io.hp`

Scope: `stdlib/io.hp` only. Regression fixtures:
`tests/stdlib/deep_io_1byte.{cp,stdin}` and
`tests/stdlib/deep_io_token.{cp,stdin}`. This document records the observed
behaviour before any change, the two minimal fixes that were applied, and the
compatibility decisions taken so that future changes do not silently drift.

## 0. Classification vocabulary used by the fixtures

| Class | Meaning in these tests |
| --- | --- |
| **EXPECTED-SUCCESS** | Call returns `STDLIB_OK` (0) or `STDLIB_EOF` (1), with an exact value and a clear `error` record. |
| **RECOVERABLE-FAILURE** | Call returns `STDLIB_FAIL` (-1) with an exact `cplus_exit_code` (`.code()`) and `STDLIB_CATEGORY_*` (`.category()`). The process must **not** terminate; a fatal trap is a test failure. |
| **FATAL-SAFETY** | A condition that would be a runtime `cplus_fatal` if the header attempted the operation (e.g. an out-of-bounds write). `io.hp` is required to convert all such conditions into RECOVERABLE-FAILURE, so **there are no fatal cases in this suite** and no subprocess wrapper is needed. This is asserted by the fact that every fixture exits 0 (see §6). |

Constants (from `runtime/include/cplus/cplus.h`, `stdlib/error.hp`):
`STDLIB_OK=0`, `STDLIB_EOF=1`, `STDLIB_FAIL=-1`;
`CPLUS_EXIT_OK=0`, `CPLUS_EXIT_BOUNDS=101`, `CPLUS_EXIT_UNSUPPORTED=108`,
`CPLUS_EXIT_IO=109`;
categories `OK=0`, `INVALID=1`, `RANGE=2`, `IO=3`, `BOUNDS=5`.

## 1. Reproduction method

All reproductions were built with the repository compiler from the repository
root, e.g.:

```sh
D=/private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/deep_io_repro
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
  build/c+ build "$D/repro_exact.cp" -o "$D/repro_exact"
"$D/repro_exact" < "$D/repro_exact.stdin"
```

Every executable run was wrapped in `timeout 15 …` to detect hangs; exact exit
codes and exact stdout/stderr bytes were captured for both final fixtures.

## 2. Findings

### Scenario 1 — `read_line()` into a 1-byte destination

Repro: `repro_1byte.cp` with stdin `\nhello\nX\n` (buffer sentinel `X`/`Y`).

Observed (unchanged by the fixes):

| Input to `read_line(buf, 1, …)` | Return | `.code()` | `.category()` | `buf[0]` | `buf[1]` sentinel |
| --- | --- | --- | --- | --- | --- |
| empty line (`\n`) | `STDLIB_OK` | 0 | OK | `\0` | untouched (`Y`) |
| non-empty (`hello`) | `STDLIB_FAIL` | 101 | BOUNDS | `\0` | untouched (`Y`) |
| one-char line (`X`) | `STDLIB_FAIL` | 101 | BOUNDS | `\0` | untouched (`Y`) |
| EOF | `STDLIB_EOF` | 0 | OK | untouched | untouched |

Verdict: **no defect**. The only write is the terminating NUL *inside* the
1-byte destination; no line data and no byte past the destination is written.
The non-empty case is RECOVERABLE-FAILURE, and the stream is drained to the
delimiter. `buf == NULL` / `size == 0` are refused up front with `CPLUS_EXIT_IO`
/ `STDLIB_CATEGORY_INVALID`.

### Scenario 2 — buffer size greater than `INT_MAX`

Repro: `repro_bounds_token.cp`, calling `read_line(b, (size_t)INT_MAX + 1, &err)`
with a one-past sentinel in `b`.

Observed: `STDLIB_FAIL`, `.code()=101`, `.category()=BOUNDS`, sentinel intact,
and the following `read_line` still returned the first real line (`first`).
Verdict: **no defect**. The `size > INT_MAX` guard runs before `fgets()`, so no
input is consumed. This is important because `fgets` takes an `int` size.

### Scenario 3 — overlong numeric tokens and `stdlib_io_token()`

Repro: `repro_bounds_token.cp` with a 70-digit token followed by `42`, then an
8-byte token fed directly to `stdlib_io_token(tok, 8)`.

Observed:

* `read_int()` on the 70-digit token → `STDLIB_FAIL`, `.code()=101`,
  `.category()=RANGE`; the next `read_int()` returned `42` (stream aligned).
* Direct `stdlib_io_token(tok, 8)` on `12345678` → returns **2**, `tok` is
  `"1234567"` with `tok[7]=='\0'`; the next `read_int()` returned `99`.

Verdict: **no defect / not silent**. `stdlib_io_token()` stores at most
`size-1` bytes, always NUL-terminates, drains the whole token, and returns 2 on
truncation. All four numeric readers translate return code 2 into a
RECOVERABLE-FAILURE with `CPLUS_EXIT_BOUNDS` / `STDLIB_CATEGORY_RANGE`. A
direct caller that ignores the return code would see a truncated token, so the
return value is part of the API contract (see §3).

### Scenario 4 — exact-fit and overlong input followed by another read

Repro: `repro_exact.cp` with a 17-byte buffer (capacity 16):

```
0123456789abcdef      (16 chars + LF  -> exact fit)
after
0123456789abcdefg     (17 chars       -> overlong)
tail
```

Observed **before** the fix:

```
exact -1 101 5 0      <- exact-fit line falsely reported "too long"
next 0 1
over -1 101 5
after_over 0 1
```

Observed **after** the fix:

```
exact 0 0 0 1
next 0 1
over -1 101 5
after_over 0 1
```

Root cause: `fgets(buf, size, …)` reads at most `size-1` bytes and stops
*without consuming the delimiter* when it fills the buffer. The old test
`n == size-1 && buf[n-1] != '\n' && !feof(stdin)` therefore fired for a line
that fitted exactly, drained the newline and returned a spurious BOUNDS error.

Second defect found in the same area (`repro_crlf.cp`): a 15-char CRLF line in
a 17-byte buffer makes `fgets` stop right after the CR, leaving the LF in the
stream; the next `read_line` returned an empty line instead of `next`. Before
fix: `crlf 0 1` / `after 0 0`; after fix: `crlf 0 1` / `after 0 1`.

Verdict: **two confirmed defects, both fixed** (§4). Overlong lines remain a
RECOVERABLE-FAILURE with `CPLUS_EXIT_BOUNDS` / `STDLIB_CATEGORY_BOUNDS`, and the
remainder is drained in both the exact-fit and the overlong path.

### Scenario 5 — numeric overflow, underflow, malformed tokens, EOF

Repro: `repro_num.cp` (17 tokens). Observed on this platform (macOS arm64):

| Reader | Token | Return / code / category |
| --- | --- | --- |
| `read_int` | `2147483648`, `-2147483649`, 20-digit | `FAIL / 101 / RANGE` |
| `read_int` | `12abc` | `FAIL / 108 / INVALID` |
| `read_long` | `9223372036854775808`, `-9223372036854775809` | `FAIL / 101 / RANGE` |
| `read_float` | `1e-50`, `1e-40`, `1e39` | `FAIL / 101 / RANGE` |
| `read_float` | `abc` | `FAIL / 108 / INVALID` |
| `read_double` | `1e-320`, `1e-310`, `1e400`, `inf` | `FAIL / 101 / RANGE` |
| `read_double` | `xyz` | `FAIL / 108 / INVALID` |
| any | EOF | `STDLIB_EOF / 0 / OK` |

Verdict: **no defect**. All conversions delegate to libc `strtol`/`strtoll`/
`strtof`/`strtod`; no hand-written parser. Every failure is deterministic and
recoverable, and `errno` is reset before each conversion. The output value is
left untouched on failure, and the error record is cleared on success/EOF.

## 3. Intended contract (as implemented and now enforced)

1. **`stdlib_io_token(buf, size)`** — skips leading whitespace; reads one
   whitespace-delimited token; writes at most `size-1` bytes plus a terminating
   NUL; drains the token to the delimiter; returns `0` (ok), `1` (EOF/no token,
   also for `buf == NULL || size == 0`), or `2` (truncated). Callers **must**
   check for 2.
2. **`read_line(buf, size, err)`** — `size == 0` / `buf == NULL` →
   `FAIL / 109 / INVALID`; `size > INT_MAX` → `FAIL / 101 / BOUNDS` with no
   stream access; `size == 1` accepts an empty line only and refuses a non-empty
   line with `FAIL / 101 / BOUNDS` after draining it; otherwise `fgets` is used,
   trailing CR/LF is stripped, an exact fit (including a CR/LF delimiter just
   past the buffer, and EOF immediately after a full buffer) is accepted, and a
   genuinely overlong line is drained and reported as `FAIL / 101 / BOUNDS`
   with `buf[0]` set to `'\0'`.
3. **Numeric readers** — `FAIL / 101 / RANGE` for token-too-long, `ERANGE` and
   out-of-range values, and non-finite float results; `FAIL / 108 / INVALID`
   for malformed text; `STDLIB_EOF` at end of input; `FAIL / 109 / INVALID` for
   a NULL destination. Subnormal results are libc-dependent: they are either
   accepted as finite values or rejected as RANGE — never any other category.
4. **Error-record hygiene** — `error_reset` runs first in every reader; a
   successful read or EOF leaves the record clear.

## 4. Fixes applied to `stdlib/io.hp` (minimal)

Inside the post-`fgets` block of `read_line()` only:

* When the buffer is full and the stream is not at EOF, peek at the next byte
  instead of unconditionally declaring failure. A line ending (`\n` or `\r`)
  or EOF means the line fitted exactly and is accepted; any other byte means
  the line was truncated, in which case the remainder is drained and the
  original `FAIL / 101 / BOUNDS` is reported.
* A CR found by the peek is consumed (along with a following LF, if present),
  mirroring the existing `size == 1` CR handling, so a CRLF delimiter at the
  width boundary cannot leak an LF into the next read.

No public signature, return code, category, or message string was changed. No
other function in `io.hp` was modified.

## 5. Backward-compatibility decisions

* **1-byte buffers remain valid** for empty lines. Refusing all `size == 1`
  calls would have broken `tests/stdlib/stdlib_hardening_test.cp`, which asserts
  `read_line(tiny_buf, 1, &err) == STDLIB_OK` for an empty line. The safe,
  narrower behaviour (NUL-only write on refusal) is preserved.
* **Underflow stays rejected.** `read_float`/`read_double` reject any `ERANGE`
  (including underflow-to-zero), not just overflow. The in-code comment names
  overflow and non-finite values, but tightening underflow would silently
  change accepted values. It is documented here instead of changed.
* **Overlong tokens stay a RANGE error** rather than being parsed from their
  truncated prefix; accepting the prefix would be a silent behaviour change.
* **No `.expected` files** were added for the two new fixtures (file ownership
  is restricted); the fixtures are self-checking and gate on exit status, and
  their exact stdout is pinned in §6.

## 6. Fixtures, exact expected output and commands

`deep_io_1byte.cp` / `.stdin` covers scenarios 1, 2 and 4. Its stdin is:

```
nonempty
<empty line>
first
0123456789abcdef
after_exact
0123456789abcdefg
after_over
0123456789abcde<CR><LF>
after_crlf
last
```

Exact stdout (16 lines, all checks `1`):

```
s1_nonempty 1
s1_empty 1
s1_null 1
s1_zero 1
s2_reject 1
s2_aligned 1
s4_exact 1
s4_after_exact 1
s4_over 1
s4_after_over 1
s4_crlf 1
s4_after_crlf 1
s4_last 1
s4_eof 1
s1_eof 1
ALL_OK 1
```

`deep_io_token.cp` / `.stdin` covers scenarios 3 and 5. Exact stdout
(21 lines):

```
s3_overlong 1
s3_aligned 1
s3_direct 1
s3_after_direct 1
s5_i_over 1
s5_i_under 1
s5_i_bad 1
s5_i_alloc_fail 1
s5_l_over 1
s5_l_under 1
s5_f_under 1
s5_f_sub 1
s5_f_over 1
s5_f_bad 1
s5_d_under 1
s5_d_sub 1
s5_d_over 1
s5_d_inf 1
s5_d_bad 1
s5_eof 1
ALL_OK 1
```

Both exit **0** and produce **zero bytes on stderr**.

### Exact verification commands

Harness (default mode, as requested):

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
CPLUS_BIN=$PWD/build/c+ \
  sh tests/stdlib/run_stdlib_tests.sh deep_io
# -> ok deep_io_1byte / ok deep_io_token
# -> C+ stdlib tests: 2 passed, 0 failed, 0 skipped   (exit 0)
```

The harness has no `--strict` / `--sanitize` flags (it accepts only a single
pattern argument), so the strict variants were run by invoking the driver
directly. **Note:** the flags must be separate argv operands; quoting them as
one string (`"--strict --sanitize=none"`) fails with
`clang: error: unsupported option '--strict --sanitize=none'`.

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
build/c+ build --strict --sanitize=none tests/stdlib/deep_io_1byte.cp \
  -o build/deep-io-modes/deep_io_1byte_strict_nosan
timeout 15 build/deep-io-modes/deep_io_1byte_strict_nosan \
  < tests/stdlib/deep_io_1byte.stdin
# -> ALL_OK 1, exit 0

PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
build/c+ build --strict --sanitize=none tests/stdlib/deep_io_token.cp \
  -o build/deep-io-modes/deep_io_token_strict_nosan
timeout 15 build/deep-io-modes/deep_io_token_strict_nosan \
  < tests/stdlib/deep_io_token.stdin
# -> ALL_OK 1, exit 0
```

`--strict` alone and `--sanitize=none` alone were also run directly; both
fixtures returned `ALL_OK 1` with exit 0 in each mode.

Regression check for pre-existing tests after the fix:

```sh
PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin \
CPLUS_BIN=$PWD/build/c+ sh tests/stdlib/run_stdlib_tests.sh io
# -> 9 passed, 0 failed, 0 skipped (includes io_edge_cases,
#    stdlib_hardening_test via "safety", and the two new fixtures)
```

## 7. Deterministic fault injection

`stdlib_alloc_fail_after(0)` / `stdlib_alloc_fail_reset()` from
`stdlib/error.hp` are exercised by `s5_i_alloc_fail` in `deep_io_token.cp`: a
successful `read_int` must still succeed while every checked allocation is
forced to fail. This confirms the I/O readers are allocation-free, so there is
no allocation-failure path to test and no need to exhaust memory.
