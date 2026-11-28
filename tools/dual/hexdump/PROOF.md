# Dual hexdump proof — `tools/dual/hexdump/`

Deterministic hex + ASCII dump utility in three implementations:

| file | implementation |
|---|---|
| `hexdump.c` | strict ISO C89-compatible pure C (`/usr/bin/clang -std=c89 -pedantic-errors`) |
| `hexdump.cp` | modern safe C+ (`stdlib/safe/safe_c.h` + `stdlib/file.hp` + `stdlib/error.hp`) |
| `benchmarks/example_rust_compare/dual/hexdump.rs` | safe Rust model (no `unsafe`) |

`run.sh` builds all of them from deterministic generated fixtures, compares
**stdout + stderr + exit status** byte for byte on valid input, exercises the
hostile cases, and prints one final `DUAL` line. Everything is written under
`tools/dual/hexdump/out/`; no checked-in source outside the two owned paths was
modified.

## Toolchain (observed, not assumed)

```
Apple clang version 16.0.0 (clang-1600.0.26.6)
Target: arm64-apple-darwin27.2.0
C+ 0.1.0  (Backend: host Clang/LLVM)
rustc 1.100.0 (0ef1bc386 2026-11-09)
host: arm64 (macOS)
```

Reproduce from the repository root:

```sh
sh tools/dual/hexdump/run.sh
```

## Output contract (valid input)

```
00000000  48 65 6c 6c 6f 2c 20 77 6f 72 6c 64 21 0a 00 01   |Hello, world!...|
00000010  02 7f 80 ff 41                                    |....A|
```

`%08lx` lowercase offset, two spaces, 16 slots of `XX ` (space-padded on the
final line), two spaces, `|ascii|` where only `0x20..=0x7e` is printed as a
character and everything else is `.`. Capacity `HD_CAP = 256`; 16 bytes/line.

## Exact command matrix — pure C standard spellings

Command per row:

```sh
/usr/bin/clang -std=<STD> -pedantic-errors -Wall -Wextra -O2 \
  -o tools/dual/hexdump/out/hexdump_c_<STD> tools/dual/hexdump/hexdump.c
```

| `<STD>` | rc | result | exact diagnostic / evidence |
|---|---:|---|---|
| `c89` | 0 | COMPILE_OK | no output; binary produced |
| `c90` | 0 | COMPILE_OK | no output; binary produced |
| `c94` | 1 | COMPILE_FAIL | `error: invalid value 'c94' in '-std=c94'` |
| `c99` | 0 | COMPILE_OK | no output; binary produced |
| `c11` | 0 | COMPILE_OK | no output; binary produced |
| `c17` | 0 | COMPILE_OK | no output; binary produced |
| `c2x` | 0 | COMPILE_OK | no output; binary produced |

Full `c94` diagnostic (verbatim, `out/compile_c94.log`):

```
error: invalid value 'c94' in '-std=c94'
note: use 'c89', 'c90', or 'iso9899:1990' for 'ISO C 1990' standard
note: use 'iso9899:199409' for 'ISO C 1990 with amendment 1' standard
note: use 'gnu89' or 'gnu90' for 'ISO C 1990 with GNU extensions' standard
note: use 'c99' or 'iso9899:1999' for 'ISO C 1999' standard
note: use 'gnu99' for 'ISO C 1999 with GNU extensions' standard
note: use 'c11' or 'iso9899:2011' for 'ISO C 2011' standard
note: use 'gnu11' for 'ISO C 2011 with GNU extensions' standard
note: use 'c17', 'iso9899:2017', 'c18', or 'iso9899:2018' for 'ISO C 2017' standard
note: use 'gnu17' or 'gnu18' for 'ISO C 2017 with GNU extensions' standard
note: use 'c2x' for 'Working Draft for ISO C2x' standard
note: use 'gnu2x' for 'Working Draft for ISO C2x with GNU extensions' standard
```

This is a **spelling** limitation of the installed clang, not a source-language
failure: the same source also compiles cleanly with the canonical C94 spelling
`/usr/bin/clang -std=iso9899:199409 -pedantic-errors ...` (rc=0) and its output
matches the C+ binary byte for byte. The requested matrix therefore records
`c94` as COMPILE_FAIL while `STDS_OK = 6`.

## Exact command matrix — modern C+ binary

```sh
./build/c+ build tools/dual/hexdump/hexdump.cp --strict --sanitize=none \
  -I. -Istdlib/safe -Ilibc/install-include \
  -o tools/dual/hexdump/out/hexdump_cp -- \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread
```

Result: `CP_BUILD OK rc=0`. The driver emits three harmless
`-Wunused-command-line-argument` warnings from its intermediate compile-only
pass; the final link succeeds (no undefined symbols). The mandated base form
`./build/c+ build hexdump.cp --strict --sanitize=none -I. -o <bin>` is extended
only with the include path for `cplus_libc.h` / `cplus_safecrt_upstream.h` and
the two SafeCRT/hardened-libc archives that back the checked `safe_c.h` calls.

## Exact command matrix — valid input (byte-identical stdout + stderr + exit)

Command per cell:

```sh
<binary> tools/dual/hexdump/out/fixture_<FIXTURE>.bin > out 2> err; echo $?
```

Fixtures are generated deterministically by `run.sh`:
`fixture_valid_small.bin` = `b"Hello, world!\n"` + `[0,1,2,127,128,255,65]` (21 B);
`fixture_valid_boundary.bin` = `bytes(range(256))` (256 B, exactly `HD_CAP`);
`fixture_oversized.bin` = `bytes(range(256)) + b'X'` (257 B).

| binary | valid_small | valid_boundary |
|---|---|---|
| pure C `c89` | MATCH rc=0/0 | MATCH rc=0/0 |
| pure C `c90` | MATCH rc=0/0 | MATCH rc=0/0 |
| pure C `c99` | MATCH rc=0/0 | MATCH rc=0/0 |
| pure C `c11` | MATCH rc=0/0 | MATCH rc=0/0 |
| pure C `c17` | MATCH rc=0/0 | MATCH rc=0/0 |
| pure C `c2x` | MATCH rc=0/0 | MATCH rc=0/0 |
| safe C+ | reference (rc=0) | reference (rc=0) |
| safe Rust model | MATCH rc=0/0 | MATCH rc=0/0 |

`c94` has no binary in this matrix because the requested spelling does not
compile (see above). `stdout`, `stderr` (both empty) and exit status were all
compared with `cmp`/integer equality. Count: **12 pure-C↔C+ matches + 2
Rust↔C+ matches = 14**.

## Exact command matrix — hostile input (refusals are runtime evidence)

| case | invocation | safe C+ stdout / exit | pure C stdout / exit |
|---|---|---|---|
| null / no argument | `<bin> </dev/null` | `HEXDUMP REFUSED bad-args code=108` / **2** | *(empty)* / **0** |
| missing path | `<bin> out/no_such_file.bin` | `HEXDUMP REFUSED cannot-open code=109` / **3** | *(empty)* / **0** |
| oversized (257 B) | `<bin> out/fixture_oversized.bin` | `HEXDUMP REFUSED oversized code=101` / **4** | 16 truncated rows of the first 256 bytes / **0** |

Codes are `CPLUS_EXIT_UNSUPPORTED=108`, `CPLUS_EXIT_IO=109`,
`CPLUS_EXIT_BOUNDS=101` from `runtime/include/cplus/cplus.h`, carried through
`stdlib/error.hp`. The safe-Rust model refuses all three with the same stdout
lines and exit codes (6 refusals total: 3 C+ + 3 Rust). The pure-C binary has
no refusal path: it falls back to stdin on a failed open, reads stdin when no
argument is given, and silently ignores input past 256 bytes.

## Verified final line

The last line printed by a real run of `sh tools/dual/hexdump/run.sh` was:

```
DUAL hexdump 6 14 6 0
```

Fields: `<stds-compiled>=6` `<matched>=14` `<refused>=6` `<failed>=0`.
Artifacts for every cell (compile logs, stdout/stderr captures, matrices,
summary env) are in `tools/dual/hexdump/out/`.

## Explicit non-claims

- **Matching output is not equivalent safety.** Byte-identical stdout for
  valid input only shows the three programs implement the same output format
  on that input. It says nothing about behavior on invalid input, memory
  safety, or provable correctness.
- **The pure-C version is NOT memory-safe.** It has no checked-read API and no
  bounds metadata; its single `n < HD_CAP` guard is a hand-written loop bound,
  not a language or runtime guarantee. This particular implementation happens
  to stop reading at 256 bytes, so the oversized demo shows *silent truncation*
  rather than a crash — that absence of a refusal is the point, not evidence
  of safety. Ordinary C can (and commonly does) overflow a fixed buffer here.
- **C+ refusals are runtime evidence, not a proof.** The three nonzero exits
  above demonstrate that the checked `file::read` / `error` / `safe_c.h` paths
  fire on the tested hostile cases in this build. They are observations of a
  finite test set, not a formal proof of absence of all memory errors, nor a
  substitute for the compiler's `--strict` analysis.
- **No live-game, platform-portability or performance claim** is made. All
  results are from the host toolchain listed above.
- **`-Wunused-command-line-argument` warnings** in the C+ build log are a
  driver quirk; they are reported verbatim rather than hidden.
