# data_2 — four deterministic data-processing apps (C+ stdlib)

Four NEW single-file apps in `examples/expanded/data_2/`. Every input is fixed
argv; there is no clock, network, interactive prompt, user path or shelling out
from inside an app. Identical argv produces byte-identical stdout. Every
refusal is a single deterministic stdout line carrying the C+ stdlib error code
and exit status 2, with empty stderr.

This directory is **self-contained**: it does not modify any existing example
or any file outside `examples/expanded/data_2/`.

## Files

| File | Purpose |
| --- | --- |
| `sort_tool.cp` | Stable integer/string sort (asc/desc) with size, token and parse gates |
| `hash_tool.cp` | Bounded FNV-1a/32 hashing with known-answer self-check |
| `rle.cp` | Bounded run-length encode/decode with byte-exact internal roundtrip |
| `regex_lite.cp` | Wildcard (`*`, `?`) match with a hard backtracking step budget |
| `run.sh` | Builds and runs all four apps plus all hostile cases; prints the summary |
| `NOTES.md` | This file |

## Exact build commands

Run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`:

```sh
./build/c+ build examples/expanded/data_2/sort_tool.cp  --strict --sanitize=none -I. -o examples/expanded/data_2/.run/sort_tool
./build/c+ build examples/expanded/data_2/hash_tool.cp  --strict --sanitize=none -I. -o examples/expanded/data_2/.run/hash_tool
./build/c+ build examples/expanded/data_2/rle.cp        --strict --sanitize=none -I. -o examples/expanded/data_2/.run/rle
./build/c+ build examples/expanded/data_2/regex_lite.cp --strict --sanitize=none -I. -o examples/expanded/data_2/.run/regex_lite
```

Each command used exactly the form
`./build/c+ build <file> --strict --sanitize=none -I. -o <bin>` and exited 0
with empty stdout and empty stderr. All four binaries were produced.

## Harness command and exact output

```sh
sh examples/expanded/data_2/run.sh
```

```
== sort_tool ==
build ok   sort_tool
ok         sort_int_asc
ok         sort_int_desc
ok         sort_str_asc
ok         sort_str_desc
ok         sort_over
ok         sort_token_over
ok         sort_int_bad
ok         sort_int_over
ok         sort_str_bad
ok         sort_badmode
== hash_tool ==
build ok   hash_tool
ok         hash_vector
ok         hash_boundary
ok         hash_input_over
ok         hash_count_over
ok         hash_bad_byte
== rle ==
build ok   rle
ok         rle_enc
ok         rle_dec
ok         rle_max
ok         rle_pipeline
ok         rle_raw_over
ok         rle_bad_magic
ok         rle_bad_count
ok         rle_dec_overflow
ok         rle_trailing_sep
== regex_lite ==
build ok   regex_lite
ok         regex_table
ok         regex_pair
ok         regex_pattern_over
ok         regex_text_over
ok         regex_step_guard
ok         regex_bad_pattern
ok         regex_usage
DATA_2 4 4 0 19
```

Harness exit code: 0.

`DATA_2 <apps> <passed> <failed> <refused>` = 4 apps, 4 passed, 0 failed,
19 hostile cases correctly refused. A scenario counts as passed only when the
exit code matches, stderr is empty, and stdout matches the expected bytes
exactly (`cmp`). Every invocation is wrapped in `timeout 30`; a timeout is
never counted as a pass. Expected fixtures are regenerated from fixed bytes on
every run.

## Per-app exact commands and outputs

All commands below were run from the repository root with the binaries in
`examples/expanded/data_2/.run`.

### sort_tool — stable sort

```sh
./examples/expanded/data_2/.run/sort_tool int asc 5 3 9 3 1
```
```
SORT app=sort_tool mode=int order=asc n=5 max_items=24 max_token=24
SORT in 0=5
SORT in 1=3
SORT in 2=9
SORT in 3=3
SORT in 4=1
SORT out 0=1 src=4
SORT out 1=3 src=1
SORT out 2=3 src=3
SORT out 3=5 src=0
SORT out 4=9 src=2
SORT check sorted=1 stable=1 cmp=9
SORT fp=81
SORT done
```

The two equal `3` values keep their original indices (`src=1`, then `src=3`),
which is the stability proof in the transcript. The `fp` field is an
order-weighted checksum (independently recomputed as
`1*1+3*2+3*3+5*4+9*5 = 81`).

```sh
./examples/expanded/data_2/.run/sort_tool str desc banana apple cherry apple
```
```
SORT app=sort_tool mode=str order=desc n=4 max_items=24 max_token=24
SORT in 0=banana
SORT in 1=apple
SORT in 2=cherry
SORT in 3=apple
SORT out 0=cherry src=2
SORT out 1=banana src=0
SORT out 2=apple src=1
SORT out 3=apple src=3
SORT check sorted=1 stable=1 cmp=4
SORT fp=974
SORT done
```

Hostile cases (all exit 2, empty stderr):

```sh
./examples/expanded/data_2/.run/sort_tool int asc 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16 17 18 19 20 21 22 23 24 25
# SORT error code=101 reason=too-many-items
./examples/expanded/data_2/.run/sort_tool str asc <65-char token> ok
# SORT error code=101 reason=token-too-long
./examples/expanded/data_2/.run/sort_tool int asc 3 abc 1
# SORT error code=108 reason=invalid-integer
./examples/expanded/data_2/.run/sort_tool int asc 999999999999
# SORT error code=101 reason=integer-overflow
./examples/expanded/data_2/.run/sort_tool str asc "$(printf 'a\001b')" ok
# SORT error code=108 reason=invalid-string
./examples/expanded/data_2/.run/sort_tool bogus asc 1 2
# SORT error code=108 reason=bad-mode
```

### hash_tool — FNV-1a/32

```sh
./examples/expanded/data_2/.run/hash_tool hello abc OpenAI
```
```
HASH app=hash_tool alg=FNV-1a-32 offset=0x811c9dc5 prime=0x01000193 max_len=64 selfcheck=ok n=3
HASH 0 len=5 hash=0x4f9f2cab text=hello
HASH 1 len=3 hash=0x1a47e90b text=abc
HASH 2 len=6 hash=0x54497523 text=OpenAI
HASH xor=0x0191b083
HASH done
```

All three hash values were independently reproduced with FNV-1a (offset
`0x811c9dc5`, prime `0x01000193`, 32-bit wrap). The `selfcheck=ok` field means
the program first verified `FNV-1a("hello") == 0x4f9f2cab` before reading
input; a mismatch is an immediate refusal. The 64-byte boundary case hashes 64
`a` bytes to `0xd96f0f85` (also independently reproduced).

Hostile cases (all exit 2):

```sh
./examples/expanded/data_2/.run/hash_tool <65-char token>
# HASH error code=101 reason=input-too-long
./examples/expanded/data_2/.run/hash_tool a a a a a a a a a a a a a a a a a
# HASH error code=101 reason=too-many-inputs
./examples/expanded/data_2/.run/hash_tool "$(printf 'a\001b')"
# HASH error code=108 reason=invalid-byte
```

### rle — run-length encode/decode

```sh
./examples/expanded/data_2/.run/rle enc aaabbc
```
```
RLE app=rle op=enc max_raw=64 max_run=99 magic=RLE1;
RLE in_len=6 in=aaabbc
RLE group 0 byte=a run=3
RLE group 1 byte=b run=2
RLE group 2 byte=c run=1
RLE encoded=RLE1;a03;b02;c01 groups=3 enc_len=16
RLE roundtrip ok=1 decoded_len=6
RLE done
```

```sh
./examples/expanded/data_2/.run/rle dec 'RLE1;a03;b02;c01'
```
```
RLE app=rle op=dec max_raw=64 max_run=99 magic=RLE1;
RLE enc=RLE1;a03;b02;c01
RLE group 0 byte=a run=3
RLE group 1 byte=b run=2
RLE group 2 byte=c run=1
RLE decoded_len=6 decoded=aaabbc
RLE roundtrip ok=1 groups=3
RLE done
```

`enc` decodes its own stream and compares the result byte-for-byte with the
original token; `dec` re-encodes its output and compares with the exact input
stream. The harness additionally extracts the encoded stream from the `enc`
run, feeds it to a separate `dec` process and requires the exact transcript
containing `decoded=aaabbc` (`rle_pipeline`). The 64-byte boundary case encodes
to the single group `RLE1;a64`.

Hostile cases (all exit 2):

```sh
./examples/expanded/data_2/.run/rle enc <65-char token>
# RLE app=rle op=enc ...
# RLE in_len=65 in=...
# RLE error code=101 reason=raw-too-long
./examples/expanded/data_2/.run/rle dec 'BAD1;a03'
# RLE error code=108 reason=bad-magic
./examples/expanded/data_2/.run/rle dec 'RLE1;aXY'
# RLE error code=108 reason=malformed-stream
./examples/expanded/data_2/.run/rle dec 'RLE1;a99;b99;c99;d99;e99;f99;g99'
# RLE error code=101 reason=decoded-overflow
./examples/expanded/data_2/.run/rle dec 'RLE1;a03;'
# RLE error code=108 reason=malformed-stream
```

### regex_lite — bounded wildcard match

```sh
./examples/expanded/data_2/.run/regex_lite
```
```
REGEX app=regex_lite max_pat=64 max_text=128 max_steps=256 selfcheck=ok cases=6
REGEX case 0 pattern=* text=anything match=1 want=1 ok=1 steps=17 backtracks=8
REGEX case 1 pattern=a?c text=abc match=1 want=1 ok=1 steps=3 backtracks=0
REGEX case 2 pattern=a?c text=ac match=0 want=0 ok=1 steps=2 backtracks=0
REGEX case 3 pattern=a*b text=aaab match=1 want=1 ok=1 steps=7 backtracks=2
REGEX case 4 pattern=a*b text=aaac match=0 want=0 ok=1 steps=8 backtracks=3
REGEX case 5 pattern=a*b?c text=aaabcc match=1 want=1 ok=1 steps=9 backtracks=2
REGEX done cases=6
```

```sh
./examples/expanded/data_2/.run/regex_lite 'a*b?c' aaabcc
```
```
REGEX pattern=a*b?c text=aaabcc match=1 steps=9 backtracks=2 plen=5 tlen=6
REGEX done
```

The built-in table has known expected answers checked before any transcript is
printed; a mismatch would refuse with `selfcheck-failed`. The matcher counts
every step (including each backtrack) and refuses once the 256-step budget is
exceeded — a refused case is never reported as a match.

Hostile cases (all exit 2):

```sh
./examples/expanded/data_2/.run/regex_lite <65-char pattern> abc
# REGEX error code=101 reason=pattern-too-long
./examples/expanded/data_2/.run/regex_lite 'a*' <129-char text>
# REGEX error code=101 reason=text-too-long
./examples/expanded/data_2/.run/regex_lite "$(printf '*a%.0s' 1 2 3 ... 20)" "<120 a's>b"
# REGEX error code=107 reason=step-budget-exhausted
./examples/expanded/data_2/.run/regex_lite "$(printf 'a\001b')" abc
# REGEX error code=108 reason=invalid-pattern
./examples/expanded/data_2/.run/regex_lite onlyone
# REGEX error code=108 reason=usage
```

## Teaching model — explicit non-proof statement

**These apps and their transcripts are teaching examples, not formal
verification or a safety proof.** They demonstrate a defensive pattern: fixed
inputs, explicit capacity gates, checked `stdlib::vector` / `stdlib::string` /
`stdlib::error` usage, refusal-before-output for hostile cases, and
self-consistency checks (stable-order check, FNV known-answer check, encode /
decode roundtrip, regex expected-answer table, step budget). The harness gives
repeatable *evidence* that the chosen fixed inputs behave as expected under
`--strict --sanitize=none` and `timeout 30`.

It does **not** prove absence of bugs, memory safety for all possible inputs,
absence of undefined behavior in every path, compiler correctness, or
resistance to adversarial inputs outside the exercised set. Nothing here was
run under a sanitizer or a formal prover, and no live-game or production
deployment claim is made. The step budget and length caps are policy choices,
not completeness guarantees. Treat the passing `DATA_2 4 4 0 19` line as
regression evidence for these fixtures only.
