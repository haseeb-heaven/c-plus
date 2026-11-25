# Real-world text utilities with safety proofs

Five deterministic, argv-driven text utilities live in this directory.  Each
is a standalone C+ program built with the same strict, no-sanitizer command
and exercised by `run.sh` against fixed fixtures in a private temp directory.

| file | purpose | key checked APIs |
|---|---|---|
| `twc.cp` | wc-like line/word/byte count | `file::read`, `error`, `string` ctor |
| `tgrep.cp` | substring + regex-lite (`.`, `*`) line search | `file::read_line`, `string::find`, `error.category()` |
| `thead.cp` | first N lines with bounded N | `file::read_line`, `string`, `error` |
| `ttr.cp` | fixed from/to character transliteration | `file::read_line`, `string::append`, `error` |
| `tbase64.cp` | base64 encode/decode, byte-exact | `file::read`, `string`, `error` |

No file outside `apps/legacy_examples/utilities/text_tools/` was created or modified.

## Exact build commands

Run from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`:

```sh
./build/c+ build apps/legacy_examples/utilities/text_tools/twc.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/twc
./build/c+ build apps/legacy_examples/utilities/text_tools/tgrep.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/tgrep
./build/c+ build apps/legacy_examples/utilities/text_tools/thead.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/thead
./build/c+ build apps/legacy_examples/utilities/text_tools/ttr.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/ttr
./build/c+ build apps/legacy_examples/utilities/text_tools/tbase64.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/tbase64
```

All five exited 0 with **empty build logs** (no compiler diagnostics).  The
harness issues exactly these commands, each wrapped in `timeout -k 5 30`.

## Exact run command and output

```sh
sh apps/legacy_examples/utilities/text_tools/run.sh
```

Observed stdout (harness exit code 0):

```text
== twc ==
build ok   twc
ok         twc_small
ok         twc_no_final_nl
ok         twc_empty
ok         twc_missing
ok         twc_usage
ok         twc_oversize
== tgrep ==
build ok   tgrep
ok         tgrep_literal
ok         tgrep_none
ok         tgrep_wild
ok         tgrep_usage
ok         tgrep_pattern
ok         tgrep_longline
ok         tgrep_missing
== thead ==
build ok   thead
ok         thead_2
ok         thead_1
ok         thead_all
ok         thead_zero
ok         thead_neg
ok         thead_big
ok         thead_nonnumeric
ok         thead_trailing
ok         thead_longline
ok         thead_usage
== ttr ==
build ok   ttr
ok         ttr_map
ok         ttr_identity
ok         ttr_mismatch
ok         ttr_longset
ok         ttr_longline
ok         ttr_usage
== tbase64 ==
build ok   tbase64
ok         b64_enc_hw
ok         b64_enc_bytes
ok         b64_enc_bin5
ok         b64_dec_hw
ok         b64_dec_nl
ok         b64_dec_bin5
ok         b64_all_enc
ok         b64_max_enc
ok         b64_all_roundtrip
ok         b64_dec_bad
ok         b64_dec_short
ok         b64_dec_noncanon
ok         b64_dec_midpad
ok         b64_enc_oversize
ok         b64_missing
ok         b64_usage_mode
ok         b64_usage_arity
UTIL_TEXT 5 5 0 26
```

`UTIL_TEXT <tools> <passed> <failed> <refused>`:
**5 tools, 5 passed, 0 failed, 26 hostile cases correctly refused.**

A case passes only when all three hold: expected exit code, **empty stderr**,
and stdout byte-identical to a fixed expected file (`cmp -s`).  Every program
and compiler invocation is bounded by `timeout -k 5 30`.

## Safety proofs (why the refusals are real, not assumed)

**`twc`** — `TWC_MAX_BYTES = 65536`.  Bytes are streamed one at a time through
`file::read(one, 1, &err)`.  At 65536 bytes the tool performs one extra read;
if a byte exists it prints `TWC refuse oversize` and exits 1.  The 65537-byte
fixture was refused.  The path is passed through the checked `string`
constructor first.  `isspace()` receives an `unsigned char` value, and the
final unterminated line is counted explicitly (`no_final_nl.txt` → `TWC 2 3 13`).

**`tgrep`** — `TG_LINE_CAP = 256`, `TG_PAT_CAP = 64`.  The argv pattern is
copied byte-by-byte into a local array and a 64-byte pattern is refused before
matching.  Lines use `file::read_line` into a local array; an overlong line
(300-byte fixture) returns `STDLIB_FAIL` with
`err.category() == STDLIB_CATEGORY_BOUNDS`, which is refused.  Pointer
parameters are read with `__builtin_memcpy` rather than raw subscripts so the
strict FFI extent checker is satisfied.  Literal patterns go through
`string::find`; the regex-lite path uses a bounded two-pointer wildcard match
with constant extents.

**`thead`** — `TH_LINE_CAP = 256`, `TH_MAX_N = 1000`.  N is parsed by a bounded
decimal parser (no `strtol`, which the strict lifetime checker cannot model):
leading sign, digit-only, early bound before multiply, exact terminator check.
`0`, `-3`, `1001`, `abc` and `2x` were all refused.  Each emitted line is
wrapped in a checked `string` before printing.

**`ttr`** — `TR_LINE_CAP = 256`, `TR_SET_CAP = 64`.  Equal, non-empty set
lengths strictly below the cap are enforced; the mapping is copied into fixed
local arrays.  Unequal (`ab`/`X`) and 64-byte sets were refused.  Each output
character is appended through `string::append` with its return code checked;
a 300-byte input line was refused as `TTR refuse line-too-long`.

**`tbase64`** — `B64_MAX_IN = 4096`, output buffer 5500 bytes (4096 bytes need
5464 characters).  One extra read detects input over 4096 bytes
(`TBASE64 refuse input-too-large`).  Decode validates length % 4, alphabet,
tail-only padding, at most two '=', RFC 4648 canonical low bits, and decoded
byte count before emitting anything.  Malformed (`abc!`), short (`Zg`),
non-canonical (`Zh==`) and mid-padding (`Zg==AAAA`) tokens were refused
byte-exactly.  Byte-exact roundtrip was checked on all 256 byte values (hex
compared against Python's `bytes(range(256)).hex()`), on a fixed binary fixture
(`abc\0\xff` → `YWJjAP8=` → `61626300ff`), and at the 4096-byte boundary.

## Bugs found and fixed during verification (no fabricated passes)

The first tbase64 decode implementation failed real checks; both defects were
found by running the harness/probes and then fixed in source:

1. Padding was converted to sextet *value* zero by writing character `'\0'`
   and then indexing `val[0]`, which is deliberately `-1`.  Observed before
   fix: `printf 'Zg==' | ... dec` → `TBASE64 1 ff` (expected `TBASE64 1 66`).
   Fixed by computing `v2`/`v3` as sextet values, not `val[]` indices.
2. The canonical-bit masks were swapped: one padding char leaves 2 unused bits
   (mask `3`) and two padding chars leave 4 (mask `15`).  Observed before fix:
   `Zm8=` was wrongly refused; after fix it decodes to `TBASE64 2 666f`, while
   the genuinely non-canonical `Zh==` is refused.

Three additional strict-build errors were fixed rather than suppressed:
pointer-parameter subscripting in `tgrep` (replaced with `__builtin_memcpy`),
`strtol`'s unmodelled out-parameter in `thead` (replaced with a bounded
parser), and a variable shift count in tbase64 decode (replaced by four-sextet
groups with literal shift counts).  `string()` with no argument also does not
lower in this compiler; `string("")` is used in `ttr`.

## Fixtures

All fixtures are generated inside a `mktemp -d` directory by `run.sh` and
removed by its EXIT trap: fixed ASCII text, an unterminated last line, an empty
file, a 300-byte line, 65537-byte and 4097-byte oversize files, fixed binary
blobs, base64 tokens (good, newline-terminated and four malformed variants),
plus 256-value and 4096-byte boundary inputs generated deterministically by
`python3`.  No clock, network, prompt, user name, machine path or game asset
is used.
