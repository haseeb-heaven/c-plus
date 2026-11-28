# Runnable Rust examples for every checked search/sort API

Measured 2026-11-23 on branch `agent/modern-stdlib`, in
`benchmarks/rust_examples/search/`. This directory is self-contained:
`search.rs`, `harness.c`, `compare.sh`, and the generated `out/` artifacts all
live here. No compiler, runtime, source, or file outside this directory was
created or modified.

## 1. Symbols checked with `nm` before any call

Archives used (both on the C link line):

```
build/libc/libcplus_hardened.a     276128 bytes  2026-11-23 10:50
build/safe/libcplus_safecrt.a      458520 bytes  2026-11-23 10:50
```

Exact evidence command:

```sh
nm -g --defined-only \
  build/libc/libcplus_hardened.a \
  build/safe/libcplus_safecrt.a
```

Relevant defined symbols (Apple `nm` prefixes C symbols with `_`):

```
0000000000000000 T _cplus_sc_upstream_bsearch_s   (libcplus_hardened.a)
00000000000000d4 T _cplus_sc_upstream_memrchr_s   (libcplus_hardened.a)
00000000000003c4 T _cplus_v_memchr_s              (libcplus_safecrt.a)
0000000000000000 T _cplus_v__strchr_s_chk         (libcplus_safecrt.a)
000000000000016c T _cplus_v__strrchr_s_chk        (libcplus_safecrt.a)
0000000000000000 T _cplus_v__strstr_s_chk         (libcplus_safecrt.a)
```

| api | symbol called by `harness.c` | nm result |
|---|---|---|
| `bsearch_s` | `cplus_sc_upstream_bsearch_s` | PRESENT |
| `memrchr_s` | `cplus_sc_upstream_memrchr_s` | PRESENT |
| `memchr_s` | `cplus_v_memchr_s` | PRESENT |
| `strchr_s` | `cplus_v__strchr_s_chk` | PRESENT |
| `strrchr_s` | `cplus_v__strrchr_s_chk` | PRESENT |
| `strstr_s` | `cplus_v__strstr_s_chk` | PRESENT |
| `qsort_s` | none | **BLOCKED** |

Plain `cplus_strchr` / `cplus_strrchr` / `cplus_strstr` are also defined in
`libcplus_hardened.a` but are **not** called: they are unchecked libc-style
functions and cannot express the null/zero/over-limit refusal cases without a
segfault. The checked `_chk` variants above were chosen instead because they
return `ESNULLP` / `ESZEROL` / `ESLEMAX` / `ESNOTFND`, which the safe-Rust
`Option`/guard models can mirror deterministically.

### `qsort_s` is BLOCKED (no symbol)

`nm -g --defined-only` on both archives yields **no** defined `qsort_s`,
`_qsort_s_chk`, `cplus_qsort_s`, `cplus_v_qsort_s`, or
`cplus_sc_upstream_qsort_s`. The only definition is a macro —
`third_party/safeclib/include/safe_lib.h:220`:

```
#define qsort_s(base, nmemb, size, compar, context)                            \
    _qsort_s_chk(base, nmemb, size, compar, context, BOS(base))
```

`cplus_qsort` / `cplus_qsort_r` are different, non-checked functions and are
not a `qsort_s` substitute. Therefore no C case can be compiled for it and it
contributes `+1` to `<blocked>`, never to `<cases>`. `search.rs` still
demonstrates the safe-Rust sort idiom (`slice::sort_by`) as the conceptual
analogue, but emits no `qsort_s` token so the two outputs stay line-aligned.

`compare.sh` prints the nm gate result for all 7 APIs plus the macro line as
explicit BLOCKED evidence, and does not treat BLOCKED as a failure.

## 2. Exact toolchains

- Host: macOS arm64, Darwin 27.2.0.
- C: Apple clang 16.0.0 (`clang-1600.0.26.6`), `/usr/bin/clang`, `-std=c99`.
  (Homebrew clang 23.1.6 is also installed at
  `/opt/homebrew/opt/llvm/bin/clang`; the driver defaults to `/usr/bin/clang`.)
- Rust: `rustc 1.100.0 (0ef1bc386 2026-11-09)`, `-O`, direct `rustc` invocation
  (2021 edition default).
- `nm`: Apple/llvm `nm` with `-g --defined-only`.
- `timeout`: BSD/macOS `/usr/bin/timeout`, invoked as `timeout 30 <cmd>`. No
  build or run reached the limit.

## 3. Exact commands

One-shot driver from the repository root:

```sh
benchmarks/rust_examples/search/compare.sh
```

What it executes internally (all builds and runs wrapped in `timeout 30`):

```sh
nm -g --defined-only \
  build/libc/libcplus_hardened.a \
  build/safe/libcplus_safecrt.a                       # symbol gate

/usr/bin/clang -std=c99 \
  -Ilibc/install-include \
  -Ithird_party/safeclib/include \
  benchmarks/rust_examples/search/harness.c \
  build/libc/libcplus_hardened.a \
  build/safe/libcplus_safecrt.a \
  -o benchmarks/rust_examples/search/out/harness_c

rustc -O benchmarks/rust_examples/search/search.rs \
  -o benchmarks/rust_examples/search/out/harness_rs

benchmarks/rust_examples/search/out/harness_c > out/c.out
benchmarks/rust_examples/search/out/harness_rs > out/rs.out
diff -u out/c.out out/rs.out
```

Header include paths resolve `cplus_safecrt_upstream.h`, `cplus_vendor_mem.h`,
`cplus_vendor_strchr.h`, and `cplus_vendor_strstr.h` from
`libc/install-include`, and the pinned safeclib types from
`third_party/safeclib/include`.

Both constraint handlers are replaced with no-ops at the top of `main()` so
the vendored `_s` functions' default stdout diagnostics cannot contaminate the
`<api> <case> <token>` stream:

```c
cplus_sc_set_mem_constraint_handler_s(noop_handler);
cplus_sc_set_str_constraint_handler_s(noop_handler);
```

## 4. Safe-Rust idioms used in `search.rs`

| C API (checked) | Rust example idiom |
|---|---|
| `bsearch_s` | `slice::binary_search_by` (plus `slice::sort_by` to prepare the sorted input; this is the `qsort_s` analogue) |
| `memchr_s` | `iter().position` + `slice::contains` |
| `memrchr_s` | `iter().rposition` |
| `strchr_s` | `iter().position` + `slice::contains` |
| `strrchr_s` | `iter().rposition` |
| `strstr_s` | `str::contains` + `str::find` (byte offsets match C pointer differences) |
| `qsort_s` | `slice::sort_by` only — no token emitted (BLOCKED on the C side) |

There is no `unsafe` block, no raw pointer, and no `extern`/FFI in
`search.rs`.

## 5. Case matrix (55 cases across 6 exercised APIs)

| api | cases |
|---|---|
| `bsearch_s` | found, not-found, null-key, null-base, null-compar, zero-nmemb, over-nmemb, over-size, context-ok, context-lost, canary (11) |
| `memrchr_s` | found, not-found, last-occurrence, null-resultp, null-dest, zero-dmax, over-dmax, ch-over, canary (9) |
| `memchr_s` | found, not-found, first-occurrence, null-resultp, null-dest, zero-dmax, over-dmax, ch-over, canary (9) |
| `strchr_s` | found, not-found, null-dest, null-resultp, zero-dmax, over-dmax, ch-over, canary (8) |
| `strrchr_s` | found, not-found, last-occurrence, null-dest, null-resultp, zero-dmax, over-dmax, ch-over, canary (9) |
| `strstr_s` | found, not-found, null-dest, null-src, null-resultp, zero-dmax, over-dmax, zero-slen, canary (9) |

Limits used: `RSIZE_MAX_MEM = 16384` for mem/bsearch APIs, `RSIZE_MAX_STR =
4096` for the vendored str APIs. "One-over count" cases use `LIMIT + 1` and
are refused before any element access (verified with a counting comparator
for `bsearch_s`). "Canary" cases place guard ints/bytes (`0x5A5AA5A5` /
`'~'`) on both sides of the searched region and assert they are unchanged.

`bsearch_s zero-nmemb` is deliberately token `EMPTY` (clean empty search,
`errno == 0`), not `REFUSED`: safeclib has no constraint for `nmemb == 0`.

## 6. Result

Clean run (exit status 0), final summary line:

```
RUST_EXAMPLES search 7 55 55 0 1
```

Meaning: 7 APIs gated (6 present + 1 blocked), 55 cases, 55 matched, 0
mismatched, 1 blocked (`qsort_s`). Both harnesses emitted 55 lines;
`diff -u out/c.out out/rs.out` was empty; no `BAD_*` token appeared in either
output.

Mismatch path was also verified: temporarily changing one Rust token produced

```
RUST_EXAMPLES search 7 55 54 1 1
```

with exit status 1, confirming the driver fails on a *real* token mismatch.
The file was restored and the clean run above repeated afterwards.

## 7. Non-equivalence statement

The Rust examples are **safe, illustrative analogues**, not a proof that the
C+ checked APIs and Rust provide identical guarantees:

- A null pointer is unrepresentable in safe Rust; the `null-*` cases are
  modelled with `Option::None` and only *correspond* to the C `ESNULLP`
  refusal.
- Rust slices carry their lengths implicitly, so the `dmax`/`nmemb`
  object-size relationship cannot be expressed identically; the over-long
  count guard is a Rust-side model of a C-side constraint.
- The C functions write results through `void **` / `char **` and report
  `errno_t` codes; the Rust helpers return enums and write `Option<usize>`.
  Tokens compare *observable outcomes* (found/not-found/refused/index/canary
  intact), not raw pointers, error numbers, or memory layout.
- `sort_by` has no exercised C counterpart here because `qsort_s` has no
  linkable symbol; it is included solely as the safe-Rust sort example.

A match therefore means the two implementations produced the same outcome
token for the same case — **not** that their binary interfaces, error models,
or safety guarantees are interchangeable.
