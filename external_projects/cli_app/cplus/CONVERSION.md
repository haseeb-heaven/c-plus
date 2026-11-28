# cli_app (`xdump`) — modern C+ conversion: evidence & results

Status: **COMPLETE**. The real upstream CLI was converted to modern C+ in
`cplus/xdump_modern.cp`, built in `--strict --sanitize=none` mode, and replayed
against the original's recorded fixed-input corpus. Result:

```
CONVERT cli_app 24 24 0
```

`24` checks = 8 cases x 3 streams (stdout, stderr, exit); `24` matched byte for
byte; `0` differed. Verification command: `bash external_projects/cli_app/cplus/verify.sh`
(exit 0). Every number below was produced by an actual command in this
workspace; nothing is reconstructed from memory.

---

## 1. Provenance

The source under conversion is the real, downloaded, unmodified upstream CLI
`xdump` (hex/ASCII file dumper). Provenance is established by the sibling
agent's `external_projects/cli_app/ORIGINAL_EVIDENCE.md` (read-only input to
this task):

| Field | Value |
|---|---|
| Project | `xfgusta/xdump` — hex/ASCII dump utility |
| Pinned commit | `97fdfbdc1e49c10aabac736a57b0256251b39b66` |
| License | MIT, Copyright (c) 2021 Gustavo Costa (permissive) |
| Original source | `external_projects/cli_app/original/xdump.c` |
| Original SHA-256 | `93106c3b4b7bb4f0608bd3308621eca30d8ab7c4c7883b23f54d3c7fe0309253` |
| Byte count | 9,828 |
| Reference outputs | `external_projects/cli_app/build/runs/<case>.{stdout,stderr,exit}` |
| Reference binary | `external_projects/cli_app/build/xdump` (clang -std=c99) |

The original SHA-256 was re-measured during this conversion and still equals
the value recorded in `ORIGINAL_EVIDENCE.md` §2 — the original file was not
edited. No file outside `external_projects/cli_app/cplus/` was created or
modified by this agent; no `git add`/commit was performed.

## 2. Conversion unit

| Item | Value |
|---|---|
| Input | `external_projects/cli_app/original/xdump.c` (read only) |
| Output | `external_projects/cli_app/cplus/xdump_modern.cp` |
| Modern source SHA-256 | `677f39e6ab44a03a03746151785b24399e5104f2b909cd5c3af781139bf10e90` |
| Lines | 360 (original) → 414 (modern), including 18-line provenance/method header |
| Build | `./build/c+ build external_projects/cli_app/cplus/xdump_modern.cp --strict --sanitize=none -I. -o external_projects/cli_app/cplus/xdump_modern` |
| Strict check | `./build/c+ check ... --strict --sanitize=none -I.` — passes (no `[CPLUS-…]` errors) |
| Verification | `bash external_projects/cli_app/cplus/verify.sh` → `CONVERT cli_app 24 24 0`, exit 0 |

Full unified diff: `cplus/source_diff.txt`. Build stderr (warnings only):
`cplus/results/build.stderr`.

## 3. Line-level changes (conservative modernization)

The conversion preserves the original program structure and all global
symbols. Every change below is either required by strict mode or an explicit
C+ stdlib adoption; no CLI behavior was changed.

| Original line(s) | Modern change | Reason |
|---|---|---|
| 1–10 (includes) | Added `#include "stdlib/string.hp"`, `#include "stdlib/file.hp"`, `#include "stdlib/error.hp"` after the original libc includes. | Adopt the C+ stdlib surface (owned/borrowed types, checked allocator, error model) without removing any original include. |
| 46–59 `pretty_printf` | Added `string_view fmt_view = string_view(fmt, strlen(fmt));`; replaced the single `vprintf(fmt, args)` with a `string_view::equals` literal dispatch over the four formats actually used (`%08lx`, `%02x`, `%c`, plus a `printf("%s", fmt)` fallback for `"|"` / `"."`); kept the surrounding color escape logic and the `char *fmt, ...` signature byte-for-byte. | Strict mode rejects a non-literal format passed to `vprintf` (`[CPLUS-FFI]` CWE-134). The dispatch proves the format is one of the known literals. All real call sites pass literals, so emitted bytes are identical (confirmed by all 24 stream comparisons). |
| 62 `unsigned char buffer[columns_opt];` | Replaced the variable-length stack array with `unsigned char *buffer = (unsigned char *)stdlib_alloc((size_t)columns_opt);` plus a `buffer == 0` early return. | Strict mode rejects an unbounded VLA (`[CPLUS-OWNER]`). `stdlib_alloc` is the checked C+ allocator; it treats a 0-byte request as 1 byte, preserving `-w 0` behavior. Managed memory is released at process exit. |
| 65 `long group = ...` region | Inserted `if(group == 0) return;` after `group` is computed. | Strict mode cannot exclude a zero modulo divisor (`[CPLUS-ARITH]`). The original divides by zero for `-w 1` with no `-g` (UB); failing closed there is a safety improvement. For the fixed corpus `group` is 8 or 4, so no output changes. This guard also gives the checker the proof needed to keep `if(i % group == 0)` itself **byte-identical** to the original. |
| 180, 235 `char *endptr;` | Initialized both to `(char *)0`. | Strict mode rejects read-before-initialize (`[CPLUS-LIFETIME]`). `strtol` always assigns `endptr` before it is read, so behavior is unchanged. |
| 329–331 `for(int i=0; i<argc; i++) { ... char *filename = argv[i]; }` | Added `char **arg_cursor = argv;` and advanced it in the loop (`i++, arg_cursor++`), reading `char *filename = *arg_cursor;`. Loop bound `i < argc` is unchanged. | Strict mode rejects subscripting a pointer parameter of unverifiable extent (`[CPLUS-FFI]`). Pointer walking is semantically identical over `0..argc-1`. |
| 356–357 `else xdump(stdin, "(standard input)");` | Wrapped stdin with the C+ stdlib borrowed-file helper: `file std_input = file_borrow(stdin); xdump(std_input.native(), "(standard input)");`. | Adopts `stdlib/file.hp` as a non-owning handle (stdin remains libc-owned; no close), demonstrating the checked file wrapper without changing I/O. Both `fopen`/`fclose` on named files remain unchanged because the original explicitly closes them. |

### Not changed (intentionally)

- All function names, parameter types and returns: `get_color_code`,
  `pretty_printf`, `xdump`, `str_to_color_code`, `set_new_colors`,
  `str_to_long`, `display_help`, `display_version`, `main`.
- All global variables: `no_color_is_on`, `skip_opt`, `length_opt`,
  `columns_opt`, `group_opt`, `color_offset`, `color_bar`, `color_nul`,
  `color_printable`, `color_whitespace`, `color_ascii`, `color_nonascii`
  (names, C types and initial values preserved).
- All CLI strings: help text, version text, `xdump: ` error prefix, ANSI color
  sequences, hex/ASCII panel spacing, newline placement, `getopt` option
  string `"hvs:n:w:g:"`, and the `exit` codes.
- `display_help()` / `display_version()` keep the original `()` spelling
  (not `(void)`) so even the source-level signatures match verbatim.
- `fprintf` in `print_error` was kept: `stdlib/safe/safe_format.h` cannot be
  resolved by the current `c+` rewriter (`[CPLUS-FFI] ... file not found`,
  reproduced in this session), so switching to `fprintf_s` is not available
  here. The literal-format dispatch above supplies the equivalent CWE-134
  protection for the sole dynamic-format call site.

## 4. Byte-identical parts

The fixed corpus covers every CLI path exercised by the original evidence:
version/help text, default file dump, grouped width-8 dump, skip+length dump,
stdin dump, bad flag, and missing file. For all eight cases the modern
binary's stdout, stderr and exit code are byte-for-byte identical to the
original's recorded captures (SHA-256 compared per stream; see the
`verify.sh` output and `cplus/results/`). Representative output (identical in
both binaries):

```
00000000  00 01 02 03 04 05 06 07  08 09 0a 0b 0c 0d 0e 0f  |................|
00000010  10 11 12 13 14 15 16 17  18 19 1a 1b 1c 1d 1e 1f  |................|
00000020  48 65 6c 6c 6f 2c 20 78  64 75 6d 70 21 0a 7f 80  |Hello, xdump!...|
00000030  c3 a9 ff 00                                       |....|
00000034
```

## 5. ABI preservation statement

The C+ conversion adds no new exported symbol and removes none from the
original translation unit. Comparing `nm -g --defined-only` of the original
executable (`cplus/nm_original.txt`) with the modern executable
(`cplus/nm_modern.txt`), all **22** original global symbols are present with
identical names *and* identical Mach-O symbol types:

```
__mh_execute_header(T)  _color_ascii(D)     _color_bar(D)
_color_nonascii(D)      _color_nul(D)       _color_offset(D)
_color_printable(D)     _color_whitespace(D) _columns_opt(D)
_display_help(T)        _display_version(T) _get_color_code(T)
_group_opt(D)           _length_opt(D)      _main(T)
_no_color_is_on(S)      _pretty_printf(T)   _set_new_colors(T)
_skip_opt(S)            _str_to_color_code(T) _str_to_long(T)
_xdump(T)
```

The modern executable additionally links the C+ runtime (symbols prefixed
`_cplus_`, plus `_stdlib_state` / `_stdlib_error_reporting` from the included
C+ headers). Those are implementation support from the C+ toolchain, not
changes to `xdump`'s own API; no original symbol was renamed, retyped,
reordered, or removed, and no calling convention changed. This is a native
Mach-O arm64 executable built from a single translation unit with the C+
compiler; there is no exported-library ABI beyond these globals.

## 6. Before / after outputs (exact)

| Case | argv | stdin | Exit (before/after) | stdout bytes | stderr |
|---|---|---|---|---|---|
| version | `-v` | empty | 0 / 0 | 20 | empty |
| help | `-h` | empty | 0 / 0 | 433 | empty |
| file_default | `build/fixtures/sample.bin` | empty | 0 / 0 | 313 | empty |
| file_width8 | `-w 8 -g 4 build/fixtures/sample.bin` | empty | 0 / 0 | 334 | empty |
| file_skip_n | `-s 32 -n 16 build/fixtures/sample.bin` | empty | 0 / 0 | 88 | empty |
| stdin_default | (none) | `< build/fixtures/sample.bin` | 0 / 0 | 313 | empty |
| bad_flag | `-Z` | empty | 1 / 1 | 0 | `./build/xdump: illegal option -- Z` |
| missing_file | `/definitely/not/a/file` | empty | 0 / 0 | 0 | `xdump: cannot open "/definitely/not/a/file": No such file or directory` |

For the `bad_flag` case the modern binary is launched with
`argv[0] = "./build/xdump"` (via `exec -a` inside `verify.sh`) so the getopt
diagnostic is directly comparable to the original capture; this only
normalizes the program name printed in the message, not any program behavior.
All runs are guarded by `timeout 30`; no case timed out.

## 7. Files created (all under `external_projects/cli_app/cplus/`)

```
cplus/xdump_modern.cp      # modern C+ source (conversion unit)
cplus/xdump_modern         # strict-mode executable
cplus/verify.sh            # reproducible build + 8-case replay + CONVERT line
cplus/CONVERSION.md        # this document
cplus/source_diff.txt      # unified diff original -> modern
cplus/nm_original.txt      # nm -g --defined-only of original executable
cplus/nm_modern.txt        # nm -g --defined-only of modern executable
cplus/results/*            # build logs + 24 captured streams/exits (verify.sh)
```

Refresh command:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
bash external_projects/cli_app/cplus/verify.sh
# expected: CONVERT cli_app 24 24 0   (exit 0)
```
