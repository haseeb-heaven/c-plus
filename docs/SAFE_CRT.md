# C+ Safe CRT (Pillar 1) — API/ABI contract

Status: **partially implemented** (`stdlib/safe_crt/safe_crt.hp`, portable,
header-only, no runtime changes). Everything marked *planned* is a contract for
Agent 1, not code that exists yet.

Goal: familiar C-style calls with an explicit capacity and a recoverable
`error *`, so overflow, undersized buffers, NULL and overlap mistakes are
reported instead of turning into undefined behaviour.

## Namespace

Names are `cplus_*_s` (`cplus_malloc_s`, `cplus_strcpy_s`, …). This is
deliberate: Microsoft's `strcpy_s`/`memcpy_s` are *different* functions with
different signatures and error semantics (`errno_t` return, `rsize_t`), and
`malloc_s`/`free_s` are new C+ names, not established CRT symbols. Keeping a
distinct prefix means a program can include `<string.h>`/`<stdio.h>` and this
header together without a declaration or ABI conflict.

## Implemented (portable, libc-backed)

| function | signature | size inference | ownership | failure |
| --- | --- | --- | --- | --- |
| `cplus_malloc_s` | `void *(size_t size, error *err)` | explicit | caller | `size==0` → `INVALID`, OOM → `MEMORY`, both `NULL` |
| `cplus_calloc_s` | `void *(size_t count, size_t elem, error *err)` | `count*elem` overflow-checked | caller | zero/overflow/OOM → `NULL` + category |
| `cplus_realloc_s` | `void *(void *p, size_t size, error *err)` | explicit | caller keeps `p` on failure | `size==0` rejected; failure leaves `p` valid |
| `cplus_free_s` | `void (void **p)` | n/a | releases and **nulls** `*p` | second call is a no-op |
| `cplus_memcpy_s` | `int (void *dst, size_t dstsz, const void *src, size_t n, error *)` | explicit destination extent | none | overlap → `INVALID`; `n > dstsz` → `BOUNDS` |
| `cplus_memmove_s` | `int (void *dst, size_t dstsz, const void *src, size_t n, error *)` | explicit | none | `n > dstsz` → `BOUNDS`; overlap allowed |
| `cplus_strcpy_s` | `int (char *dst, size_t dstsz, const char *src, error *)` | `strlen(src)+1` | none | too small/overlap → `BOUNDS`, `dst[0]='\0'` |
| `cplus_strcat_s` | `int (char *dst, size_t dstsz, const char *src, error *)` | `strnlen(dst)+strlen(src)+1` | none | unterminated/too small → `INVALID`/`BOUNDS`, `dst[0]='\0'` |
| `cplus_sprintf_s` | `int (char *dst, size_t dstsz, error *, const char *fmt, ...)` | `vsnprintf` result | none | truncation → `BOUNDS`, always terminated |
| `cplus_fopen_s` | `FILE *(const char *path, const char *mode, error *)` | n/a | caller (`fclose`) | `NULL` + `IO` |
| `cplus_fread_s` | `size_t (void *dst, size_t dstsz, size_t elem, size_t count, FILE *, error *)` | `count <= dstsz/elem` | caller | extent → `BOUNDS`; short read reported by the return value |
| `cplus_fwrite_s` | `size_t (const void *src, size_t elem, size_t count, FILE *, error *)` | explicit | none | short write → `IO`, count returned |
| `cplus_parse_int` / `cplus_parse_double` | `int (const char *text, T *out, error *)` | n/a | none | trailing garbage/range/non-finite → `INVALID` |

Platforms: any C17/POSIX libc (macOS, Linux, BSD). `strnlen` and `vsnprintf` are
C99/POSIX; Windows provides both.

## Planned (needs Agent 1 or a later slice)

| item | contract | needs |
| --- | --- | --- |
| `cplus_scanf_s` | bounded `fscanf` equivalents with explicit extents; EOF distinct from error | libc only — next slice |
| `cplus_strncpy_s` / `cplus_strncat_s` | count-limited variants of the above with identical failure rules | libc only — next slice |
| `cplus_asprintf_s` | owned, caller-freed formatted buffer (`free_s` contract) | libc only — next slice |
| AST/CFG recognition of the checked family | the compiler treats `cplus_strcpy_s` & co. as safe, and warns when the raw `strcpy`/`sprintf` forms are used instead | **Agent 1** (compiler hooks) |
| checked contracts for existing `malloc/free/strcpy` | the compiler understands the capacity argument of the checked forms, and the existing raw calls keep their current diagnostics | **Agent 1** |

## Non-claims

- `cplus_malloc_s(size)` does not prevent use-after-free, pointer-provenance
  errors, or data races; it only refuses zero sizes and reports OOM.
- `cplus_strcpy_s(dst, dstsz, src)` prevents *writes past `dstsz`*; it cannot
  know the true extent of a heap object that was smaller than `dstsz` claims,
  nor stop a concurrent writer.
- This module does not replace the runtime's allocator diagnostics: memory
  allocated with `cplus_malloc_s` is released with `cplus_free_s` and is not
  tracked by `cplus_release`.
