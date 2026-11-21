# Existing-libc safety: conformance corpus and platform matrix (#19)

Scope: make **existing C standard-library calls** safer without replacing libc,
without a `malloc_s`/`free_s` family, and without changing libc semantics.
Agent 1 owns the compiler side (#19 / PR #16); this document and the corpus in
`tests/crt/` are the Agent 2 share.

## Corpus

`tests/crt/run_crt_tests.sh` compiles each case **unchanged** twice — once with
`c+ build` (safe mode) and once with `clang -O2` — runs both, and requires equal
exit status and byte-identical stdout. Undefined behaviour is documented in the
case comments, never executed.

| case | covers |
| --- | --- |
| `alloc.c` | `malloc`/`calloc`/`realloc`/`free` valid paths, `free(NULL)`, `calloc` multiplication overflow → `NULL`, failed `realloc` preserves the owner, alias freed once |
| `memory_ops.c` | `memcpy` non-overlapping, zero length, `memmove` overlapping (both directions), `memchr`, `memcmp` |
| `string_ops.c` | `strlen`/`strnlen`, `strcpy`/`strcat` that fit, `snprintf` truncation contract, `strchr`/`strrchr`/`strstr`/`strncmp` |
| `format_ops.c` | `snprintf` sizing/truncation, `fscanf` conversions, invalid conversion → `0`, end of input, stream stays usable |
| `stdio_ops.c` | `fopen`/`fwrite`/`fflush`/`fclose`, short read at EOF, `feof`/`ferror`, `fseek`/`ftell`, missing file → `NULL` |

Result on this host: **5 passed, 0 failed** (C+ safe mode == native `clang -O2`).

## Native facilities actually available (probe output)

`tests/crt/probe_native.c`, macOS arm64, Apple clang 16:

| facility | result |
| --- | --- |
| `_FORTIFY_SOURCE` | 2 (both builds) |
| `__builtin_object_size` | yes |
| `__builtin_dynamic_object_size` | yes |
| `__builtin___memcpy_chk` | yes |
| `__builtin_add_overflow` | yes |
| C11 Annex K (`__STDC_LIB_EXT1__`) | **none** |
| Microsoft UCRT secure CRT | **unavailable** (not `_WIN32`) |
| glibc fortify / `malloc_usable_size` | n/a (not glibc) |

## Function matrix

| family | native facility used | constraints / error handling | protection class | expected corpus result | upstream / licence | benchmark |
| --- | --- | --- | --- | --- | --- | --- |
| `malloc`/`calloc`/`realloc`/`free` | libc + `__builtin_add_overflow` in callers | overflow → `NULL`; `free(NULL)` no-op; failed `realloc` keeps owner; **use-after-free and double free are not detected** for raw pointers | runtime guard (overflow only), otherwise unprotected | passes | Apple Libc (APSL), glibc (LGPL-2.1+), musl (MIT) | malloc/free 200k: 0.034 ms native vs 0.171 ms C+ safe |
| `memcpy`/`memmove` | `__builtin___memcpy_chk`, `-D_FORTIFY_SOURCE=2` | overlap with `memcpy` is UB (documented, not executed); `memmove` overlap defined | compile diagnostic only when the object size is known at the call site | passes | same libcs; Clang/LLVM (Apache-2.0 WITH LLVM-exception) | 64 × 1 MiB copy: 1.40 ms native vs 3.16 ms C+ safe |
| `strcpy`/`strcat`/`strlen` | fortify when the destination size is known; `strnlen` for bounded reads | unbounded copy overflow is UB and **unprotected for unknown-size pointers**; `snprintf` is the defined bounded path | compile diagnostic (known extent) / none | passes | same libcs | — |
| `snprintf`/`scanf` | libc; `-Werror=format-security` (C+ safe mode) | truncation returns the untruncated length; `snprintf(NULL,0,…)` sizing; `scanf` invalid conversion returns `0`, EOF is distinct | compile diagnostic (format) + runtime return contract | passes | same libcs | snprintf 200k: 15.5 ms native vs 26.8 ms C+ safe |
| `fopen`/`fread`/`fwrite`/`fclose` | libc | short read at EOF is a return value; `ferror` for errors; `fclose` twice is UB (not executed) | unprotected (return-value contract only) | passes | same libcs | — |

**Caveat on the benchmarks:** `c+ --release` keeps the mandated sanitizer set,
so these are cost-of-safety numbers, not codegen comparisons; the *native*
column is plain `clang -O2` on the same file.

## Findings for Agent 1 (compiler)

1. **Static bounds pass mis-sizes expression array bounds.** `int buf[1 << 20]`
   is treated as length **1**: `c+ build --release tests/crt/bench_crt.c`
   reports `out-of-bounds access: index 63 reaches past array 'destination' of
   length 1`.  Repro is the committed benchmark; using a macro/identifier for
   the bound avoids it.  This is a false positive on valid C.
2. **Allocation-failure observation is optimiser-sensitive.** At `-O2` LLVM can
   fold `calloc(SIZE_MAX, k)` and assume the allocation cannot fail, so a NULL
   check may be optimised away.  Corpus cases therefore call through
   `noinline` helpers with `volatile` sizes; worth a note in the compiler's
   allocation diagnostics.

## Remaining gaps (no claim that all C is safe)

- Raw `strcpy`/`strcat`/`sprintf` into an undersized or unknown-size buffer is
  still UB; fortify only helps when the object size is visible.
- No Annex K and no UCRT secure APIs on this platform; a portable `*_s` family
  is explicitly out of scope (issue #19 is about existing calls).
- `safeclib` was not vendored (licence/audit cost); it is an optional adapter
  only if the owner asks for it.
- Data races, provenance errors and use-after-free of raw pointers are outside
  what any of these facilities detect.
