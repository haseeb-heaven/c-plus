# Original CLI Project Evidence — `xdump`

Subagent 3/10 — task: download a real, already-made C CLI utility, verify compile and
run, and preserve original source and ABI. All files created by this subagent live
under `external_projects/cli_app/` only. No `git add`/commit performed.

## 1. Provenance

| Field | Value |
|---|---|
| Project | **xdump** — a filter that displays a file (or stdin) in hexadecimal and ASCII |
| Repository | `https://github.com/xfgusta/xdump` |
| Downloaded from (pinned tree tarball) | `https://github.com/xfgusta/xdump/archive/97fdfbdc1e49c10aabac736a57b0256251b39b66.tar.gz` |
| Commit SHA (exact, pinned) | `97fdfbdc1e49c10aabac736a57b0256251b39b66` |
| Default branch | `main` |
| Commit author / date | Gustavo Costa `<xfgusta@gmail.com>` — author date `2022-09-03T11:21:53-03:00`, commit date `2022-09-03T11:21:53-03:00` |
| Commit subject | `Set variables only if it's not already set` |
| License | **MIT** (permissive; no GPL). Copyright (c) 2021 Gustavo Costa. |
| License source | In-repo `LICENSE` file at the pinned commit, fetched from `https://raw.githubusercontent.com/xfgusta/xdump/main/LICENSE`, and independently verified inside the pinned tarball (`xdump-97fdfbdc.../LICENSE`). The `README.md` at the same commit also states "Distributed under the MIT license." |
| Verification date | `2026-11-23T06:48:56Z` (UTC) |

Verification chain for provenance/labeling:

- `git ls-remote https://github.com/xfgusta/xdump.git HEAD refs/heads/main` returned
  `97fdfbdc1e49c10aabac736a57b0256251b39b66` for both `HEAD` and `refs/heads/main`.
- A shallow `git clone` at that commit reported the metadata in the table above.
- The pinned tarball was downloaded over HTTPS and extracted; its `LICENSE` text is
  standard MIT, byte-identical to the raw `LICENSE` fetched from the repo.
- The `xdump.c` file fetched via `raw.githubusercontent.com/.../main/xdump.c` was
  byte-identical (`cmp`) to the `xdump.c` inside the pinned tarball, confirming the
  branch content and the pinned SHA agree.

Selection rationale: single self-contained C99 source file (`xdump.c`, 9,828 bytes),
no external dependencies beyond libc/POSIX, a genuine standalone CLI with documented
`argv` options, and a clearly verifiable MIT license — fits the requested "small
self-contained CLI C app" category (hexdump/checksum-style inspection tool).

## 2. Preserved original files and SHA-256

Stored under `external_projects/cli_app/original/`, copied byte-for-byte from the
extracted pinned tarball (`xdump-97fdfbdc.../`). Two independent `cmp` checks against
the tarball — one immediately after copying and one final check at the end — both passed.

| File | Size (bytes) | SHA-256 |
|---|---|---|
| `original/xdump.c` | 9828 | `93106c3b4b7bb4f0608bd3308621eca30d8ab7c4c7883b23f54d3c7fe0309253` |
| `original/LICENSE` | 1070 | `a26531ee3ee8f9e12415618e04293813c4f248e4f8bc759c6ae0fb89ec3560a8` |

Pinned tarball SHA-256 (for reference): `e138b247f86ed64065fd4358c345b6ccfd2b1982ccd6b1fa16a12628e9e93ee2`.

Fixture SHA-256 (deterministic 52-byte input, generated locally, not upstream):
`13d85a5e73340137a5ae38d4b8d1bbe34ab2539ed2974fc5a457d44edc10972c`
(bytes 0x00–0x1f, `Hello, xdump!\n`, then `7f 80 c3 a9 ff 00`).

**Original-unchanged statement:** `original/xdump.c` and `original/LICENSE` are
byte-for-byte identical to the files in the upstream tarball at commit
`97fdfbdc1e49c10aabac736a57b0256251b39b66`. No upstream file was edited, patched,
or reformatted. The exact compile command builds the preserved source directly from
`original/xdump.c` with no source modifications.

## 3. Compile results

Compiler: `/usr/bin/clang --version` → Apple clang version 16.0.0 (clang-1600.0.26.6),
target arm64-apple-darwin, installed via Command Line Tools. Platform: macOS (darwin), arm64.

### Required compile (plain `/usr/bin/clang -std=c99`)

Exact command (also saved to `build/compile.command.txt`):

```
/usr/bin/clang -std=c99 original/xdump.c -o build/xdump
```

- **Exit code: 0**
- stdout: empty
- stderr: empty (zero warnings)
- Output binary: `build/xdump` — Mach-O 64-bit executable arm64, 52,296 bytes.

### Ancillary compile checks (documented for completeness, not required by task)

| Command | Exit | Result |
|---|---|---|
| `/usr/bin/clang -std=c99 -Wall -Wextra -pedantic original/xdump.c -o build/xdump_c99_warn` | 0 | Builds; 2 warnings, both `-Wstrict-prototypes` on `display_help()` (line 246) and `display_version()` (line 262). Full text in `build/compile_c99_warn.stderr`. |
| `/usr/bin/clang -std=c99 -Wall -Wextra -Werror -pedantic -O2 original/xdump.c -o build/xdump_strict` | **1** | Fails *only* because `-Werror` promotes those two `-Wstrict-prototypes` warnings. Exact error is preserved verbatim in `build/compile_strict.stderr`. Example error line: `original/xdump.c:246:18: error: a function declaration without a prototype is deprecated in all versions of C [-Werror,-Wstrict-prototypes]`. |
| `/usr/bin/clang -Wall -Wextra -Werror -pedantic -std=c11 -O2 original/xdump.c -o build/xdump_upstream` | **1** | Same two `-Wstrict-prototypes` errors, proving this is an Apple-clang-16 default diagnostic and **not** a C99-mode incompatibility: it also fails with the project's own upstream makefile flags (`-std=c11 -Wall -Wextra -Werror -pedantic`). No source defect specific to C99 was found, and no source change was made. |

Note: the upstream `makefile` uses `CC ?= gcc` and `-std=c11`; with the available
`/usr/bin/clang` the two `()`-style definitions trigger Apple clang's
`-Wstrict-prototypes`, which upstream GCC builds would not promote to an error.
This is recorded as-is; the original source was intentionally left unchanged.

## 4. Run results (all under `timeout 30`)

Each run used the exact utility `/usr/bin/timeout` (verified present) with a 30-second
limit, the compiled `build/xdump`, fixed argv, and fixed stdin/file input. Outputs
were captured to `build/runs/<case>.{stdout,stderr,exit}`; the fixture is
`build/fixtures/sample.bin`. Color was automatically disabled because stdout is not
a TTY (matching upstream behavior).

| Case | Command (argv) | stdin | Exit | stdout bytes | stderr bytes |
|---|---|---|---|---|---|
| `version` | `./build/xdump -v` | empty | 0 | 20 | 0 |
| `help` | `./build/xdump -h` | empty | 0 | 433 | 0 |
| `file_default` | `./build/xdump build/fixtures/sample.bin` | empty | 0 | 313 | 0 |
| `file_width8` | `./build/xdump -w 8 -g 4 build/fixtures/sample.bin` | empty | 0 | 334 | 0 |
| `file_skip_n` | `./build/xdump -s 32 -n 16 build/fixtures/sample.bin` | empty | 0 | 88 | 0 |
| `stdin_default` | `./build/xdump` | `< build/fixtures/sample.bin` | 0 | 313 | 0 |
| `bad_flag` | `./build/xdump -Z` | empty | 1 | 0 | 35 |
| `missing_file` | `./build/xdump /definitely/not/a/file` | empty | 0 | 0 | 71 |

No case hit the 30-second timeout; every run completed immediately.

### Captured outputs (verbatim)

`version` stdout:
```
xdump version 1.0.0
```

`help` stdout:
```
Usage: xdump [-h] [-v] [-s OFFSET] [-n LENGTH] [-w COUNT] [-g SIZE] [FILE...]

Options:
  -h         display this help and exit
  -v         output version information and exit
  -s OFFSET  skip OFFSET bytes from the beginning of the input
  -n LENGTH  interpret only LENGTH bytes of input
  -w COUNT   print COUNT hex per line
  -g SIZE    group output into SIZE-hex groups

Report bugs to <https://github.com/xfgusta/xdump/issues>
```

`file_default` and `stdin_default` stdout (identical — file mode and stdin mode agree):
```
00000000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  |................|
00000010  10 11 12 13 14 15 16 17  18 19 1a 1b 1c 1d 1e 1f  |................|
00000020  48 65 6c 6c 6f 2c 20 78  64 75 6d 70 21 0a 7f 80  |Hello, xdump!...|
00000030  c3 a9 ff 00                                       |....|
00000034
```

`file_width8` stdout (`-w 8 -g 4` — 8 columns, grouping after every 4 bytes):
```
00000000  00 01 02 03  04 05 06 07  |........|
00000008  08 09 0a 0b  0c 0d 0e 0f  |........|
00000010  10 11 12 13  14 15 16 17  |........|
00000018  18 19 1a 1b  1c 1d 1e 1f  |........|
00000020  48 65 6c 6c  6f 2c 20 78  |Hello, x|
00000028  64 75 6d 70  21 0a 7f 80  |dump!...|
00000030  c3 a9 ff 00               |....|
00000034
```
Authoritative capture: `build/runs/file_width8.stdout`.

`file_skip_n` stdout (`-s 32 -n 16` — starts at offset 0x20, stops after 16 bytes):
```
00000020  48 65 6c 6c 6f 2c 20 78  64 75 6d 70 21 0a 7f 80  |Hello, xdump!...|
00000030
```

`bad_flag` stderr (exit 1):
```
./build/xdump: illegal option -- Z
```

`missing_file` stderr (exit 0 — upstream continues after a failed `fopen`):
```
xdump: cannot open "/definitely/not/a/file": No such file or directory
```

All other stdout/stderr streams for these cases were empty.

## 5. Exported symbols (`nm`)

Command: `nm -g --defined-only build/xdump` (full output preserved in `build/nm_defined.txt`).

```
0000000100000000 T __mh_execute_header
000000010000802c D _color_ascii
000000010000801c D _color_bar
0000000100008030 D _color_nonascii
0000000100008020 D _color_nul
0000000100008018 D _color_offset
0000000100008024 D _color_printable
0000000100008028 D _color_whitespace
0000000100008008 D _columns_opt
00000001000034bc T _display_help
00000001000034d8 T _display_version
0000000100002724 T _get_color_code
0000000100008010 D _group_opt
0000000100008000 D _length_opt
0000000100003508 T _main
0000000100008038 S _no_color_is_on
00000001000027f0 T _pretty_printf
00000001000031d8 T _set_new_colors
0000000100008040 S _skip_opt
0000000100003114 T _str_to_color_code
0000000100003424 T _str_to_long
0000000100002890 T _xdump
```

Note: the authoritative symbol listing is the file `build/nm_defined.txt` captured
directly from `nm`; the block above is transcribed verbatim from it. All global defined symbols are the
expected ones from this single translation unit plus `__mh_execute_header`; there is
no exported-library ABI in this CLI executable, and it was built exactly once from
the unmodified `original/xdump.c`.

Undefined (imported libc/POSIX) symbols were also captured via `nm -u build/xdump`
in `build/nm_undefined.txt` (e.g. `_fopen`, `_fread`, `_getopt`, `_printf`,
`_isatty`, `_strtol`, `_exit`), confirming linkage against the normal C runtime with
no third-party dependency.

### ABI preservation statement

The compiled artifact is a native Mach-O arm64 executable built from the unmodified
preserved source. No headers, structs, calling conventions, compiler feature flags
beyond the required `-std=c99`, or source lines were changed. The `nm -u` symbol set
matches a plain libc/POSIX CLI. Source SHA-256 values above are the ABI/source
preservation evidence; recompiling `original/xdump.c` with the exact command in §3
reproduces the same exported symbol set.

## 6. Files created (all inside `external_projects/cli_app/`)

- `original/xdump.c`, `original/LICENSE` — byte-for-byte upstream originals.
- `ORIGINAL_EVIDENCE.md` — this document.
- `build/` — compile commands, stdout/stderr/exit captures, strict/upstream compile checks,
  `nm` captures, the deterministic fixture, and the compiled `xdump` binary plus run
  captures under `build/runs/`.

No files outside `external_projects/cli_app/` were created or modified by this
subagent, and no git staging/commit was performed.
