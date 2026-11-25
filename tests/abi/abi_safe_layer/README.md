# `tests/abi/abi_safe_layer` — checked ordinary-C layer: source/ABI compatibility

**Claim under test:** enabling the checked ordinary-C layer
(`-include stdlib/safe/safe_c.h` + `libcplus_safecrt.a` + `libcplus_hardened.a`)
does **not** change the source-level ABI of original C code. The same C file
still compiles, links, and produces byte-identical output for bounded valid
operations; pointer/struct layouts and exported C symbol names are unchanged.

**This is compatibility evidence only.** Byte-identical output on valid inputs
does **not** establish that the layer is "safe", that it rejects invalid input,
or that it proves pointer provenance/lifetime/alias properties. See §6.

## Files (all created/owned here)

| file | role |
| --- | --- |
| `original_c.c` | the single, unmodified ordinary ISO C source; no `_s`/`cplus_*` names, no `#ifdef`, no config macros |
| `rewrite_probe.c` | unused-at-runtime TU whose preprocessed text exposes the exact macro rewrites |
| `empty.c` | empty carrier TU for the `-dM -E` macro-table dump |
| `expected_symbols.txt` | exported unmangled C symbol names audited with `nm` |
| `run.sh` | runner: restricted `PATH=/usr/bin:/bin`, absolute tools, `/usr/bin/timeout` on every run, no `\|\| true`, records every command + exit code |
| `out/` | **all** generated artifacts (never touches `build/`) |
| `out/results.log` | full command / exit-code / stdout / check transcript |

## Run

```sh
sh tests/abi/abi_safe_layer/run.sh
```

Result: **`abi_safe_layer: 155 passed, 0 failed`**. All **20** logged commands
exited `0` (no nonzero `RC[...]` in the log). `tests/abi/abi_safe_layer/out/`
holds the evidence. The runner reads the two prebuilt archives from `build/` but
never rebuilds or edits them; their hashes are recorded in the log.

## 1. Toolchain / provenance (from `out/results.log`)

| item | value |
| --- | --- |
| date (UTC) | `2026-11-23T04:38:12Z` |
| compiler | `/usr/bin/clang` — Apple clang 16.0.0 (clang-1600.0.26.6) |
| target | `arm64-apple-darwin27.2.0` |
| arch / os | `arm64` / `Darwin` |
| `libcplus_safecrt.a` | sha256 `10195ff2e11b119b190743fc79bcb5d12b01a3190372b0a0ff3c3ce738bfef71` |
| `libcplus_hardened.a` | sha256 `70b425cef68b3d9d73ef5541dc44b5264a2aabe936cce69cb98ad0e0c4b95964` |

## 2. Exact compiler commands and exit codes

`$R` = `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`. cwd is `$R` for
every command. All three programs use the identical source `original_c.c`.

```sh
# (1) plain: system clang, system headers, system libc
/usr/bin/clang -O0 -fno-builtin -std=c11 -Wall -Wextra \
  tests/abi/abi_safe_layer/original_c.c \
  -o tests/abi/abi_safe_layer/out/plain                                   # rc=0

# (2) safe: force-included checked layer + both real static archives
/usr/bin/clang -O0 -fno-builtin -std=c11 -Wall -Wextra \
  -include stdlib/safe/safe_c.h \
  -I$R -I$R/stdlib/safe -I$R/libc -I$R/libc/install-include \
  -I$R/third_party/safeclib/include -I$R/third_party/intel-safestringlib/include \
  tests/abi/abi_safe_layer/original_c.c \
  $R/build/safe/libcplus_safecrt.a $R/build/libc/libcplus_hardened.a \
  -lm -pthread -o tests/abi/abi_safe_layer/out/safe                       # rc=0

# (3) declarations-only control: same header, rewriting switched off
/usr/bin/clang -O0 -fno-builtin -std=c11 -Wall -Wextra \
  -DCPLUS_SAFE_C_DISABLE -include stdlib/safe/safe_c.h \
  -I$R -I$R/stdlib/safe -I$R/libc -I$R/libc/install-include \
  -I$R/third_party/safeclib/include -I$R/third_party/intel-safestringlib/include \
  tests/abi/abi_safe_layer/original_c.c \
  -o tests/abi/abi_safe_layer/out/disable                                 # rc=0
```

Each binary was run under a 30 s timeout with the same argument
(`tests/abi/abi_safe_layer/out/io_roundtrip.bin`); all three runs exited `0`.

## 3. stdout: byte-identical, exact diff

```
/usr/bin/diff -u out/plain.stdout out/safe.stdout    > out/diff_plain_vs_safe.txt     # rc=0, 0 bytes
/usr/bin/diff -u out/plain.stdout out/disable.stdout > out/diff_plain_vs_disable.txt  # rc=0, 0 bytes
```

All three stdout files have the **same SHA-256**:

```
e4bdfb11408c5cd74d03617ba85d2d3a86cccca11eaa9e2789444ab69fa29bc7  out/plain.stdout
e4bdfb11408c5cd74d03617ba85d2d3a86cccca11eaa9e2789444ab69fa29bc7  out/safe.stdout
e4bdfb11408c5cd74d03617ba85d2d3a86cccca11eaa9e2789444ab69fa29bc7  out/disable.stdout
```

Covered operations (all bounded, valid) and their lines in `plain.stdout`:

- allocation: `MALLOC`, `CALLOC` (zeroed), `REALLOC_GROW`, `REALLOC_SHRINK`, then `free`
- memory: `MEMCPY`, `MEMMOVE`, `STACK_MEMSET`, `MEMCMP_EQUAL 0`, `STACK_MEMCMP_*`
  (memcmp result normalised to its sign — the standard guarantees only the sign,
  not the magnitude, so comparing magnitudes would be an invalid test)
- strings: `STRCPY`, `STRCAT`, `STRNCPY`, `STRNCAT`, `STRLEN_STRLEN`, `STRNLEN_BOUNDED`
- streams: `FWRITE 7`, `FCLOSE_W 0`, `FREAD 7`, `FCLOSE_R 0`, `FILE_PAYLOAD ...`
  (`fopen`/`fclose` are intentionally unremapped by `safe_c.h`)

## 4. Layout / pointer-size assertions

Asserted in **all three** stdout streams: `PTR_SIZE 8`, `LAYOUT void_ptr 8 8`,
`LAYOUT abi_sample ...`, `OFFSET abi_sample.p 16`. The complete captured table
(identical in all three): `void*` 8/8, `size_t` 8/8, `FILE*` 8/8, `div_t` 8/4,
`time_t` 8/8, `struct tm` 56/8, user struct `abi_sample` 32/8 with offsets
`c 0, i 4, d 8, p 16, s 24, tail 26`. Force-including the header does not alter
any of these.

## 5. `CPLUS_SAFE_C_DISABLE`: declarations-only, zero macro rewriting

Two independent preprocessor checks (both `rc=0`):

- **`clang -dM -E` macro table** (`out/macros_enabled.txt` vs
  `out/macros_disabled.txt`): `#define malloc(sz)`, `calloc(n,sz)`,
  `memcpy(d,s,n)`, `strcpy(d,s)`, `fread(p,sz,n,f)`, `printf(...)` and
  `#define CPLUS_SAFE_C_ENABLE 1` are all **present** without the flag and all
  **absent** with `-DCPLUS_SAFE_C_DISABLE`. (The dump normalises whitespace in
  parameter lists.)
- **`clang -E -P` token rewriting** (`out/pp_rewrite_enabled.i` vs
  `out/pp_rewrite_disabled.i`): enabled contains `malloc_s(8)` /
  `cplus_memcpy_s_auto((q),...)` and no bare `malloc(8)`; disabled contains
  bare `malloc(8)` and no rewritten call. The header still *declares* the
  wrappers in declarations-only mode (by design) — the check matches the
  rewritten **call expression**, not the bare declaration.

The `disable` executable also links against system libc alone (no archives),
contains none of the wrapper symbols, and produces byte-identical stdout to
`plain` — confirming declarations-only mode is a true no-rewrite control.

## 6. Exported symbol names (`nm`)

`nm -gU` on both real archives: every name in `expected_symbols.txt` is present
as a defined text symbol (`T _<name>`), and **no** ` T __Z` (Itanium/C++
mangling) appears in either archive or in any executable. Audited unmangled
entry points include:

- safecrt: `_malloc_s`, `_calloc_s`, `_realloc_s`, `_free_s`,
  `_cplus_memcpy_s_auto`, `_cplus_memmove_s_auto`, `_cplus_memset_s_auto`,
  `_cplus_memcmp_s_auto`, `_cplus_strcpy_s_auto`, `_cplus_strncpy_s_auto`,
  `_cplus_strcat_s_auto`, `_cplus_strncat_s_auto`
- hardened libc: `_cplus_malloc`, `_cplus_calloc`, `_cplus_realloc`,
  `_cplus_free`, `_cplus_fread`, `_cplus_fwrite`, `_cplus_printf`,
  `_cplus_memcpy`, `_cplus_strcpy`

`nm -gU` / `nm -u` on the executables shows the mapping itself: the wrappers
are linked into `safe` as plain unmangled C symbols and are absent from `plain`
and `disable`; plain resolves ordinary libSystem names (`_malloc`, `_calloc`,
`_realloc`, `_free`, `_memcmp`, `_printf`, `_fread`, `_fwrite`, and Apple's
fortified `___memcpy_chk`, `___memmove_chk`, `___memset_chk`, `___strcpy_chk`,
`___strncpy_chk`, `___strcat_chk`, `___strncat_chk`). Names `safe_c.h` does not
remap — `_strlen`, `_strnlen`, `_fopen`, `_fclose` — remain ordinary libSystem
calls in **all three** builds, confirming that enabling the layer changes only
the mapped names.

## 7. Scope / non-claims

- Equality here is **source/ABI compatibility evidence**, not a safety proof.
  It shows the checked layer is transparent for valid, bounded calls: nothing
  is renamed, no layout shifts, no mangling appears, and observable results
  match host libc.
- No claim is made that invalid/overflowing/untrusted inputs are rejected —
  that is a separate safety question with its own adversarial/fuzz suites.
- `memcmp` magnitudes are deliberately not compared; only the sign (the
  standard-mandated contract) is.
- Evidence is for the host toolchain above (`arm64-apple-darwin`, Apple clang
  16, statically linked prebuilt archives). It does not establish Windows x86 /
  MSVC / UCRT behaviour.
