# Per-method memory safety comparison: C+ checked APIs vs safe Rust

All files in this directory were produced under a strict ownership rule: only
`benchmarks/safety_compare/per_method/memory/` was created/modified. No build
system, compiler, runtime, or other repository files were touched. No results
below are fabricated; every token came from an executed binary (see `out/`).

## 1. Symbol verification (done before writing any harness)

```
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
nm -g build/safe/libcplus_safecrt.a | grep -iE 'cplus_sc_|_s$' \
  | grep -iE 'memcpy|memmove|memset|memcmp|memzero' | sort -u
```

Relevant defined (`T`) symbols observed in `build/safe/libcplus_safecrt.a`:

| Symbol | Address | Notes |
|---|---|---|
| `_cplus_sc_memcpy_s`  | `0x0`  | safeclib-backed checked API (from `sc_cplus_sc_wrappers.o`) |
| `_cplus_sc_memmove_s` | `0xc`  | ditto |
| `_cplus_sc_memset_s`  | `0x18` | ditto |
| `_cplus_sc_memcmp_s`  | `0x20` | ditto |
| `_cplus_sc__memcpy_s_chk` / `_memmove_s_chk` / `_memset_s_chk` / `_memcmp_s_chk` | `0x0` | checked cores |
| `_cplus_memcpy_s` / `_cplus_memmove_s` / `_cplus_memset_s` / `_cplus_memcmp_s` | `0x0`, `0x1a4`, `0x270`, `0x328` | thin C+ wrappers over the `cplus_sc_*` calls |

The harnesses call the `cplus_sc_*` entry points directly so the measured
behaviour is the checked API itself, not an additional wrapper layer.

## 2. Toolchain versions (captured in `out/toolchains.txt`)

```
clang: Apple clang version 16.0.0 (clang-1600.0.26.6)
rustc: rustc 1.100.0 (0ef1bc386 2026-11-09)
date:  2026-11-23T04:59:31Z
```
Host: Darwin 27.2.0, arm64. `timeout` resolved to `/usr/bin/timeout`.

## 3. Exact commands

Driver (does all builds, runs, diff and counting):

```
sh benchmarks/safety_compare/per_method/memory/compare.sh
```

Per-method C build/run commands issued by the driver (one subprocess per
method, `${ROOT}` = repo root):

```
/usr/bin/clang -std=c99 \
  -I${ROOT}/libc/install-include -I${ROOT}/third_party/safeclib/include \
  ${ROOT}/benchmarks/safety_compare/per_method/memory/<method>.c \
  ${ROOT}/build/safe/libcplus_safecrt.a \
  ${ROOT}/build/libc/libcplus_hardened.a -lm -pthread \
  -o ${ROOT}/benchmarks/safety_compare/per_method/memory/bin/c_<method>
timeout 30 .../bin/c_<method> > out/c_<method>.stdout 2> out/c_<method>.stderr
```
`<method>` ∈ `memcpy_s`, `memmove_s`, `memset_s`, `memcmp_s`.

Rust build/run:

```
rustc -O .../memory.rs -o .../bin/rust_memory
timeout 30 .../bin/rust_memory > out/rust_tokens.txt.raw 2> out/rust.stderr
```

Comparison: `sort` both token streams, `diff -u` them, and count matched /
mismatched keys with `awk` on `("<method>", "<case>")`.

## 4. Observed C+ return codes and canary evidence

Exact codes from `out/c_*.stderr` (`DIAG <method> <case> rc=<n> guards=<0|1>`):

| case | memcpy_s | memmove_s | memset_s | memcmp_s | meaning of code |
|---|---|---|---|---|---|
| valid_exact_fit | 0 | 0 | 0 | 0 | `EOK` |
| null_src / null_dest | 400 | 400 | 400 | 400 | `ESNULLP` |
| dest_one_byte_too_small | 406 | 406 | 406 | 406 | `ESNOSPC` |
| size_max_length | 403 | 403 | 403 | 403 | `ESLEMAX` (`SIZE_MAX` > `RSIZE_MAX_MEM` = 16384) |
| overlap | 404 | 0 | N/A | 0 | `ESOVRLP` for memcpy; `EOK` for memmove/memcmp |

`guards=1` for **every** executed case, including all refusals. Canaries are
guard bytes placed immediately after the declared destination capacity
(`dmax`). Note: safeclib deliberately zeroes up to `dmax` bytes of the
destination on several constraint violations (documented C11 Annex K
behaviour); that is inside the declared region and does *not* touch the guard
bytes. The canaries therefore prove that no refusal wrote outside the declared
destination — not that the destination contents were left byte-for-byte
unchanged.

## 5. Rust modelling (safe APIs only, no `unsafe`)

* `copy_from_slice` — exact-fit copy; bounds are checked by the callee.
* `copy_within` — overlap-capable memmove analogue (overlap is OK).
* `fill` — memset analogue.
* slice equality (`a[..] == b[..]`) — memcmp analogue.
* `get` / `get_mut(..n)` — return `None` for one-byte-too-small and
  `..usize::MAX` requests; no panic, no out-of-bounds write.
* C null has no safe-Rust value; it is represented by `Option::None` and the
  safe path refuses it (`REFUSED`).
* memcpy overlap cannot be expressed as two overlapping `&mut [u8]` in safe
  Rust. It is modelled with `RefCell::try_borrow_mut`, which returns `Err`
  when a second mutable view would alias the same byte → `REFUSED`. This is a
  runtime `Result`-style refusal in the harness; the deeper guarantee is the
  compile-time borrow checker, which rejects such aliasing outright.
* memset has one pointer and no source operand, so "overlap" is `N/A` in both
  harnesses. Two overlapping *shared* slices (memcmp) are legal in safe Rust,
  matching `EOK`.

## 6. Results (stdout of the final run; `out/c_tokens.txt` and `out/rust_tokens.txt`)

```
memcpy_s  valid_exact_fit          OK      / OK
memcpy_s  null_src                 REFUSED / REFUSED
memcpy_s  dest_one_byte_too_small  REFUSED / REFUSED
memcpy_s  size_max_length          REFUSED / REFUSED
memcpy_s  overlap                  REFUSED / REFUSED
memmove_s valid_exact_fit          OK      / OK
memmove_s null_src                 REFUSED / REFUSED
memmove_s dest_one_byte_too_small  REFUSED / REFUSED
memmove_s size_max_length          REFUSED / REFUSED
memmove_s overlap                  OK      / OK
memset_s  valid_exact_fit          OK      / OK
memset_s  null_dest                REFUSED / REFUSED
memset_s  dest_one_byte_too_small  REFUSED / REFUSED
memset_s  size_max_length          REFUSED / REFUSED
memset_s  overlap                  N/A     / N/A
memcmp_s  valid_exact_fit          OK      / OK
memcmp_s  null_src                 REFUSED / REFUSED
memcmp_s  dest_one_byte_too_small  REFUSED / REFUSED
memcmp_s  size_max_length          REFUSED / REFUSED
memcmp_s  overlap                  OK      / OK
```

Driver summary and exit status:

```
METHOD_MEMORY 20 20 0
compare_exit=0
```

`sorted diff` (`out/diff.txt`) is empty. Failure-path check: temporarily
forcing one `memcmp_s overlap` token to a wrong value produced
`METHOD_MEMORY 20 19 1`, `compare_exit=1`, and a `MISMATCH` line in
`out/mismatch_detail.txt`; the harness was then restored byte-for-byte and the
final re-run reproduced `METHOD_MEMORY 20 20 0`, `compare_exit=0`.

## 7. Explicit caveat — matching tokens are NOT equivalent guarantees

A matched `OK`/`REFUSED` token means only that the two programs printed the
same word for the same `(method, case)` label. It does **not** mean the two
languages provide the same safety property:

* C+ refuses **at runtime** through a checked API: the `cplus_sc_*` functions
  validate `dmax`/`slen`, null pointers and overlap and return positive
  `errno_t` codes (400/403/404/406). They cannot prove pointer provenance,
  ownership, lifetime or alias freedom — those remain the caller's problem.
* Rust rejects **many of the same situations at compile time** (no null
  references; no two overlapping `&mut` borrows) or returns `None`/`Err` from
  safe runtime APIs (`get`, `get_mut`, `try_borrow_mut`). Its guarantees cover
  provenance/aliasing/lifetime by construction, which the C checked API does
  not attempt.
* The Rust `null_src`/`null_dest` and `overlap` tokens are deliberate models of
  situations that safe Rust cannot express; they are not evidence that a
  Rust program can pass a null pointer or create overlapping mutable slices.

Therefore this comparison demonstrates aligned *observable outcomes* for a
fixed set of cases, not equivalence of the underlying safety guarantees.
