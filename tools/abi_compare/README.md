# ABI fingerprinting tool (`tools/abi_compare/`)

Purpose: capture a deterministic, machine-diffable fingerprint of the
**C-compatible public ABI surface** of this repository and fail on unexpected
drift against a committed baseline.

**Implementation rule: shell + C only.** No Python is used anywhere in the
capture, conversion, or comparison path. The canonical artifact is a text
record stream (`fingerprint.txt`); `fingerprint.json` is derived from it by a
small C program.

## Files

| file | role |
| --- | --- |
| `abi_fp_probe.c` | C source of the probe. Prints `sizeof`, `_Alignof`, `offsetof`, enum values, and runs compile-time function-signature assertions. |
| `fp_to_json.c` | C source converting the canonical text records into a JSON object. |
| `run.sh` | Driver: materialises the C sources, compiles/runs them, captures compiler and symbol facts, writes fingerprints, implements `compare`. |
| `.gitignore` | keeps generated `.out/` artifacts out of git. |

Committed artifacts live in `tests/abi/fingerprints/`:

| file | role |
| --- | --- |
| `baseline.txt` | authoritative canonical baseline from an actual `run.sh baseline` capture. |
| `baseline.json` | JSON rendering of the same baseline. |
| `ignore.example` | example allow-list for expected, non-ABI drift (e.g. toolchain version defines). |
| `RESULTS.md` | exact commands and results of capture / validation / comparison tests. |

## What is fingerprinted

The fixed type set is the public C-compatible subset declared in
`libc/install-include/*.h` and `stdlib/safe/*.h`. It mirrors the authoritative
layout table in `tests/abi/abi_layouts.c`:

- base data model: `void *`, `char *`, `int *`, function pointer, `size_t`,
  `uintptr_t`, `int`, `long`, `unsigned long`, `long long`, `double`,
  `long double`, `max_align_t` (size and alignment)
- `cplus_alloc_meta` (size, alignment, field offsets)
- `cplus_thrd_t`, `cplus_mtx_t`, `cplus_cnd_t`, `cplus_once_flag`,
  `cplus_tss_t`, `cplus_thrd_start_t`, and the anonymous thread enum values
- `char16_t`, `char32_t`, `mbstate_t`
- `cplus_safe_alloc_meta`, `cplus_safe_stat`, `cplus_safe_platform_info`
  (size, alignment, field offsets)
- `cplus_safe_log_level`, `cplus_safe_constraint_mode` (size, alignment,
  enumerator values)
- `cplus_runtime_handler`, `errno_t`, `rsize_t` (size, alignment)

Additionally:

- **compiler ABI facts** (`C` records): target triple, `clang --version`, and
  an ABI-relevant subset of `clang -dM -E -x c /dev/null` defines (pointer /
  long / int sizes, byte order, architecture, LP64, C standard version,
  Mach-O/ELF/Windows platform macros).
- **exported symbols** (`S` records): `nm -gU` on the built archives
  `build/libc/libcplus_hardened.a` and `build/safe/libcplus_safecrt.a` by
  default. Records include a per-archive defined-symbol count and the full
  sorted, de-duplicated list of exported symbol names (Mach-O `_` prefix
  preserved). Override the archive set with `CPLUS_FP_ARCHIVES`.
- **function signatures** (`F` records): twelve public functions are checked
  at **compile time** with `_Static_assert(__builtin_types_compatible_p(...))`
  against their expected ABI signatures. A signature change is a hard compile
  error, so the tool fails loudly instead of recording a stale fingerprint.

## Record format

One record per line: `SECTION<TAB>key<TAB>value`. Lines beginning with `#` are
comments and are ignored by the JSON converter and by the comparator.

| section | meaning |
| --- | --- |
| `V` | tool/format metadata (`format`, `generated_by`, `clang_version`, `target_triple`) |
| `L` | layout facts (`size T`, `align T`, `offset T.f`, `value ENUM`) |
| `C` | compiler ABI defines (`define NAME VALUE`) |
| `S` | symbols (`archive PATH count=N`, `symbol PATH::NAME 1`) |
| `F` | function signatures (`signature NAME EXPECTED-SIGNATURE`) |

## Usage

```sh
# Capture into the default working dir (tools/abi_compare/.out/).
sh tools/abi_compare/run.sh capture

# Capture into a specific directory.
sh tools/abi_compare/run.sh capture <outdir>

# Refresh the committed baseline from an actual run.
sh tools/abi_compare/run.sh baseline

# Compare a fresh capture against the committed baseline (exit 0 on match).
sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt

# Same, but ignore allow-listed records (real TAB separators in the file).
sh tools/abi_compare/run.sh compare tests/abi/fingerprints/baseline.txt \
    tests/abi/fingerprints/ignore.example
```

`compare` regenerates a fresh fingerprint, filters the baseline and the fresh
capture through the optional ignore file, and runs `diff -u`. Any non-empty
diff prints the changes (baseline -> current) and exits **1**. The full diff is
kept at `tools/abi_compare/.out/compare/diff.txt`.

Ignore-file lines are either a bare section letter (ignore the whole section)
or `SECTION<TAB>key-prefix` (ignore records whose key starts with that prefix).
`#` lines and blank lines are comments.

Environment overrides: `CPLUS_FP_CLANG`, `CPLUS_FP_NM`, `CPLUS_FP_DIFF`,
`CPLUS_FP_OUT`, `CPLUS_FP_ARCHIVES`.

## Scope / limitations

The baseline is specific to the host toolchain
(`arm64-apple-darwin`, Apple clang 16, LP64) and to the two default archives.
It does not by itself establish calling-convention correctness, Windows x86 /
MSVC / UCRT layout, or bitfield packing across compilers — those are covered
by other ABI evidence in `tests/abi/`. It detects *change*, not correctness;
review any diff before accepting it as an intentional ABI update. Toolchain
version drift can be allow-listed with an ignore file (see `ignore.example`).
