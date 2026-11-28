# Real-world utilities — C+ vs safe-Rust stdout/exit comparison

This directory holds one standalone **safe-Rust** model (`.rs`, no `unsafe`, no
FFI, no external crates) for each of the 30 NEW `examples/utilities/**/*.cp`
programs, plus a single `compare.sh` that builds the original C+ source and the
Rust model, runs both with **identical fixed inputs** under a 30-second timeout,
and compares **stdout bytes and process exit status**.

All numbers and result lines below come from actual runs, not from synthesis.
The final captured stdout/stderr of `compare.sh` are in `.build/stdout.txt` and
`.build/stderr.txt`.

## Toolchains (exact, from the captured run)

```
CMD: rustc --version
rustc 1.100.0 (0ef1bc386 2026-11-09)

CMD: ./build/c+ --version 2>&1 | head -1
C+ 0.1.0

CMD: /usr/bin/clang --version | head -2
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0

CMD: timeout --help 2>&1 | head -1
Usage: timeout [-f | --foreground] [-k time | --kill-after time] [-p | --preserve-status] [-s signal | --signal signal]  [-v | --verbose] <duration> <command> [arg ...]

CMD: uname -m
arm64
```

Repository path for the run:
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`, branch `agent/modern-stdlib`.

## Reproduction

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
PATH=/usr/bin:/bin:/usr/sbin:/sbin:"$(dirname "$(command -v rustc)")" \
CPLUS_CLANG=/usr/bin/clang LC_ALL=C \
  timeout 600 sh benchmarks/example_rust_compare/utilities/compare.sh
```

`compare.sh` writes only `UTIL_COMPARE ...` lines to stdout (30 lines, 2073
bytes, SHA-256 `6311461d6e8b8c7234e79589bada438dab68cdeb2c7019f86599ce0a9c5588f5`);
build commands, per-utility stdout hashes and the final
`SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0` line go to stderr.

## Exact build and run commands used by `compare.sh`

All builds run with cwd = repository root so `stdlib/*.hp` includes resolve.

C+ (default: 26 of 30 utilities), with per-source extra flags where documented:

```sh
# all except jsonfmt and the four safeclib-users
timeout 30 ./build/c+ build examples/utilities/<group>/<name>.cp \
  --strict --sanitize=none -I. -o <out>/c_<name>

# jsonfmt (needs the bundled yyjson headers/library)
timeout 30 ./build/c+ build examples/utilities/data_tools/jsonfmt.cp \
  --strict --sanitize=none -I. -Ithird_party/yyjson -Lbuild/ext -lyyjson \
  -o <out>/c_jsonfmt

# checksum / pwdcheck / safe_copy / safe_token (need the safeclib-backed archives)
timeout 30 ./build/c+ build examples/utilities/security_tools/<name>.cp \
  --strict --sanitize=none -I. -o <out>/c_<name> -- \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a
```

Rust (all 30):

```sh
timeout 30 rustc -O --edition 2021 <name>.rs -o <out>/r_<name>
```

Run (each fixed case; identical arguments and empty stdin for both sides):

```sh
timeout 30 <binary> <fixed args>   # stdout appended, exit code recorded
```

Comparison: `cmp -s <c.stdout> <r.stdout> && cmp -s <c.rc> <r.rc>`.

## Fixed inputs (identical for both sides)

Fixtures are rewritten with fixed bytes by `compare.sh` on every run (so no
pre-existing file content can influence the result): `text.txt`, `csv.csv`,
`nodata.csv`, `ini.ini`, `diff_a.txt`, `diff_b.txt`, `bin.dat` (contains a NUL),
`b64.txt`, `log.txt`, `oversized.bin` (257 B), `big4097.bin`, `big65537.bin`.

| Utility | Fixed invocations (cases concatenated for the comparison) |
|---|---|
| `fhash` | valid 14-byte file; missing file; 4097-byte file (refusal) |
| `fdiff` | two different files; identical files |
| `fcat` | 4-line fixture |
| `fsize` | two existing relative entries; one missing entry |
| `fcopy` | copy 38-byte file (then readback/verify); 4097-byte source (refusal) |
| `twc` | 38-byte text; 65537-byte file (oversize refusal) |
| `tgrep` | plain substring `bar`; wildcard `b.*z` |
| `thead` | `2`; `0` (refused N) |
| `ttr` | `abc -> xyz` over the text fixture |
| `tbase64` | `enc` of the binary fixture; `dec` of a token fixture |
| `csvstat` | `stats`, `sum`, `min`, `max`, `count`; no-data file; bad command |
| `iniparse` | `sections`; `keys server`; `get server host`; `get server missing`; bad command |
| `jsonfmt` | nested object; truncated JSON; empty string |
| `uriparse` | `https://example.com:8443/a/b`; `http://example.com`; `ftp://host/path`; query string |
| `logparse` | filter `INFO`; `all`; `TRACE`; invalid filter |
| `checksum` | `hello`; 33-byte input; empty input; no argument |
| `pwdcheck` | strong candidate `Str0ng!Pass`; too-short candidate |
| `safe_copy` | `exact`, `over`, `overlap`, `null`, `nullsrc`, `raw`, unknown case |
| `safe_token` | 4 valid tokens; 17-byte token; 5 tokens |
| `bounds_check` | `exact`, `one-over`, `interior`, `stale` |
| `hexdump` | 14-byte binary fixture; 257-byte file (oversized refusal) |
| `numconv` | `255 dec hex`; `101010 bin dec`; `ff hex bin`; overflow; invalid digit |
| `sha_lite` | `hello`; 65-byte input (refusal) |
| `envfilter` | prefix `APP_` over four `KEY=VALUE` arguments |
| `timeconv` | `0`; `1700000000`; `253402300799`; `-1`; u64 overflow; non-digit |
| `urlcheck` | two argv URLs added to the built-in hostile suite |
| `emailcheck` | two argv addresses added to the built-in hostile suite |
| `ipcheck` | two argv IPs added to the built-in hostile suite |
| `portcheck` | two argv ports added to the built-in hostile suite |
| `httpline` | two argv request lines added to the built-in hostile suite |

Total: **83 fixed invocations** across 30 utilities. The five
`*check` harnesses (`urlcheck`, `emailcheck`, `ipcheck`, `portcheck`,
`httpline`) print their built-in case tables plus one line per argv case in a
single invocation, so their "1 fixed case" count is one process run, not one
output line.

## Captured result lines (verbatim stdout of `compare.sh`)

```
UTIL_COMPARE fhash MATCH stdout+exit identical for 3 fixed case(s)
UTIL_COMPARE fdiff MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE fcat MATCH stdout+exit identical for 1 fixed case(s)
UTIL_COMPARE fsize MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE fcopy MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE twc MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE tgrep MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE thead MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE ttr MATCH stdout+exit identical for 1 fixed case(s)
UTIL_COMPARE tbase64 MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE csvstat MATCH stdout+exit identical for 7 fixed case(s)
UTIL_COMPARE iniparse MATCH stdout+exit identical for 5 fixed case(s)
UTIL_COMPARE jsonfmt MATCH stdout+exit identical for 3 fixed case(s)
UTIL_COMPARE uriparse MATCH stdout+exit identical for 4 fixed case(s)
UTIL_COMPARE logparse MATCH stdout+exit identical for 4 fixed case(s)
UTIL_COMPARE checksum MATCH stdout+exit identical for 4 fixed case(s)
UTIL_COMPARE pwdcheck MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE safe_copy MATCH stdout+exit identical for 7 fixed case(s)
UTIL_COMPARE safe_token MATCH stdout+exit identical for 3 fixed case(s)
UTIL_COMPARE bounds_check MATCH stdout+exit identical for 4 fixed case(s)
UTIL_COMPARE hexdump MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE numconv MATCH stdout+exit identical for 5 fixed case(s)
UTIL_COMPARE sha_lite MATCH stdout+exit identical for 2 fixed case(s)
UTIL_COMPARE envfilter MATCH stdout+exit identical for 1 fixed case(s)
UTIL_COMPARE timeconv MATCH stdout+exit identical for 6 fixed case(s)
UTIL_COMPARE urlcheck MATCH stdout+exit identical for 1 fixed case(s)
UTIL_COMPARE emailcheck MATCH stdout+exit identical for 1 fixed case(s)
UTIL_COMPARE ipcheck MATCH stdout+exit identical for 1 fixed case(s)
UTIL_COMPARE portcheck MATCH stdout+exit identical for 1 fixed case(s)
UTIL_COMPARE httpline MATCH stdout+exit identical for 1 fixed case(s)
```

Script exit status: **0**. Final stderr summary line:
`SUMMARY different=0 inconclusive(BLOCKED+PENDING)=0`.

## Totals

| MATCH | DIFFERENT | BLOCKED | PENDING | Total |
|---:|---:|---:|---:|---:|
| 30 | 0 | 0 | 0 | 30 |

Earlier in the session two models initially produced `DIFFERENT` (`jsonfmt`
indent depth, `timeconv` month math after a sibling updated the C+ source).
Both were diagnosed from `.build/*.diff`, fixed in the Rust model, and the
final run above has zero differences. No `MATCH` was fabricated.

## Per-utility evidence

Combined SHA-256 of the C and Rust stdout files is identical for every utility;
byte counts are from `wc -c` on the captured combined-stdout files.

| Utility | C+ source | Rust model | stdout bytes (C / Rust) | Cases | Result |
|---|---|---|---:|---:|---|
| `fhash` | `file_tools/fhash.cp` | `fhash.rs` | 83 / 83 | 3 | **MATCH** |
| `fdiff` | `file_tools/fdiff.cp` | `fdiff.rs` | 217 / 217 | 2 | **MATCH** |
| `fcat` | `file_tools/fcat.cp` | `fcat.rs` | 87 / 87 | 1 | **MATCH** |
| `fsize` | `file_tools/fsize.cp` | `fsize.rs` | 93 / 93 | 2 | **MATCH** |
| `fcopy` | `file_tools/fcopy.cp` | `fcopy.rs` | 65 / 65 | 2 | **MATCH** |
| `twc` | `text_tools/twc.cp` | `twc.rs` | 31 / 31 | 2 | **MATCH** |
| `tgrep` | `text_tools/tgrep.cp` | `tgrep.rs` | 72 / 72 | 2 | **MATCH** |
| `thead` | `text_tools/thead.cp` | `thead.rs` | 69 / 69 | 2 | **MATCH** |
| `ttr` | `text_tools/ttr.cp` | `ttr.rs` | 66 / 66 | 1 | **MATCH** |
| `tbase64` | `text_tools/tbase64.cp` | `tbase64.rs` | 67 / 67 | 2 | **MATCH** |
| `csvstat` | `data_tools/csvstat.cp` | `csvstat.rs` | 130 / 130 | 7 | **MATCH** |
| `iniparse` | `data_tools/iniparse.cp` | `iniparse.rs` | 172 / 172 | 5 | **MATCH** |
| `jsonfmt` | `data_tools/jsonfmt.cp` | `jsonfmt.rs` | 161 / 161 | 3 | **MATCH** |
| `uriparse` | `data_tools/uriparse.cp` | `uriparse.rs` | 204 / 204 | 4 | **MATCH** |
| `logparse` | `data_tools/logparse.cp` | `logparse.rs` | 298 / 298 | 4 | **MATCH** |
| `checksum` | `security_tools/checksum.cp` | `checksum.rs` | 466 / 466 | 4 | **MATCH** |
| `pwdcheck` | `security_tools/pwdcheck.cp` | `pwdcheck.rs` | 227 / 227 | 2 | **MATCH** |
| `safe_copy` | `security_tools/safe_copy.cp` | `safe_copy.rs` | 675 / 675 | 7 | **MATCH** |
| `safe_token` | `security_tools/safe_token.cp` | `safe_token.rs` | 324 / 324 | 3 | **MATCH** |
| `bounds_check` | `security_tools/bounds_check.cp` | `bounds_check.rs` | 269 / 269 | 4 | **MATCH** |
| `hexdump` | `dev_tools/hexdump.cp` | `hexdump.rs` | 123 / 123 | 2 | **MATCH** |
| `numconv` | `dev_tools/numconv.cp` | `numconv.rs` | 132 / 132 | 5 | **MATCH** |
| `sha_lite` | `dev_tools/sha_lite.cp` | `sha_lite.rs` | 52 / 52 | 2 | **MATCH** |
| `envfilter` | `dev_tools/envfilter.cp` | `envfilter.rs` | 65 / 65 | 1 | **MATCH** |
| `timeconv` | `dev_tools/timeconv.cp` | `timeconv.rs` | 196 / 196 | 6 | **MATCH** |
| `urlcheck` | `net_tools/urlcheck.cp` | `urlcheck.rs` | 755 / 755 | 1 | **MATCH** |
| `emailcheck` | `net_tools/emailcheck.cp` | `emailcheck.rs` | 704 / 704 | 1 | **MATCH** |
| `ipcheck` | `net_tools/ipcheck.cp` | `ipcheck.rs` | 620 / 620 | 1 | **MATCH** |
| `portcheck` | `net_tools/portcheck.cp` | `portcheck.rs` | 544 / 544 | 1 | **MATCH** |
| `httpline` | `net_tools/httpline.cp` | `httpline.rs` | 739 / 739 | 1 | **MATCH** |

Build/run artifacts are kept in
`benchmarks/example_rust_compare/utilities/.build/`
(per-utility `<name>.c.build.log`, `<name>.rs.build.log`,
`<name>.cases.c.stdout`, `<name>.cases.r.stdout`, `<name>.cases.c.rc`,
`<name>.cases.r.rc`, and `<name>.diff` only for differences).

## Exit-status semantics (verified)

`compare.sh` exits nonzero **only on a real `DIFFERENT`**. `BLOCKED` (a C+ or
Rust build failure) and `PENDING` (a scoped source or Rust model not created
yet) are reported as `UTIL_COMPARE ...` lines but are deliberately inconclusive
and do **not** fail the script. This was verified with controlled runs using
environment overrides and isolated output directories (no result file faked):

| Scenario | Override | Observed stdout | Exit | As required |
|---|---|---|---:|---|
| All MATCH (final run) | none | 30 MATCH | 0 | yes |
| All `BLOCKED` | `UC_RUSTC=false UC_OUT=<temp>` | 30 BLOCKED | 0 | yes |
| One `PENDING` | temporarily hid `twc.rs`, `UC_OUT=<temp>` | 1 PENDING + 29 MATCH | 0 | yes |
| All `DIFFERENT` | stub `rustc` printing a wrong line, `UC_OUT=<temp>` | 30 DIFFERENT | 30 | yes |

The `twc.rs` file was restored immediately after the PENDING check; the final
run above is with all 30 models present.

## `PENDING` behaviour and exact refresh command

If a scoped C+ source (or its Rust model) is absent at run time, `compare.sh`
reports it as `PENDING` before any build or comparison:

```
UTIL_COMPARE <name> PENDING source-not-yet-created <abs path>; refresh: sh <here>/compare.sh
UTIL_COMPARE <name> PENDING rust-model-not-yet-created <abs path>; refresh: sh <here>/compare.sh
```

Exact command to refresh after a missing source appears:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh benchmarks/example_rust_compare/utilities/compare.sh
```

No `MATCH` is ever fabricated for a missing source.

## How each C+ feature was modeled (and what differs)

- **File utilities** (`fhash`, `fdiff`, `fcat`, `fsize`, `fcopy`) — checked
  `fs_size`/`fs_exists`/`fs_is_file` preflights become `fs::symlink_metadata`;
  `file::read`/`read_line` with `error *` become `fs::read` plus explicit
  bounds/line splitting; FNV-1a uses `wrapping_mul`. Difference: C+ streams
  through fixed stack buffers with compile-time extents and runtime refusal;
  Rust uses owned `Vec<u8>`/`String` and `Result`.
- **Text utilities** (`twc`, `tgrep`, `thead`, `ttr`, `tbase64`) — `string` /
  `file::read_line` / checked `append` become `String`, `Vec<u8>` and checked
  slicing. The regex-lite wildcard matcher is a direct port of `glob_full`.
  Difference: C+ relies on `string::find`/`substr` checked APIs; Rust uses
  `str`/`[u8]` methods and lossy UTF-8 conversion for rendering arbitrary file
  bytes.
- **Data utilities** (`csvstat`, `iniparse`, `jsonfmt`, `uriparse`, `logparse`)
  — fixed tables become `Vec`/`HashMap`-style owned collections; `strtoll`
  and checked sum become `i64::parse` plus `checked_add`; `json_parse_budget`
  and yyjson canonicalization become a small recursive JSON parser and a
  direct port of the depth-based pretty renderer. Difference: C+ uses fixed
  global 2-D arrays, strict-mode-proven copies and an external yyjson library;
  Rust uses owned vectors and a self-contained parser. Only the deterministic
  inputs exercised were compared.
- **Security utilities** (`checksum`, `pwdcheck`, `safe_copy`, `safe_token`,
  `bounds_check`) — `cplus_strnlen_s`/`cplus_strncpy_s` bounded queries become
  `String::len` and slicing; bitwise CRC-32/FNV-1a are direct ports. The
  `safe_copy` "raw" case performs an out-of-bounds copy in a forked child in
  C+; the Rust model reproduces only its deterministic **transcript** with
  safe code and performs no out-of-bounds write. Difference: C+ refusal
  categories/codes come from safeclib's global handler (whose
  `IGNORE CONSTRAINT HANDLER` stdout line is reproduced verbatim); Rust has
  no such global side channel.
- **Dev utilities** (`hexdump`, `numconv`, `sha_lite`, `envfilter`, `timeconv`)
  — `printf("...%08x...")` becomes `println!("{:08x}")`; shift-subtract
  division in `numconv` becomes safe `checked_mul`/`checked_add`; the
  Gregorian civil-from-days arithmetic is ported with `i64`. Difference: C+
  refuses via explicit guards to satisfy strict mode; Rust uses checked
  arithmetic and `Option`.
- **Net validators** (`urlcheck`, `emailcheck`, `ipcheck`, `portcheck`,
  `httpline`) — each C+ built-in case table and validator is reproduced with
  safe string splitting/comparisons and `u32` arithmetic; refusal codes and
  category names are emitted identically. Difference: C+ uses the checked
  `string` API (`count_fields`, `field`, `substr`) and an `error` object with
  `stdlib_category_name`; Rust uses `str::split`, `find` and literal codes.

Every Rust program contains **no `unsafe` blocks, no FFI, no raw pointers and
no external crates**. Each `.rs` compiles standalone with
`rustc -O --edition 2021`.

## Explicit non-equivalence statement

**A `MATCH` here means only: for the fixed, deterministic arguments and empty
stdin used, the original C+ executable and the safe-Rust model produced the
same stdout bytes and the same process exit codes.** It does **not** mean the
two programs are language-, ABI-, implementation- or safety-equivalent. The
Rust files are idiomatic *models of observable behaviour*, not translations of
the C+ lowering pipeline: `.hp` header lowering, checked `string`/`file`/`fs`
wrappers, the strict checker's fail-closed refusals, checked-allocator side
tables, `error *` categories, safeclib constraint handlers and C+ RAII have no
direct Rust syntax analogue and were replaced with `Vec`/`String`/`Result`,
`Option`, `checked_add`, iterators and Rust's type-system safety. Memory
layout, allocation strategy, integer-overflow diagnostics, exact refusal
mechanisms and compile-time guarantees were **not** compared. No claim is made
that any utility is memory-safe, that the two languages give equivalent safety
guarantees, or that passing these comparisons proves Rust equivalence. The
`safe_copy` model in particular is a transcript-only model of an out-of-bounds
C+ demonstration and does not reproduce the unsafe operation itself.
