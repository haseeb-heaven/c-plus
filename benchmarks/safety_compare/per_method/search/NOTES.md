# Per-method memory-safety comparison: C+ checked search APIs vs safe Rust

Measured 2026-11-23 in
`benchmarks/safety_compare/per_method/search/` (branch `agent/modern-stdlib`).
This directory is self-contained: all sources, the driver, and its generated
`out/` artifacts live here. No compiler, runtime, or files outside this
directory were touched.

## 1. Methods actually available (nm evidence)

`nm -g --defined-only` over every `.a` in the checkout
(`find . -type f -name '*.a'`) produced only these relevant definitions:

```
./build/libc/libcplus_hardened.a:
0000000000000000 T _cplus_sc_upstream_bsearch_s
00000000000000d4 T _cplus_sc_upstream_memrchr_s
./build/libc-install/lib/libcplus_hardened_libc.a:
0000000000000000 T _cplus_sc_upstream_bsearch_s
0000000000000210 T _cplus_sc_upstream_memrchr_s
./build/libc-cmake/libcplus_hardened_libc.a:
0000000000000000 T _cplus_sc_upstream_bsearch_s
0000000000000210 T _cplus_sc_upstream_memrchr_s
./build/safe/libcplus_safecrt.a:
00000000000000fc T __memchr_s_uchk
0000000000000000 T _cplus_v__memchr_s_chk
00000000000003c4 T _cplus_v_memchr_s
```

| method | linkable symbol | status |
|---|---|---|
| `bsearch_s` | `cplus_sc_upstream_bsearch_s` | available, exercised |
| `memrchr_s` | `cplus_sc_upstream_memrchr_s` | available, exercised |
| `memchr_s` | `cplus_v_memchr_s` | available (newly vendored), exercised |
| `qsort_s` | none | **BLOCKED** |

`qsort_s` is only a macro, never a symbol —
`third_party/safeclib/include/safe_lib.h:220`:

```
#define qsort_s(base, nmemb, size, compar, context)                            \
    _qsort_s_chk(base, nmemb, size, compar, context, BOS(base))
```

There is no `_qsort_s_chk` definition in any archive either (`cplus_qsort` /
`cplus_qsort_r` are different, non-checked functions). Therefore no C99
harness case can be compiled for it and it contributes `+1` to the `blocked`
field, never to `cases`.

**memchr_s provenance / timing note.** At the first nm pass (~10:24) no
`memchr_s` symbol existed in any archive. A parallel rebuild produced
`build/safe/libcplus_safecrt.a` at 10:26:17 containing `cplus_v_memchr_s`
(vendored from `third_party/safeclib/src/extmem/memchr_s.c`); the final
measurement used archives stamped
`build/libc/libcplus_hardened.a 2026-11-23 10:28:24` and
`build/safe/libcplus_safecrt.a 2026-11-23 10:28:26`. The method is therefore
reported PRESENT and exercised, not BLOCKED. If that archive is removed, the
driver fails at link time rather than silently reporting a pass.

## 2. Exact toolchains

- Host: macOS arm64, Darwin 27.2.0.
- C: Apple clang 16.0.0 (`clang-1600.0.26.6`, `/usr/bin/clang`),
  `-std=c99`.
- Rust: `rustc 1.100.0 (0ef1bc386 2026-11-09)`, `-O`, edition implied by
  direct `rustc` invocation (2021 default for this toolchain).
- `timeout`: BSD/macOS `timeout` at `/usr/bin/timeout`, invoked as
  `timeout 30 <cmd>`. No run reached the limit.
- `nm`: Apple/llvm `nm` (`-g --defined-only`).

## 3. Exact commands

From the repository root:

```sh
# one-shot driver (builds both harnesses, runs both, diffs tokens)
benchmarks/safety_compare/per_method/search/compare.sh
```

What the driver executes internally (all with `timeout 30`):

```sh
nm -g --defined-only \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a   # symbol gate

/usr/bin/clang -std=c99 \
  -Ilibc/install-include \
  -Ithird_party/safeclib/include \
  benchmarks/safety_compare/per_method/search/harness.c \
  build/libc/libcplus_hardened.a build/safe/libcplus_safecrt.a \
  -o benchmarks/safety_compare/per_method/search/out/harness_c

rustc -O benchmarks/safety_compare/per_method/search/search.rs \
  -o benchmarks/safety_compare/per_method/search/out/harness_rs

timeout 30 .../out/harness_c > .../out/c.out
timeout 30 .../out/harness_rs > .../out/rs.out
```

The vendored `memchr_s` routes constraint reports through safeclib's global
handlers, whose default prints to **stdout**. `harness.c` therefore installs
no-op mem/str constraint handlers via
`cplus_sc_set_mem_constraint_handler_s` / `cplus_sc_set_str_constraint_handler_s`
before any case, so stdout contains only the deterministic token lines. The
error codes themselves (400/401/403/409) are still asserted by the harness.

## 4. Exact output

Driver stdout:

```
SYMBOL bsearch_s PRESENT (/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/safe/libcplus_safecrt.a)
SYMBOL memrchr_s PRESENT (/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/safe/libcplus_safecrt.a)
SYMBOL memchr_s PRESENT (/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/libc/libcplus_hardened.a /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/safe/libcplus_safecrt.a)
SYMBOL qsort_s ABSENT (BLOCKED, no linkable symbol)
METHOD_SEARCH 29 29 0 1
```

Driver exit status: `0`.

Both `out/c.out` and `out/rs.out` are byte-identical (verified with
`diff -u`, no differences). Their 29 lines, in order:

```
bsearch_s found FOUND
bsearch_s not-found NOTFOUND
bsearch_s null-key REFUSED
bsearch_s null-base REFUSED
bsearch_s null-compar REFUSED
bsearch_s zero-nmemb EMPTY
bsearch_s over-nmemb REFUSED
bsearch_s over-size REFUSED
bsearch_s context-ok CTX_OK
bsearch_s context-lost CTX_LOST
bsearch_s canary INTACT
memrchr_s found FOUND
memrchr_s not-found NOTFOUND
memrchr_s last-occurrence IDX3
memrchr_s null-resultp REFUSED
memrchr_s null-dest REFUSED
memrchr_s zero-dmax REFUSED
memrchr_s over-dmax REFUSED
memrchr_s ch-over REFUSED
memrchr_s canary INTACT
memchr_s found FOUND
memchr_s not-found NOTFOUND
memchr_s first-occurrence IDX0
memchr_s null-resultp REFUSED
memchr_s null-dest REFUSED
memchr_s zero-dmax REFUSED
memchr_s over-dmax REFUSED
memchr_s ch-over REFUSED
memchr_s canary INTACT
```

### Summary-field convention

`METHOD_SEARCH <cases> <matched> <mismatched> <blocked>`:

- `cases` (29) = number of compared token lines = max(C lines, Rust lines).
- `matched` (29) = lines identical byte-for-byte at the same ordinal.
- `mismatched` (0) = lines that differ or are missing; any nonzero makes the
  driver exit 1. `BAD_*` tokens emitted by a failing assertion are deliberate
  mismatch signals, not expected output.
- `blocked` (1) = methods with no linkable symbol (`qsort_s`). BLOCKED is not
  a pass and not a failure; the driver still exits 0.

## 5. Explicit non-equivalence note

**Token agreement is not proof of equivalent safety guarantees.** The two
programs were written independently to express the *same situation* with the
contracts each language actually offers; only the observed tokens are
comparable.

1. **Mechanism differs.** The C+ APIs enforce constraints at **run time** and
   report them as `errno` (`bsearch_s`: ESNULLP/ESLEMAX) or as `errno_t`
   return codes plus `*resultp == NULL` (`memrchr_s`, `memchr_s`: ESNULLP,
   ESZEROL, ESLEMAX, ESNOTFND). Rust makes null pointers, absent references,
   and out-of-range slice access **unrepresentable in safe code**; the
   corresponding `REFUSED` tokens come from explicit `Option`/length guards in
   the safe wrapper functions, not from a runtime constraint handler. Both
   produce `REFUSED`; Rust's guarantee is structural, C+'s is operational.
2. **No object-size check in the C APIs.** The vendored/upstream checked
   functions validate nulls, `RSIZE_MAX_MEM == 16384`, and `ch <= 255`, but
   (with the BOS/basebos branches omitted) they do **not** validate that
   `nmemb*size` or `dmax` fits the actual buffer. Passing an over-large count
   against a small buffer would still over-read in C. Rust slices carry their
   length, so the wrappers can and do refuse `count > base.len()` /
   `dmax > dest.len()`. This is a real strength difference, not a token
   difference; the harness only passes valid lengths for successful cases.
3. **`zero-nmemb` is deliberately `EMPTY`, not `REFUSED`.** safeclib's
   `bsearch_s` has no zero-count constraint: it clears `errno` and returns
   NULL for an empty range (`libc/src/string/upstream_safecrt.c`, the
   `while (nmemb > 0)` loop). Rust's `binary_search` on an empty slice
   likewise returns `Err`. By contrast `memrchr_s` / `memchr_s` return ESZEROL
   for `dmax == 0`, so their `zero-dmax` cases are genuine `REFUSED`. The
   generic "zero count/size -> REFUSED" expectation is therefore satisfied by
   the `*_dmax` cases; the `bsearch_s` zero-count case is reported honestly as
   `EMPTY` with source evidence.
4. **Context propagation is compile-time in Rust.** C passes `context` as a
   runtime `void *`, so a NULL/mangled context is possible and the comparator
   detects it. A Rust closure captures its context lexically and cannot lose
   it; `context-lost` is *simulated* by supplying a bogus `Ctx` value. The
   matching `CTX_LOST` token shows the same search outcome, not the same kind
   of failure.
5. **RSIZE_MAX_MEM is an arbitrary policy limit in Rust.** The `over-*`
   cases use a small slice plus an explicit count of 16385; the Rust wrapper
   refuses it by policy. No Rust allocation of that size is attempted and no
   Rust safety rule depends on 16384.
6. **Comparator conventions differ but are self-consistent.** C's
   `compar(key, element)` returns `sign(key - element)`; Rust's
   `binary_search_by` closure returns `probe.cmp(key)`. Both find index 3 for
   key 7 in `[1,3,5,7,9]`, but the signs are opposite by API convention.
7. **`qsort_s` is BLOCKED.** No symbol exists and the safeclib header macro
   cannot be linked without `_qsort_s_chk`; no C/Rust comparison was attempted
   for it. BLOCKED is neither matched nor mismatched.
8. **Search direction.** `memrchr_s` maps to `iter().rposition` (last match,
   `IDX3`), `memchr_s` maps to `iter().position` (first match, `IDX0`). The
   tokens encode index only to make the direction difference visible; they
   are not the same operation.

Nothing here is a live-game claim, a speed result, or a proof that the C+
runtime is memory-safe in general. Only the 29 deterministic outcome tokens
above were compared.
