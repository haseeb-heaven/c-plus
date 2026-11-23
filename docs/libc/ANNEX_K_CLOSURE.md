# Annex K / stdio API closure audit

Branch: `agent/modern-stdlib` · Audited: 2026-11-23 · Scope: 13 stdio Annex K
entries plus 2 stdlib entries assigned to this agent:

`gets`, `tmpnam`, `fopen_s`, `fclose_s`, `fread_s`, `fprintf_s`, `printf_s`,
`sprintf_s`, `scanf_s`, `fscanf_s`, `sscanf_s`, `gets_s`, `tmpnam_s`,
`bsearch_s`, `qsort_s`.

This audit is deliberately narrow: it classifies what is **already callable**
versus what needs an additive wrapper, and records the exact compile/run
result for a test that exercises only already-callable APIs. No headers, no
build scripts, no vendored sources and no `docs/libc/API_STATUS_MATRIX.md`
rows were modified. No wrappers were fabricated.

## Classification legend

| Code | Meaning |
|---|---|
| **(a)** | Deliberately removed / unsafe with no safe equivalent to expose; documented rationale and the safe alternative that replaced it. |
| **(b)** | Already available as a `cplus_*` export in `stdlib/safe`, with header, source and test citations. |
| **(c)** | Implementable from already-vendored safeclib source via an additive `cplus_*` wrapper; exact interface described below. Requires a new declaration in `stdlib/safe/` and new source entries in `scripts/build_safecrt.sh` — both out of scope for this agent. |
| **(d)** | Blocked by a hard technical/platform dependency. **No entry in this audit is (d)** — see "Why none are blocked". |

## Method and raw evidence

1. Enumerated exports of the checked CRT archive:
   ```
   nm -g --defined-only build/safe/libcplus_safecrt.a
   ```
   The nine relevant `T` symbols present (exact output):
   ```
   0000000000000000 T _cplus_fopen_s
   000000000000033c T _cplus_fclose_s
   0000000000000544 T _cplus_fread_s
   0000000000000614 T _cplus_printf_s
   0000000000000918 T _cplus_fprintf_s
   0000000000000c60 T _cplus_scanf_s
   0000000000001094 T _cplus_fscanf_s
   0000000000001360 T _cplus_sscanf_s
   00000000000016dc T _cplus_sprintf_s
   ```
   The same archive also exports `_cplus_snprintf_s`, `_cplus_vsnprintf_s`,
   `_cplus_vsprintf_s`, `_cplus_fwrite_s` and the `*_checked` format variants.
   It exports **no** `gets_s`, `tmpnam_s`, `bsearch_s` or `qsort_s` symbol; a
   filtered search across every `.a` under `build/` returns none for those
   four names.
2. Cross-checked `stdlib/safe/safe_io.h`, `stdlib/safe/safe_format.h`,
   `stdlib/safe/src/safe_io.c` and `stdlib/safe/src/safe_format.c`.
3. Checked vendored safeclib sources for the four unexported `_s` names:
   `third_party/safeclib/src/io/gets_s.c`, `.../io/tmpnam_s.c`,
   `.../misc/bsearch_s.c`, `.../misc/qsort_s.c` — all present.
   `scripts/build_safecrt.sh` compiles only the mem/str subset of safeclib plus
   `cplus_sc_wrappers.c`; the io/ and misc/ sources listed above are **not** in
   its compile list, which is why the symbols are absent from the archive.
4. Compiled and ran `tests/crt/unit/test_annex_k_closure.c` against the
   existing archive with `/usr/bin/clang` (full result in "Test results").

## Per-API classification

| API | Class | Evidence / disposition |
|---|---|---|
| `gets` | **(a)** | Removed from C11; unbounded stdin read with no caller-supplied capacity, so no safe equivalent of that signature can exist. Replaced by bounded `read_line` (`stdlib/io.hp:52`, `stdlib/file.hp:55`) and host `fgets`. Rationale also recorded in `docs/libc/ISO_C_AUDIT.md:35`. |
| `tmpnam` | **(a)** | Deprecated/removed; TOCTOU race between name generation and file creation. Replaced by `cplus_mkstemp` (`libc/src/stdlib/stdlib_extra6.c:15`, declared `libc/install-include/cplus_libc.h:115`) and `cplus_tmpfile` (`libc/src/stdio/stdio3.c:30`, declared `libc/install-include/cplus_libc.h:171`). Rationale at `docs/libc/API_STATUS_MATRIX.md:831`. |
| `fopen_s` | **(b)** | `cplus_fopen_s` — `stdlib/safe/safe_io.h:10`, `stdlib/safe/src/safe_io.c:124`, test `tests/crt/unit/test_fopen_s.c`. |
| `fclose_s` | **(b)** | `cplus_fclose_s` — `stdlib/safe/safe_io.h:11`, `stdlib/safe/src/safe_io.c:153`, test `tests/crt/unit/test_fclose_s.c`. |
| `fread_s` | **(b)** | `cplus_fread_s` — `stdlib/safe/safe_io.h:14`, `stdlib/safe/src/safe_io.c:196`, test `tests/crt/unit/test_fread_s.c`. |
| `fprintf_s` | **(b)** | `cplus_fprintf_s` — `stdlib/safe/safe_format.h:63`, `stdlib/safe/src/safe_format.c:202`, test `tests/crt/unit/test_fprintf_s.c`. |
| `printf_s` | **(b)** | `cplus_printf_s` — `stdlib/safe/safe_format.h:62`, `stdlib/safe/src/safe_format.c:173`, test `tests/crt/unit/test_printf_s.c`. |
| `sprintf_s` | **(b)** | `cplus_sprintf_s` — `stdlib/safe/safe_format.h:67`, `stdlib/safe/src/safe_format.c:331`, test `tests/crt/unit/test_sprintf_s.c`. Bounded `dmax` adapter (see deviation note). |
| `scanf_s` | **(b)** | `cplus_scanf_s` — `stdlib/safe/safe_format.h:64`, `stdlib/safe/src/safe_format.c:233`, test `tests/crt/unit/test_scanf_s.c` (callable-contract only) plus new closure test (real stdin). |
| `fscanf_s` | **(b)** | `cplus_fscanf_s` — `stdlib/safe/safe_format.h:65`, `stdlib/safe/src/safe_format.c:262`, test `tests/crt/unit/test_fscanf_s.c`. |
| `sscanf_s` | **(b)** | `cplus_sscanf_s` — `stdlib/safe/safe_format.h:66`, `stdlib/safe/src/safe_format.c:293`, exercised at `tests/crt/unit/test_format_security_s.c:169,174` and in the new closure test. |
| `gets_s` | **(c)** | Vendored `third_party/safeclib/src/io/gets_s.c` (export `char *_gets_s_chk(char *dest, rsize_t dmax, const size_t destbos)`, defined at line 107; macro `gets_s(dest, dmax)` at `third_party/safeclib/include/safe_lib.h:123`). No `cplus_*` declaration/export. Policy-discouraged — see note below. |
| `tmpnam_s` | **(c)** | Vendored `third_party/safeclib/src/io/tmpnam_s.c` (export `errno_t _tmpnam_s_chk(const char *dest, rsize_t dmax, const size_t destbos)`, line 97; header macro at `third_party/safeclib/include/safe_lib.h:118` is gated behind `SAFECLIB_ENABLE_UNSAFE`). No `cplus_*` declaration/export. Policy-discouraged — see note below. |
| `bsearch_s` | **(c)** | Vendored `third_party/safeclib/src/misc/bsearch_s.c` (export `void *_bsearch_s_chk(const void *key, const void *base, rsize_t nmemb, rsize_t size, int (*compar)(const void *k, const void *y, void *context), void *context, const size_t basebos)`, line 115; header macro at `third_party/safeclib/include/safe_lib.h:209`). No `cplus_*` declaration/export. |
| `qsort_s` | **(c)** | Vendored `third_party/safeclib/src/misc/qsort_s.c` (export `errno_t _qsort_s_chk(void *base, rsize_t nmemb, rsize_t size, int (*compar)(const void *k, const void *y, void *context), void *context, const size_t basebos)`, line 352; header macro at `third_party/safeclib/include/safe_lib.h:220`). No `cplus_*` declaration/export. |

Totals: **2 × (a)**, **9 × (b)**, **4 × (c)**, **0 × (d)**.
This closes 9 of the 15 audited entries as already available; the remaining 6
are accounted for without any fabricated implementation.

## (b) details — what is actually available

All nine are declared in the two public checked-C headers and exported from
`build/safe/libcplus_safecrt.a`. `safe_io.h:18-23` and `safe_format.h:79-103`
also define unprefixed aliases (`fopen_s`, `printf_s`, `sprintf_s`, ...) that
route through the `*_checked` variants; those add literal-format enforcement
and `__builtin_object_size` destination checks on top of the direct `cplus_*`
calls. The test calls the `cplus_*` names directly so it depends only on
linkable symbols, not on macro expansion.

**Strict-Annex-K deviations (documented, not hidden):**

- `cplus_fopen_s` returns `FILE *` and takes `(name, mode)`; Annex K returns
  `errno_t` and writes through a `FILE **restrict` out-parameter.
- `cplus_fclose_s` returns `int`/`EOF`, not `errno_t`.
- `cplus_fread_s` returns `size_t` (bytes transferred) instead of `errno_t`,
  and takes an extra leading `bmax` capacity argument.
- The format/scan adapters return `int` (C99-style count or negative error),
  not `errno_t`. `cplus_sprintf_s` is explicitly a bounded `vsnprintf`-style
  adapter — the source comment at `stdlib/safe/src/safe_format.c:326` says
  "Alias of the bounded vsnprintf behavior ... NOT Annex K". It is safe
  because `dmax` bounds the write, but it is not ABI-compatible with the
  strict Annex K `sprintf_s` rejection semantics.
- The scanf adapters enforce field widths on `%s`/`%[` and reject `%n`
  (`stdlib/safe/src/safe_format.c:76-106,124`) instead of requiring the Annex K
  `_s` size arguments.

These are genuine safe equivalents and are already shipped/tested, but callers
porting strict Annex K code must translate signatures.

## (c) details — exact additive interfaces

Each item below is implementable without new third-party code. The proposed
`cplus_*` signatures mirror the vendored `_..._chk` entry points but keep the
public `cplus_` prefix used throughout `stdlib/safe`. None of these wrappers
exists today; they are described, not added.

1. `char *cplus_gets_s(char *dest, size_t dmax);`
   - Body: call `_gets_s_chk(dest, (rsize_t)dmax, BOS_UNKNOWN)` from
     `third_party/safeclib/src/io/gets_s.c`.
   - Add `third_party/safeclib/src/io/gets_s.c` to the compile list in
     `scripts/build_safecrt.sh` with the same `-D` prefixing used for the
     other upstream sources (no `gets_s` rename is currently defined in
     `rename_symbols.h`, so the export remains `_gets_s_chk`).
   - Policy note: `docs/libc/ISO_C_AUDIT.md:35` states `gets_s` is omitted in
     favour of bounded `read_line`/`fgets`. Shipping this wrapper is a product
     decision, not a technical requirement.

2. `errno_t cplus_tmpnam_s(char *dest, size_t dmax);`
   - Body: call `_tmpnam_s_chk(dest, (rsize_t)dmax, BOS_UNKNOWN)` from
     `third_party/safeclib/src/io/tmpnam_s.c`.
   - Extra build condition: the public header macro is inside
     `#ifdef SAFECLIB_ENABLE_UNSAFE` (`safe_lib.h:115`), and the source is
     guarded by `!(defined(TEST_MSVCRT) && defined(HAVE_TMPNAM_S))`. The
     generated config in `scripts/build_safecrt.sh` does not currently define
     `SAFECLIB_ENABLE_UNSAFE`; a wrapper can call `_tmpnam_s_chk` directly and
     avoid the gated macro, but the TOCTOU objection still applies — prefer
     `cplus_tmpfile`/`cplus_mkstemp`.

3. `void *cplus_bsearch_s(const void *key, const void *base, size_t nmemb,
                          size_t size,
                          int (*compar)(const void *, const void *, void *),
                          void *context);`
   - Body: call `_bsearch_s_chk(key, base, (rsize_t)nmemb, (rsize_t)size,
     compar, context, BOS_UNKNOWN)` from
     `third_party/safeclib/src/misc/bsearch_s.c`. Portable (no host Annex K
     dependency); the macOS "lacks sort hooks" rationale in
     `docs/libc/API_STATUS_MATRIX.md:121` therefore applies to the host CRT,
     not to this vendored implementation.

4. `errno_t cplus_qsort_s(void *base, size_t nmemb, size_t size,
                          int (*compar)(const void *, const void *, void *),
                          void *context);`
   - Body: call `_qsort_s_chk(base, (rsize_t)nmemb, (rsize_t)size, compar,
     context, BOS_UNKNOWN)` from `third_party/safeclib/src/misc/qsort_s.c`.
     Its core is a self-contained smoothsort migrated from musl, so it does
     not depend on the platform's `qsort_s`.
   - Alternative already in the tree: `cplus_qsort_r`
     (`libc/src/stdlib/stdlib_extra6.c:27`, declared
     `libc/install-include/cplus_libc.h:115`) provides a context-aware sort on
     macOS/BSD/glibc, but its comparator is BSD-style
     `int (*)(void *context, const void *, const void *)`. A thunk that swaps
     the context argument would be needed to meet the Annex K comparator
     order, so this does not by itself close `qsort_s`.
   - Note: no `cplus_bsearch_r` exists; the plain `cplus_bsearch`
     (`libc/install-include/cplus_libc.h:113`) carries no context.

For all four, the mechanical work is (i) add the upstream `.c` file to
`scripts/build_safecrt.sh`, (ii) declare the `cplus_*` prototype in the
appropriate `stdlib/safe/safe_*.h`, (iii) add a thin wrapper source under
`stdlib/safe/src/`, and (iv) add unit tests. Steps (i)–(iii) are intentionally
not performed by this agent.

## Why none are blocked — (d)

There is no hard blocker among the fifteen. The nine (b) entries already link
and run. The four (c) entries have complete, self-contained vendored source in
this repository — including a portable smoothsort for `qsort_s` — so the only
missing pieces are a header declaration, a wrapper and a build-script source
entry. The two (a) entries are intentionally not exposed; their safe
replacements are already shipped.

## Test results

Source: `tests/crt/unit/test_annex_k_closure.c` (new). It exercises exactly the
nine callable `cplus_*` APIs — `cplus_fopen_s`, `cplus_fclose_s`,
`cplus_fread_s`, `cplus_fprintf_s`, `cplus_printf_s`, `cplus_sprintf_s`,
`cplus_scanf_s`, `cplus_fscanf_s`, `cplus_sscanf_s` — with positive round trips
and NULL/invalid-argument negative checks. `cplus_scanf_s` is fed from a
fixture via `dup`/`dup2` on `STDIN_FILENO`, with the descriptor restored
afterwards. The four (c) names are not referenced.

Command (repository root), using the existing archive with no rebuild:

```
/usr/bin/clang -O2 -Wall -Wextra -pedantic -std=c99 \
  -I. -Istdlib/safe -Ithird_party/safeclib/include \
  tests/crt/unit/test_annex_k_closure.c \
  build/safe/libcplus_safecrt.a -lm -pthread \
  -o /private/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/opencode/test_annex_k_closure
```

Exact result:

```
compile_exit=0
annex-k closure: printf_s ok
annex_k_closure: 40/40 checks passed; 9/9 cplus_* APIs callable
run_exit=0
```

Compiler: Apple clang 16.0.0 (clang-1600.0.26.6), arm64-apple-darwin.
Archive: `build/safe/libcplus_safecrt.a` (mtime 2026-11-23 06:27:52).
The compile produced zero warnings under `-Wall -Wextra -pedantic -std=c99`.

## Files owned/touched by this audit

- `docs/libc/ANNEX_K_CLOSURE.md` — this document (new).
- `tests/crt/unit/test_annex_k_closure.c` — closure test (new).

No other file was created or modified; in particular no header, no build
script, no vendored source and no existing status-matrix row was changed.
