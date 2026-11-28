# Real-world-shaped utilities built on the checked C+ stdlib

Thirty small, deterministic CLI utilities in six categories, each exercising
the checked C+ standard library (`stdlib/*.hp`) and refusing hostile input with
stable, observable exit codes and tokens.

> **These are practical teaching utilities, not production tools.** Each one
> uses fixed fixtures, hard caps and a deterministic argv/stdin transcript; no
> clock, interactive prompt, network, user-specific path or shelling out from
> the utility itself. A name like `jsonfmt`, `pwdcheck` or `urlcheck` describes
> the *grammar being demonstrated*, not a production parser, checker or
> security product. See [Non-claims](#non-claims).
>
> **Refusals are runtime evidence, not a safety proof.** A `REFUSED` line means
> that, for that exact input on that run, an explicit bounds/format check or a
> checked stdlib API returned a stable nonzero code. There is no formal proof,
> no model checking, no fuzzing campaign and no sanitizer: every build uses
> `--strict --sanitize=none`. Passing the fixed cases says nothing about
> untested inputs.

## Run all utilities

```sh
sh apps/legacy_examples/utilities/run_all.sh
```

The aggregate runner discovers every `<category>/run.sh` at depth 2 below this
directory, runs each from the repository root under `timeout -k 10 300` with a
restricted `PATH` and `CPLUS_CLANG=/usr/bin/clang`, captures the **last** line
emitted by each runner that begins with its token, prints a table, and exits
nonzero on any real failure. It continues after failures and never masks a
failure with a success override (`|| true`). It uses no Python. Per-category
logs land in `build/utilities/<category>.log`; the verbatim final lines are
saved to `build/utilities/final_lines.txt`.

### Final-line contract (for category authors)

Each category's `run.sh` must print, as its last matching line, exactly one
line beginning with its token:

| Directory | Token |
| --- | --- |
| `file_tools/` | `UTIL_FILE` |
| `text_tools/` | `UTIL_TEXT` |
| `data_tools/` | `UTIL_DATA` |
| `security_tools/` | `UTIL_SECURITY` |
| `dev_tools/` | `UTIL_DEV` |
| `net_tools/` | `UTIL_NET` |

Shape: `TOKEN <tools> <passed> <failed> [refused]`.

- Field 2 is always the tool count (**5** for every category here).
- Field 3 is the passed-unit count (whole tools in some categories, individual
  checks in others — see each `NOTES.md`).
- Field 4 is the **failure count**. The aggregate treats a nonzero field 4, a
  nonzero child exit status, a missing final line, an unrecognised token, or the
  words `FAIL`/`ERROR`/`BUILD_FAIL`/`MISMATCH` as real failures.
- Field 5, when present, is an informational count of refused hostile inputs
  and is **not** a failure signal (a high refusal count is the point of the
  demos).
- `BLOCKED` / `blocked=<n>` is reported but is not a hard failure, matching the
  repository evidence convention.
- A token that never appears is reported as `PENDING` and fails the aggregate;
  no missing category is ever silently counted as a pass.

Each category owns its own build commands, fixtures and per-case evidence in
its `run.sh` and `NOTES.md`; the aggregate does not rebuild examples itself and
is not a substitute for reading those notes. The exact observed table is in
[RESULTS.md](RESULTS.md).

## Catalogue

All build commands below are run **from the repository root**
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`. The security utilities
marked with `+safecrt` append
`-- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a`; `jsonfmt`
adds `-Ithird_party/yyjson -Lbuild/ext -lyyjson`. Every build uses
`--strict --sanitize=none -I.`.

### file_tools — bounded file operations (token `UTIL_FILE`)

| Utility | One-line purpose | Build command | Checked stdlib APIs | Hostile inputs refused |
| --- | --- | --- | --- | --- |
| `fhash.cp` | FNV-1a-32 file hash with bounded, chunked reads and a known-answer self-check. | `./build/c+ build apps/legacy_examples/utilities/file_tools/fhash.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/file_tools/.run/fhash` | `file::read`, `error`, `string`, `fs_size` | File larger than 4096 bytes; directory/non-regular path; unopenable file; failed self-check. |
| `fdiff.cp` | Bounded, index-aligned diff of two files with fixed labels `a`/`b`. | `./build/c+ build apps/legacy_examples/utilities/file_tools/fdiff.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/file_tools/.run/fdiff` | `file::read_line`, `error`, `string`, `fs_size` | File over 2048 bytes; line over 64 bytes; over 32 lines; missing or bad path. |
| `fcat.cp` | Bounded `cat`: validates every line before printing so a refusal leaves no partial transcript. | `./build/c+ build apps/legacy_examples/utilities/file_tools/fcat.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/file_tools/.run/fcat` | `file::read_line`, `error`, `string`, `fs_size` | File over 2048 bytes; line over 128 bytes; over 64 lines; missing or bad path. |
| `fsize.cp` | Size listing of fixed relative paths inside a directory, with no timestamps. | `./build/c+ build apps/legacy_examples/utilities/file_tools/fsize.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/file_tools/.run/fsize` | `file`, `fs` (`fs_size`/directory check), `string`, `error` | Non-directory argument; overlong paths; missing entry; bad path. |
| `fcopy.cp` | Bounded copy with exact-fit/one-over preflight and byte-exact read-back verification. | `./build/c+ build apps/legacy_examples/utilities/file_tools/fcopy.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/file_tools/.run/fcopy` | `file::read`/write, `error`, `string`, `fs_size` | Source over capacity; destination directory/empty/overlong path; one-over destination; read-back mismatch. |

### text_tools — bounded text processing (token `UTIL_TEXT`)

| Utility | One-line purpose | Build command | Checked stdlib APIs | Hostile inputs refused |
| --- | --- | --- | --- | --- |
| `twc.cp` | `wc`-like line/word/byte counter that streams one byte at a time. | `./build/c+ build apps/legacy_examples/utilities/text_tools/twc.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/twc` | `file::read`, `error`, `string` constructor | File over 65536 bytes; cannot-open; read/close errors; wrong argc. |
| `tgrep.cp` | Substring and regex-lite (`.`, `*`) line search with stable match/refusal lines. | `./build/c+ build apps/legacy_examples/utilities/text_tools/tgrep.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/tgrep` | `file::read_line`, `string::find`, `error.category()` | Pattern of 64+ bytes; line over 256 bytes; cannot-open; wrong argc. |
| `thead.cp` | Bounded "first N lines" with strict integer parsing. | `./build/c+ build apps/legacy_examples/utilities/text_tools/thead.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/thead` | `file::read_line`, `string`, `error` | N outside [1,1000]; negative/zero/oversized/non-numeric/trailing N; overlong line; wrong argc. |
| `ttr.cp` | Fixed from/to character transliteration assembled with checked append. | `./build/c+ build apps/legacy_examples/utilities/text_tools/ttr.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/ttr` | `file::read_line`, `string::append`, `error` | Unequal set lengths; sets of 64+ bytes; overlong line; cannot-open; wrong argc. |
| `tbase64.cp` | Byte-exact base64 encode/decode with strict canonical-form validation. | `./build/c+ build apps/legacy_examples/utilities/text_tools/tbase64.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/text_tools/.run/tbase64` | `file::read`, `string`, `error` | Input over cap; length not a multiple of four; non-alphabet bytes; padding not at tail; over two `=`; non-canonical low bits. |

### data_tools — real-world-shaped data/config parsing (token `UTIL_DATA`)

| Utility | One-line purpose | Build command | Checked stdlib APIs | Hostile inputs refused |
| --- | --- | --- | --- | --- |
| `csvstat.cp` | CSV count/sum/min/max with overflow-guarded accumulation. | `./build/c+ build apps/legacy_examples/utilities/data_tools/csvstat.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/data_tools/.run/csvstat` | `file::read_line`, `string`, `error` | Overlong line; malformed row; non-numeric cell; missing header; over 64 rows; summation overflow. |
| `iniparse.cp` | Fixed INI section/key/value parser with `get`/`sections`/`keys`. | `./build/c+ build apps/legacy_examples/utilities/data_tools/iniparse.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/data_tools/.run/iniparse` | `file::read_line`, `string`, `error` | Overlong lines; oversized section/key/value names; malformed lines; keys outside a section; duplicate keys; table overflow; missing lookup. |
| `jsonfmt.cp` | Budgeted JSON parse plus deterministic canonical, indented pretty-print. | `./build/c+ build apps/legacy_examples/utilities/data_tools/jsonfmt.cp --strict --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson -o apps/legacy_examples/utilities/data_tools/.run/jsonfmt` | `json_parse_budget`, `json_view::write`, `error` | Malformed or truncated JSON; nesting deeper than 8; input over 2048 bytes. |
| `uriparse.cp` | Bounded `scheme://host[:port][/path]` parser reporting parsed components. | `./build/c+ build apps/legacy_examples/utilities/data_tools/uriparse.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/data_tools/.run/uriparse` | `string`, `error` | Query strings; fragments; userinfo; whitespace; IPv6 literals; port over 65535 or over five digits; bad scheme; URL over 512 bytes. |
| `logparse.cp` | Fixed `TIMESTAMP LEVEL MESSAGE` log parser with level filtering. | `./build/c+ build apps/legacy_examples/utilities/data_tools/logparse.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/data_tools/.run/logparse` | `file::read_line`, `string`, `error` | Overlong line; malformed line; bad timestamp shape; unknown level; missing file. |

### security_tools — checked-API refusal demonstrations (token `UTIL_SECURITY`)

These are demonstrations of additive checked-stdlib refusals, **not**
cryptographic or authentication tools: FNV-1a and CRC-32 are deliberately
non-cryptographic, and `pwdcheck` reads no real secret.

| Utility | One-line purpose | Build command | Checked stdlib APIs | Hostile inputs refused |
| --- | --- | --- | --- | --- |
| `checksum.cp` | Bounded CRC32/FNV-1a digest of one fixed argv string (`+safecrt`). | `./build/c+ build apps/legacy_examples/utilities/security_tools/checksum.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/security_tools/checksum -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a` | `cplus_strnlen_s`, `safe_error`, `error` | Argument over 32 bytes or empty → `CPLUS_EXIT_BOUNDS` (101), before any copy; failed known-answer self-check. |
| `pwdcheck.cp` | Password-policy demonstration over fixed dummy strings; never echoes the candidate (`+safecrt`). | `./build/c+ build apps/legacy_examples/utilities/security_tools/pwdcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/security_tools/pwdcheck -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a` | `cplus_strnlen_s`, `safe_error`, `error` | Candidate over 32 bytes → 101 before copy; too short (<8) or missing required character class. |
| `safe_copy.cp` | `cplus_memcpy_s` refusals with an intact canary; the unguarded "before" case runs in a forked child (`+safecrt`). | `./build/c+ build apps/legacy_examples/utilities/security_tools/safe_copy.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/security_tools/safe_copy -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a` | `cplus_memcpy_s`, `safe_memory`, `safe_error` | One-over copy → 406 `ESNOSPC`; null pointer → 400 `ESNULLP`; overlap → 404 `ESOVRLP`; child canary corruption cannot touch the parent. |
| `safe_token.cp` | Bounded tokenization of one argv string (`+safecrt`). | `./build/c+ build apps/legacy_examples/utilities/security_tools/safe_token.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/security_tools/safe_token -- build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a` | `cplus_strtok_s`, `cplus_strnlen_s`, `cplus_strncpy_s` | Input over 64 bytes; token over 16 bytes; more than four tokens. |
| `bounds_check.cp` | CLI over checked bounded queries: exact, one-over, interior pointer, stale generation. | `./build/c+ build apps/legacy_examples/utilities/security_tools/bounds_check.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/security_tools/bounds_check` | `stdlib_alloc`, `stdlib_mem_span_ok`, `stdlib_mem_query`, `error` | One-over span → 101 / category 5; stale generation key → 103 / category 7. Exact fit and interior pointer with explicit `(base,size)` are positive controls. |

### dev_tools — deterministic developer helpers (token `UTIL_DEV`)

| Utility | One-line purpose | Build command | Checked stdlib APIs | Hostile inputs refused |
| --- | --- | --- | --- | --- |
| `hexdump.cp` | Bounded 256-byte hex + ASCII dump of a fixed input file. | `./build/c+ build apps/legacy_examples/utilities/dev_tools/hexdump.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/dev_tools/hexdump` | `file::read`, `error`, `string` | Missing/unopenable file; file over 256 bytes (exact fit accepted as positive control). |
| `numconv.cp` | Dec/hex/bin conversion with overflow-safe, shift-subtract parsing. | `./build/c+ build apps/legacy_examples/utilities/dev_tools/numconv.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/dev_tools/numconv` | `string`; constant-width shift-subtract division | Value over `UINT32_MAX`; digit illegal for source base; empty value; unknown base; wrong argc. |
| `sha_lite.cp` | Deterministic FNV-1a 32-bit hash of a bounded argv string. | `./build/c+ build apps/legacy_examples/utilities/dev_tools/sha_lite.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/dev_tools/sha_lite` | `string` | Input over 64 bytes; missing or extra argument. |
| `envfilter.cp` | Filters `KEY=VALUE` argv lines by key prefix (never reads the real environment). | `./build/c+ build apps/legacy_examples/utilities/dev_tools/envfilter.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/dev_tools/envfilter` | `string` (`valid`, `size`, `substr`) | Missing `=`; empty key; illegal key character; line over 128 bytes; invalid prefix. |
| `timeconv.cp` | Epoch seconds → fixed UTC date/time with integer arithmetic only. | `./build/c+ build apps/legacy_examples/utilities/dev_tools/timeconv.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/dev_tools/timeconv` | `string` | Negative or over 253402300799 seconds; 64-bit accumulation overflow; empty or non-digit input. |

### net_tools — offline grammar validators, not network clients (token `UTIL_NET`)

None of these opens a socket, resolves a name or contacts a host.

| Utility | One-line purpose | Build command | Checked stdlib APIs | Hostile inputs refused |
| --- | --- | --- | --- | --- |
| `urlcheck.cp` | Validates `scheme://host[:port][/path]` and reports parsed components. | `./build/c+ build apps/legacy_examples/utilities/net_tools/urlcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/urlcheck` | `string` (`substr`, `find`, `field`), `error` | Bad scheme; empty authority; bad host characters; port over 65535; alpha/empty port; bad host edge; `..` path; oversized input. |
| `emailcheck.cp` | Validates `local@domain` with bounded local/domain/label rules. | `./build/c+ build apps/legacy_examples/utilities/net_tools/emailcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/emailcheck` | `string` (`substr`), `error` | Missing or duplicate `@`; empty local/domain; consecutive dots; dot-led local; domain without a dot; one-character TLD; dash-led label; over-long local/total. |
| `ipcheck.cp` | Validates IPv4 dotted quads with overflow-safe octet accumulation. | `./build/c+ build apps/legacy_examples/utilities/net_tools/ipcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/ipcheck` | `string`, `error` | Octet over 255; wrong field count; empty octet; leading zero; non-digit/negative; oversized input. |
| `portcheck.cp` | Overflow-safe parse of TCP/UDP port numbers 0..65535. | `./build/c+ build apps/legacy_examples/utilities/net_tools/portcheck.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/portcheck` | `string`, `error` | Negative; over 65535; 20-digit overflow; non-numeric/trailing text; empty; `+`; whitespace; leading zero. |
| `httpline.cp` | Validates an HTTP request line `METHOD path VERSION`. | `./build/c+ build apps/legacy_examples/utilities/net_tools/httpline.cp --strict --sanitize=none -I. -o apps/legacy_examples/utilities/net_tools/results/httpline` | `string` (`field`, `substr`), `error` | Bad method or version; two/four fields; double space; empty field; relative path; `..` path; trailing space; oversized input. |

## Per-category evidence

Each category directory contains a `NOTES.md` with the exact build commands,
every fixed scenario, its verbatim output, exit codes and the strict-mode
findings. The aggregate snapshot is [RESULTS.md](RESULTS.md). Captured terminal
transcripts and rendered SVG evidence from the separate safety-demo campaign
live under [`apps/legacy_examples/screenshots/`](../screenshots/) (see its `MANIFEST.md`).

## Non-claims

- **Not production software.** These are practical teaching utilities with
  fixed caps and fixed fixtures; no production parser, hasher, password
  checker, file tool, validator or security product is claimed.
- **No 100% safety claim.** Refusals are runtime evidence for the tested
  inputs only. The checked layers do not establish pointer provenance or full
  ownership, and a caller that does not use them can bypass them.
- **No Rust-equivalence claim.** No Rust comparison is implied by these
  utilities or by the aggregate runner.
- **macOS arm64 only.** Evidence was produced on Darwin arm64 with Apple clang
  16.0.0; no other OS or architecture is claimed.
- **A build is not a result.** Passing builds and fixed-input transcripts do
  not justify claims about untested inputs, longer runs or concurrent use.
