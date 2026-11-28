# safecrt_memstr — checked C memory/string APIs vs safe Rust

This directory contains a runnable, deterministic token comparison between
every checked memory/string `_s` API shipped in `build/safe/libcplus_safecrt.a`
and a safe-Rust equivalent. It follows the same stdout contract as the existing
`benchmarks/safety_compare/per_method/` harnesses
(`<api> <case> <token>`), but covers the whole memory/string `_s` family in a
single pair of programs.

## Deliverables

| File | Purpose |
| --- | --- |
| `examples.rs` | 21 public example functions (one per checked API), **safe Rust only** — no `unsafe`, no FFI, no raw pointers. Uses `copy_from_slice`, `copy_within`-style slicing, `fill`, slice `==`, `String`/`str` methods, `split`, `std::io::Error::to_string`, `Option`/`get`/`get_mut`. |
| `harness.c` | C99 harness that actually calls the corresponding `cplus_sc_*` / `cplus_*_s_auto` functions for the identical cases and prints matching tokens. Diagnostics (return codes, canary state) go to stderr. |
| `compare.sh` | Verifies symbols with `nm`, builds both programs under `timeout 30`, diffs tokens, prints the summary line, exits nonzero on real mismatch. |
| `NOTES.md` | This file. |

Generated evidence lives in `out/` (ignored by the repository `.gitignore`
via `benchmarks/rust_examples/**/out/`).

## Authoritative symbol inventory (nm evidence)

Source of truth for scope:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
nm -g build/safe/libcplus_safecrt.a | grep ' T _cplus'
```

All **21** required API symbols are present as defined text (`T`) symbols.
Raw excerpt (address, type, name):

```
0000000000000000 T _cplus_sc_memcpy_s          00000000000004c8 T _cplus_memcpy_s_auto
000000000000000c T _cplus_sc_memmove_s         000000000000069c T _cplus_strcpy_s_auto
0000000000000018 T _cplus_sc_memset_s          00000000000007b8 T _cplus_memmove_s_auto
0000000000000020 T _cplus_sc_memcmp_s          00000000000007dc T _cplus_strncpy_s_auto
000000000000002c T _cplus_sc_memzero_s         0000000000000920 T _cplus_strcat_s_auto
0000000000000034 T _cplus_sc_strcpy_s          0000000000000934 T _cplus_memset_s_auto
000000000000003c T _cplus_sc_strncpy_s         0000000000000a80 T _cplus_strncat_s_auto
0000000000000048 T _cplus_sc_strcat_s          0000000000000b5c T _cplus_memcmp_s_auto
0000000000000050 T _cplus_sc_strncat_s
000000000000005c T _cplus_sc_strnlen_s
0000000000000064 T _cplus_sc_strcmp_s
0000000000000070 T _cplus_sc_strerror_s
0000000000000078 T _cplus_sc_strtok_s
```

`compare.sh` re-verifies each symbol itself (`nm_has`) and records the result
in `out/symbols.txt`. It also requires the three support symbols
`cplus_sc_set_mem_constraint_handler_s_wrapper`,
`cplus_sc_set_str_constraint_handler_s_wrapper`, and
`cplus_runtime_set_violation_trap`, all present. **BLOCKED list is empty** in
this build: no API had to be skipped and no token was fabricated. If a future
rebuild drops a symbol, `compare.sh` prints `BLOCKED ... (symbol absent)` to
stderr, excludes that API from *both* program argv lists, and reduces `<apis>`
accordingly — it never emits a guessed token for a missing symbol.

The `cplus_*_s` adapter entry points (e.g. `cplus_memcpy_s`) and the internal
`cplus_sc__*_chk` / `cplus_upstream_*` symbols also exist, but the harness
targets the safeclib-backed `cplus_sc_*` implementations for the 13 base APIs
(the same layer the existing per-method harnesses use) and the public
`cplus_*_s_auto` wrappers for the 8 auto variants.

## Exact commands and toolchains

```sh
# From the repo root, with build/safe/libcplus_safecrt.a and
# build/libc/libcplus_hardened.a already built:
sh benchmarks/rust_examples/safecrt_memstr/compare.sh
```

`compare.sh` executes, in essence:

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -Werror \
  -Ilibc/install-include -Ithird_party/safeclib/include \
  benchmarks/rust_examples/safecrt_memstr/harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o out/harness_bin

rustc -O benchmarks/rust_examples/safecrt_memstr/examples.rs -o out/examples_bin

timeout 30 out/harness_bin   <nm-verified API list> > out/c.out    2> out/c.err
timeout 30 out/examples_bin  <nm-verified API list> > out/rust.out 2> out/rust.err
# sort each, diff, and count tokens keyed by "<api>|<case>"
```

Toolchain versions captured in `out/toolchains.txt` on the verification run:

```
clang: Apple clang version 16.0.0 (clang-1600.0.26.6)
rustc: rustc 1.100.0 (0ef1bc386 2026-11-09)
nm:    llvm-nm, compatible with GNU nm
date:  2026-11-23T05:34:43Z
```

Both harnesses accept an optional list of API names as argv. With no argv they
emit all 21 APIs; `compare.sh` always passes the nm-verified list so the two
sides are restricted identically.

## Case matrix and observed C behavior

Each API emits up to six cases: `valid`, `null`, `zero_length`, `one_over`,
`overflow_size`, `canary_intact`. Token meanings:

- `OK` — operation completed, trailing 0xA5 canary region intact
- `REFUSED` — invalid condition detected before an out-of-bounds write, canary intact
- `N/A` — case undefined for that API (only `memzero_s one_over`: it has no separate requested length)
- `CANARY_OK` — explicit case: a refused operation left the guard intact
- `CANARY_BROKEN` — bytes past the declared capacity were modified (never observed; would be a real defect)

The Rust side models null as `None`, too-large lengths via `get`/`get_mut`
returning `None`, and canary integrity by inspecting the trailing slice. Both
sides share the token vocabulary, so the diff is meaningful.

Observed safeclib return codes (from the empirical probes used to build the
model; `0` = OK, non-zero = refused; `401/400/403/406/402` are safeclib codes,
**not** POSIX `errno` values):

| API (base) | valid | null | zero_length | one_over | overflow_size |
| --- | --- | --- | --- | --- | --- |
| memcpy_s / memmove_s / memset_s | 0 | 400 | 0 | 406 | 403 |
| memzero_s | 0 | 400 | 401 | N/A | 403 |
| memcmp_s | 0 | 400 | 401 | 406 | 403 |
| strcpy_s | 0 | 400 | 401 | 406 | 406 |
| strncpy_s | 0 | 400 | 0 | 406 | 403 |
| strcat_s / strncat_s | 0 | 400 | 401 | 406 | 403/403 |
| strnlen_s (returns length) | 3, no violation | 0, violation | 0, violation | 3, no violation | 0, violation |
| strtok_s (returns token or NULL) | token | NULL | NULL | token | NULL |
| strerror_s | 0 | 400 | 401 | 402 (dmax=3) | 403 |
| strcmp_s | 0 | 400 | 401 | 0 (OK) | 403 (dmax=SIZE_MAX) |

Notes on the table:

- `memcmp_s` (base) treats `smax == 0` as `ESZEROL`, while `memcmp_s_auto`
  defines `n == 0` as a successful no-op; likewise the base `strcpy_s`,
  `strcat_s`, `strncat_s` refuse `dmax == 0` while the `_auto` wrappers make an
  empty source / `n == 0` a no-op. These are real API differences and the
  harness models them separately.
- `strerror_s` refuses `dmax < 4` (ESLEMIN) and truncates its message to fit
  `dmax - 1` bytes plus NUL for larger capacities — hence `valid` (dmax=8)
  succeeds and `one_over` (dmax=3) is refused.
- In this shipped build, `strcmp_s` does **not** refuse a source longer than
  `dmax`; it compares and reports an ordering (rc=0). Its `one_over` token is
  therefore `OK`. This is recorded, not hidden.
- `cplus_runtime_set_violation_trap(0)` is set at harness startup so auto-wrapper
  overflow checks (which call `cplus_runtime_violation`) report to stderr and
  return NULL/ERANGE instead of terminating with exit 70. Constraint banners
  are replaced by a counting no-op handler. These settings are harness-only.

## Actual verification run

```
$ sh benchmarks/rust_examples/safecrt_memstr/compare.sh
RUST_EXAMPLES safecrt_memstr 21 126 126 0
$ echo $?
0
```

- 21 APIs (13 base + 8 auto variants), 126 case lines (21 x 6, including the
  single `N/A` for `memzero_s one_over`).
- `out/diff.txt` is **empty** — the sorted token streams are byte-identical.
- `out/mismatch_detail.txt` is **empty** — no missing or mismatched keys.
- Exit code 0. Both builds were warning-free (`-Werror` enabled on the C side).

## MATCHING TOKENS ARE NOT EQUIVALENT GUARANTEES

The token equality above records **observed outcomes on a fixed set of
inputs**. It does **not** mean the C+ SafeCRT layer and safe Rust provide the
same safety guarantees:

- The C+ functions are **runtime checks** over caller-supplied extents
  (`dmax`/`smax`/`n`). They detect null pointers, out-of-range sizes, overlap,
  and missing terminators when the caller passes truthful capacities. They do
  **not** prove pointer provenance, ownership, lifetime, alias freedom, or
  use-after-free safety; the adapter layer documents this explicitly
  (`stdlib/safe/safe_memory.h`, `stdlib/safe/safe_string.h`).
- If a caller lies about `dmax` (declares a capacity larger than the real
  object), the checked C API cannot detect the lie and can corrupt memory; the
  safe-Rust side cannot express that program at all. The canary cases here keep
  the declared capacity truthful on both sides, which is exactly why tokens
  match — the comparison does not exercise caller-dishonesty scenarios.
- Safe Rust gets its guarantees **statically**: a null reference is
  unrepresentable, a slice cannot be indexed out of bounds, and two mutable
  references cannot alias. Refusals are `None`/`Err` values or compile-time
  errors, not runtime constraint-handler callbacks. The `Option`-based models
  in `examples.rs` are *illustrations* of that discipline, not runtime checks
  with the same mechanism as safeclib.
- Return-code families differ: safeclib returns positive constraint codes
  (400/401/402/403/406...), the `_auto` wrappers return NULL or an errno,
  and `memcmp_s_auto` returns a comparison result (with -1/1 used for refusal).
  Tokens deliberately discard these distinctions; the exact codes are kept in
  stderr diagnostics and in the table above.
- A match here is evidence that the *chosen cases* produce the same normalized
  safety outcome. It is not a proof of functional equivalence, performance
  equivalence, or of behavior outside this case matrix.

See `docs/libc/C_BUG_CLASS_MATRIX.md`, `docs/libc/ORDINARY_C_SAFETY.md`, and
`stdlib/safe/safe_c.h` ("HONEST LIMITS") in the repository for the full
statement of what the C+ layer does and does not enforce.
