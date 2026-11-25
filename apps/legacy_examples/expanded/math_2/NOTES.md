# Expanded math/encoding examples (C+ deterministic apps)

Four single-file apps with deterministic, bounded, testable cores. All builds
use the repository C+ driver in `--strict --sanitize=none` mode from the repo
root so `stdlib/*.hp` resolves via `-I.`.

## Teaching model / non-proof statement (READ FIRST)

**These programs are teaching models with run-time checks, not formal proofs.**
They demonstrate how to express fixed-capacity arithmetic, a deterministic
PRNG, checksums and base64 in the C+ stdlib with explicit bounds checks and
refusal paths. The observed refusals are *evidence of those run-time checks
firing on the listed hostile inputs*, not a proof of memory safety, absence of
undefined behaviour, mathematical correctness for all inputs, or resistance to
adversarial attack. No theorem prover, model checker, fuzzer or formal
verification tool was run. No pass was fabricated: every output in this file
was produced by running the exact commands shown on this host, and the expected
values were independently recomputed (see "Independent value check" below).

## Files

| File | Purpose |
| --- | --- |
| `bigint.cp` | Fixed-width base-10 digit-array add/sub/mul; refuses operand/result overflow, underflow and bad digits |
| `prng.cp` | xorshift32 with fixed seed, bounded values via bitmask; refuses seed/count overflow and over-cap counts |
| `checksum.cp` | CRC32 (reflected IEEE) + Adler32 over bounded byte strings; refuses inputs over 128 bytes |
| `base64.cp` | RFC 4648 encode/decode with byte-exact roundtrip; refuses oversized and malformed input |
| `run.sh` | Builds and runs all four plus 18 hostile scenarios; prints the summary line |
| `NOTES.md` | This file |

Shared conventions: all output is on stdout, stderr is empty on every scenario,
refusals print a `REFUSED code=<cplus-code> <reason>` line and exit 1. The
`<cplus-code>` values are the real `cplus_exit_code` values: `101` =
`CPLUS_EXIT_BOUNDS`, `108` = `CPLUS_EXIT_UNSUPPORTED`. (The C+ `print()`
macro separates arguments with a space, hence `code= 101` rather than
`code=101`; this is stable and byte-compared by the harness.)

## Build commands (exact)

Run from `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`:

```sh
./build/c+ build apps/legacy_examples/expanded/math_2/bigint.cp   --strict --sanitize=none -I. -o apps/legacy_examples/expanded/math_2/.run/bigint
./build/c+ build apps/legacy_examples/expanded/math_2/prng.cp     --strict --sanitize=none -I. -o apps/legacy_examples/expanded/math_2/.run/prng
./build/c+ build apps/legacy_examples/expanded/math_2/checksum.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/math_2/.run/checksum
./build/c+ build apps/legacy_examples/expanded/math_2/base64.cp   --strict --sanitize=none -I. -o apps/legacy_examples/expanded/math_2/.run/base64
```

All four builds exited 0 with empty stdout and empty stderr.

## Harness command and exact output

```sh
sh apps/legacy_examples/expanded/math_2/run.sh
```

```
== bigint ==
build ok   bigint
ok         bi_default
refused    bi_input
refused    bi_result
refused    bi_under
refused    bi_mul
refused    bi_digit
refused    bi_mode
== prng ==
build ok   prng
ok         pr_default
ok         pr_seed1
refused    pr_seedov
refused    pr_big
refused    pr_syn
refused    pr_countov
refused    pr_usage
== checksum ==
build ok   checksum
ok         ck_default
ok         ck_hello
refused    ck_long
refused    ck_longarg
refused    ck_mode
== base64 ==
build ok   base64
ok         b64_default
ok         b64_enc
ok         b64_dec
refused    b64_long
refused    b64_enclong
refused    b64_bad
refused    b64_badlen
MATH_2 4 4 0 18
```

The harness checks, per scenario: exit code matches, stderr is empty, and
stdout matches the expected bytes exactly (`cmp`). Every invocation is bounded
by `timeout -k 5 30`. Running the harness twice produced byte-identical output
(diff clean). `MATH_2 4 4 0 18` = 4 apps, 4 apps passed, 0 apps failed, 18
hostile scenarios correctly refused.

## Per-app commands and exact outputs

Let `B=apps/legacy_examples/expanded/math_2/.run/<app>` (built above).

### bigint.cp

```sh
timeout 30 "$B"    # B = .run/bigint, no arguments
```
```
BIGINT add 999999999999999 + 1 = 1000000000000000
BIGINT add 1234567890123456 + 7 = 1234567890123463
BIGINT sub 1000000000000000 - 1 = 999999999999999
BIGINT mul 99999 * 99999 = 9999800001
BIGINT mul 1234567 * 89 = 109876463
BIGINT OK count=5 cap=16 ops= 5
```
Exit 0.

Hostile cases (each exit 1):

```sh
timeout 30 "$B" input-overflow    # 17-digit operand
timeout 30 "$B" result-overflow   # 16-digit max + 1 carries out
timeout 30 "$B" sub-underflow     # 1 - 2, negative result refused
timeout 30 "$B" mul-overflow      # 18-digit product refused
timeout 30 "$B" bad-digit         # "12x4" refused
timeout 30 "$B" bogus             # unknown mode
```
```
BIGINT REFUSED code= 101 overflow-input
BIGINT REFUSED code= 101 overflow-result
BIGINT REFUSED code= 101 underflow
BIGINT REFUSED code= 101 overflow-result
BIGINT REFUSED code= 101 digit-syntax
BIGINT REFUSED code= 108 bad-mode
```

### prng.cp

```sh
timeout 30 "$B"          # .run/prng, fixed seed 305419896 (0x12345678), 16 values
```
```
PRNG seed=305419896 count=16 cap=256
PRNG 0 raw=2274908837 bounded=5
PRNG 1 raw=358294691 bounded=3
PRNG 2 raw=1210119364 bounded=4
PRNG 3 raw=2176035992 bounded=0
PRNG 4 raw=1882851208 bounded=0
PRNG 5 raw=698933837 bounded=5
PRNG 6 raw=2311737117 bounded=5
PRNG 7 raw=3306712617 bounded=1
PRNG 8 raw=3547882151 bounded=7
PRNG 9 raw=984697617 bounded=1
PRNG 10 raw=1774140920 bounded=0
PRNG 11 raw=1309615608 bounded=0
PRNG 12 raw=4240336032 bounded=0
PRNG 13 raw=895871509 bounded=5
PRNG 14 raw=2361839302 bounded=6
PRNG 15 raw=4141526377 bounded=1
PRNG sum=43 OK
```
Exit 0. The final `OK` is printed only after an in-process replay from the
same seed reproduces every stored raw value.

```sh
timeout 30 "$B" 1 8
```
```
PRNG seed=1 count=8 cap=256
PRNG 0 raw=270369 bounded=1
PRNG 1 raw=67634689 bounded=1
PRNG 2 raw=2647435461 bounded=5
PRNG 3 raw=307599695 bounded=7
PRNG 4 raw=2398689233 bounded=1
PRNG 5 raw=745495504 bounded=0
PRNG 6 raw=632435482 bounded=2
PRNG 7 raw=435756210 bounded=2
PRNG sum=19 OK
```
Exit 0.

Hostile cases (each exit 1):

```sh
timeout 30 "$B" 4294967296 4               # seed > 2^32-1
timeout 30 "$B" 1 257                      # count > cap 256
timeout 30 "$B" 1x 4                       # non-digit seed
timeout 30 "$B" 1 99999999999999999999999  # count > 2^64-1
timeout 30 "$B" 1                          # wrong argument count
```
```
PRNG REFUSED code= 101 seed-overflow
PRNG REFUSED code= 101 count-too-large
PRNG REFUSED code= 108 seed-syntax
PRNG REFUSED code= 101 count-overflow
PRNG REFUSED code= 108 usage
```

### checksum.cp

```sh
timeout 30 "$B"    # .run/checksum, built-in vectors
```
```
CHECKSUM len=0 text= crc32=00000000 adler32=00000001
CHECKSUM len=9 text=123456789 crc32=cbf43926 adler32=091e01de
CHECKSUM len=43 text=The quick brown fox jumps over the lazy dog crc32=414fa339 adler32=5bdc0fda
CHECKSUM len=3 text=abc crc32=352441c2 adler32=024d0127
CHECKSUM OK count=4 cap=128 n= 4
```
Exit 0.

```sh
timeout 30 "$B" text hello
```
```
CHECKSUM len=5 text=hello crc32=3610a686 adler32=062c0215
```
Exit 0.

Hostile cases (each exit 1). `LONG` is 129 `A` bytes generated by
`head -c 129 /dev/zero | tr '\0' 'A'`:

```sh
timeout 30 "$B" oversized
timeout 30 "$B" text "$LONG"
timeout 30 "$B" bogus
```
```
CHECKSUM REFUSED code= 101 input-too-long
CHECKSUM REFUSED code= 101 input-too-long
CHECKSUM REFUSED code= 108 bad-mode
```

### base64.cp

```sh
timeout 30 "$B"    # .run/base64, RFC 4648 vectors + decode/roundtrip
```
```
BASE64 enc "" ->  rt=OK len=0
BASE64 enc "f" -> Zg== rt=OK len=1
BASE64 enc "fo" -> Zm8= rt=OK len=2
BASE64 enc "foo" -> Zm9v rt=OK len=3
BASE64 enc "foob" -> Zm9vYg== rt=OK len=4
BASE64 enc "fooba" -> Zm9vYmE= rt=OK len=5
BASE64 enc "foobar" -> Zm9vYmFy rt=OK len=6
BASE64 OK count=7 cap=96 n= 7
```
Exit 0. Each `rt=OK` means the encoded text was decoded again and the bytes
matched the original input byte-for-byte.

```sh
timeout 30 "$B" enc foobar
timeout 30 "$B" dec Zm9vYmFy
```
```
BASE64 enc "foobar" -> Zm9vYmFy rt=OK len=6
BASE64 dec Zm9vYmFy len=6 hex=666f6f626172
```
Exit 0 for both.

Hostile cases (each exit 1):

```sh
timeout 30 "$B" oversized
timeout 30 "$B" encoversized
timeout 30 "$B" malformed 'Zm9*'
timeout 30 "$B" badlen Zm9vZ
```
```
BASE64 REFUSED code= 101 input-too-long
BASE64 REFUSED code= 101 encoded-too-long
BASE64 REFUSED code= 108 malformed-encoding
BASE64 REFUSED code= 108 malformed-length
```

## Independent value check

Expected values were recomputed with the host Python standard library before
the expected files were written; the C+ outputs then matched all of them:

```python
import zlib, base64
zlib.crc32(b"123456789") & 0xffffffff        # 0xcbf43926
zlib.adler32(b"123456789") & 0xffffffff      # 0x091e01de
base64.b64encode(b"foobar")                  # b'Zm9vYmFy'
base64.b64decode(b'Zm9vYmFy').hex()          # '666f6f626172'
# xorshift32 seed 305419896: first raw = 2274908837, sum(raw & 7, 16) = 43
```

## Checked-API notes / strict-mode findings

* `argv` slots are FFI pointers: each app copies the slot with
  `__builtin_memcpy(&value, p + idx, sizeof(value))` before use.
* `--strict` rejects a subscript on a `const char *` parameter whose extent it
  cannot prove (`[CPLUS-FFI] ... unverifiable extent`). `bigint` and `prng`
  therefore copy into a fixed local buffer with `__builtin_memcpy` (after a
  constant upper-bound guard) and index that array. `checksum` and `base64`
  keep the same constant-bound discipline.
* `error` instances are used with `.code()` / `.what()` when held by value and
  `->code()` / `->what()` when held through `error *` (the global refusal
  helper). Mixing these fails to compile.
* `goto` is rejected by C+ RAII lowering ("bypasses C+ cleanup"); `prng`
  places all fallible work in `pr_run()` and drops its containers in `main`.
* `.len` is a reserved C+ length property, so the bigint digit count field is
  named `ndig`.
* `string()` with no argument does not lower cleanly; `string("")` is used.
* No variable divisor or modulus appears: only the named constants
  `DEC_BASE`, `ADLER_MOD`, `B64_DEC`, `CK_DEC` and shift/mask arithmetic
  (`& 0x7u`, `>> 18`, `& 0x3Fu`, `& 0xFFu`) are used.
* `vector::at_checked`, `push_back`, `string::append`, `substr`-style copying
  and `error_fail` are the failure-reporting paths; allocation/vector failures
  are treated as refusals, never as zero values.

No wall-clock, network, environment, interactive prompt or user-specific path
is consulted by any app. Output is byte-for-byte reproducible.
