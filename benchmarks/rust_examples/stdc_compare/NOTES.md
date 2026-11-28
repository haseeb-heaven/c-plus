# stdc_compare — ordinary-spelling checked C vs safe Rust

This directory contains a runnable, deterministic token comparison between the
**ordinary ISO C spellings** remapped by `-include stdlib/safe/safe_c.h` to the
C+ checked layer, and a safe-Rust reference implementation. Both programs print
lines of the form

```
<api> <case> <token>
```

and `compare.sh` diffs the normalized token streams. This follows the stdout
contract of the other `benchmarks/rust_examples/*` harnesses.

## Deliverables

| File | Purpose |
| --- | --- |
| `harness.c` | C99 harness containing **only ordinary C spellings** (`memcpy`, `strcpy`, `malloc`, `free`, ...). The force-included `safe_c.h` rewrites each to the checked entry point. Diagnostics go to stderr; tokens to stdout. |
| `stdc_compare.rs` | Safe-Rust counterpart — **no `unsafe`, no FFI, no raw pointers**. Uses `copy_from_slice`, `fill`, slice equality, `get`/`get_mut`, `String::push_str`, `Vec::try_reserve_exact`, `Option`, `drop`. |
| `compare.sh` | Verifies every backing symbol with `nm`, builds both under `timeout 30`, diffs tokens, prints the summary line, exits nonzero on real mismatch. |
| `NOTES.md` | This file. |

Generated evidence lives in `out/` (ignored by the repository `.gitignore` via
`benchmarks/rust_examples/**/out/`).

## Why `-include` matters

The C harness source never writes a `_s` name. It is compiled with:

```
/usr/bin/clang -std=c99 -Wall -Wextra -Werror -I. \
  -Ilibc/install-include -Ithird_party/safeclib/include \
  -include stdlib/safe/safe_c.h \
  benchmarks/rust_examples/stdc_compare/harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o out/harness_bin
```

`safe_c.h` defines the remapping macros only when included first. For example:

```
#define memcpy(d, s, n)  ((void)cplus_memcpy_s_auto((d),(s),(n),CPLUS_SAFE_OBJ_SIZE(d)))
#define strcpy(d, s)     cplus_strcpy_s_auto((d),(s),CPLUS_SAFE_OBJ_SIZE(d))
#define malloc(sz)       malloc_s(sz)
#define free(p)          free_s(p)
```

`CPLUS_SAFE_OBJ_SIZE` is `__builtin_object_size(p, 1)`, so a stack destination
of declared size `N` reports capacity `N`. The harness uses a **separate 0xA5
guard object** (not a trailing region of the same array), so an over-sized
request is refused on capacity grounds before the guard could ever be reached.
This keeps `CANARY_BROKEN` impossible in both programs.

## Authoritative symbol inventory (nm evidence)

Source of truth for scope:

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
nm -g build/safe/libcplus_safecrt.a | grep ' T _'
```

Every ordinary C spelling used by the harness maps to a **real defined text
symbol** in `build/safe/libcplus_safecrt.a`. Raw excerpt (address, type, name):

```
00000000000004c8 T _cplus_memcpy_s_auto
00000000000007b8 T _cplus_memmove_s_auto
0000000000000934 T _cplus_memset_s_auto
0000000000000b5c T _cplus_memcmp_s_auto
000000000000069c T _cplus_strcpy_s_auto
00000000000007dc T _cplus_strncpy_s_auto
0000000000000920 T _cplus_strcat_s_auto
0000000000000a80 T _cplus_strncat_s_auto
0000000000000000 T _malloc_s
00000000000001e4 T _calloc_s
0000000000000470 T _realloc_s
0000000000000bf4 T _free_s
```

Full mapping used by `compare.sh`:

| Ordinary C spelling | Checked symbol backing it |
| --- | --- |
| `memcpy` | `cplus_memcpy_s_auto` |
| `memmove` | `cplus_memmove_s_auto` |
| `memset` | `cplus_memset_s_auto` |
| `memcmp` | `cplus_memcmp_s_auto` |
| `strcpy` | `cplus_strcpy_s_auto` |
| `strncpy` | `cplus_strncpy_s_auto` |
| `strcat` | `cplus_strcat_s_auto` |
| `strncat` | `cplus_strncat_s_auto` |
| `malloc` | `malloc_s` |
| `calloc` | `calloc_s` |
| `realloc` | `realloc_s` |
| `free` | `free_s` |

Support symbols also verified: `cplus_runtime_set_violation_trap`,
`cplus_sc_set_mem_constraint_handler_s_wrapper`,
`cplus_sc_set_str_constraint_handler_s_wrapper`, `cplus_safe_alloc_fail_after`,
`cplus_safe_alloc_fail_reset`, `cplus_safe_ptr_is_live`.

**BLOCKED list is empty** in this build: all 12 APIs have a real checked symbol
and none were skipped. `compare.sh` still re-verifies each symbol with `nm`
(`nm_has`) and records the result in `out/symbols.txt`. If a future rebuild drops
a symbol, `compare.sh` prints `BLOCKED ... (symbol absent)` to stderr, excludes
that API from *both* argv lists, increments `<blocked>`, and never emits a
guessed token for the absent symbol. A missing *support* symbol fails closed
(exit 2) because the harness cannot run without it.

## Exact commands and toolchains

```sh
# From the repo root, with build/safe/libcplus_safecrt.a and
# build/libc/libcplus_hardened.a already built:
sh benchmarks/rust_examples/stdc_compare/compare.sh
```

`compare.sh` executes, in essence:

```sh
/usr/bin/clang -std=c99 -Wall -Wextra -Werror \
  -I. -Ilibc/install-include -Ithird_party/safeclib/include \
  -include stdlib/safe/safe_c.h \
  benchmarks/rust_examples/stdc_compare/harness.c \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o out/harness_bin

rustc --edition 2021 -O benchmarks/rust_examples/stdc_compare/stdc_compare.rs \
  -o out/examples_bin

timeout 30 out/harness_bin   <nm-verified API list> > out/c.out    2> out/c.err
timeout 30 out/examples_bin  <nm-verified API list> > out/rust.out 2> out/rust.err
# sort each, diff, and count tokens keyed by "<api>|<case>"
```

Toolchain versions captured in `out/toolchains.txt` on the verification run:

```
clang: Apple clang version 16.0.0 (clang-1600.0.26.6)
rustc: rustc 1.100.0 (0ef1bc386 2026-11-09)
nm:    llvm-nm, compatible with GNU nm
date:  2026-11-23T05:57:39Z
```

Both harnesses accept an optional API-name list as argv. With no argv they emit
all 12 APIs; `compare.sh` always passes the nm-verified list so both sides are
restricted identically.

## Case matrix

Case spellings in the output (`zero_length`, `one_over`, `huge_n`) correspond to
the requested "zero-length", "one-over", and "huge-n" cases. Six cases per API:

| Case | Meaning |
| --- | --- |
| `valid` | correctly sized request; operation succeeds |
| `null` | null pointer (memory/string APIs) or injected allocation failure returning NULL (allocators) |
| `zero_length` | `n == 0` / empty source / zero-size allocation |
| `one_over` | request exceeds the declared capacity by one byte |
| `huge_n` | `n == (size_t)-1` / over-long source / size above `RSIZE_MAX_MEM` |
| `canary` | explicit check that the separate 0xA5 guard survived |

Token meanings:

- `OK` — operation completed, guard intact
- `REFUSED` — invalid condition detected before an out-of-bounds write, guard intact
- `N/A` — case has no meaning for that API (pure allocators: `one_over`; `free`: `zero_length`/`one_over`/`huge_n`)
- `CANARY_OK` — explicit case: the guard is intact
- `CANARY_BROKEN` — bytes past the declared capacity were modified (never observed; would be a real defect)

### Observed behavior (C side, from `out/c.err` and return values)

| API family | valid | null | zero_length | one_over | huge_n | canary |
| --- | --- | --- | --- | --- | --- | --- |
| memcpy / memmove / memset | OK | REFUSED | OK | REFUSED (ESNOSPC 406) | REFUSED | CANARY_OK |
| memcmp | OK (equal) | REFUSED (-1) | OK (n=0) | REFUSED | REFUSED | CANARY_OK |
| strcpy / strcat | OK | REFUSED | OK (empty src) | REFUSED | REFUSED (long src) | CANARY_OK |
| strncpy / strncat | OK | REFUSED | OK (n=0) | REFUSED | REFUSED (ESLEMAX) | CANARY_OK |
| malloc / calloc / realloc | OK | REFUSED (injected NULL) | OK (defined 1-byte block) | N/A | REFUSED | CANARY_OK |
| free | OK | OK (no-op) | N/A | N/A | N/A | CANARY_OK |

Notes on the model:

- The memory/string checks come from the `_auto` wrappers combining
  `__builtin_object_size` (stack) with tracked allocation capacity (heap). The
  observed refusals are `ESNOSPC 406` / null-pointer refusals; they are reported
  to stderr and counted, never aborted (`cplus_runtime_set_violation_trap(0)`
  plus counting constraint handlers, harness-only).
- `memcmp` checks `n == 0` before the null check, so the `null` case uses a
  nonzero `n` and is refused; `zero_length` uses two non-null buffers.
- For `strcpy`/`strcat`, which have no length argument, "zero_length" means an
  empty source string and "huge_n" means an over-long source string; for
  `strncpy`/`strncat` the same labels mean `n == 0` and `n == (size_t)-1`.
- Allocator "null" uses `cplus_safe_alloc_fail_after(0)` to force a NULL return,
  then `cplus_safe_alloc_fail_reset()`. On an injected `realloc` failure the
  original block is left live and is freed by the harness.
- "huge_n" for allocators uses a size above `RSIZE_MAX_MEM` (SIZE_MAX >> 1);
  `calloc` uses `SIZE_MAX * 2`, exercising the overflow branch. Safe Rust models
  this with `Vec::try_reserve_exact`, a genuine `Err` return, not a panic.
- Safe Rust has no null reference and no OOM injection: `None` models null and
  a `force_fail` flag models the injected NULL. The canary is a separate `[u8;
  8]` of `0xA5`, so `CANARY_BROKEN` is unrepresentable on the Rust side.

## Actual verification run

```
$ sh benchmarks/rust_examples/stdc_compare/compare.sh
RUST_EXAMPLES stdc_compare 12 72 72 0 0
$ echo $?
0
```

- 12 APIs × 6 cases = 72 case lines (including the nine `N/A` lines: three
  allocators × `one_over`, plus `free` × `zero_length`/`one_over`/`huge_n`).
- `out/diff.txt` is **empty** — the sorted token streams are byte-identical.
- `out/mismatch_detail.txt` is **empty** — no missing or mismatched keys.
- Exit code 0. Both builds were warning-free (`-Werror` on the C side).

## MATCHING TOKENS ARE NOT EQUIVALENT GUARANTEES

The token equality above records **observed outcomes on a fixed set of inputs**.
It does **not** mean the C+ checked layer and safe Rust provide the same safety
guarantees, and no Rust-equivalence or 100%-safety claim is made:

- The C functions are **runtime checks** over caller-supplied extents
  (`__builtin_object_size`, tracked allocation capacity, `n`). They detect null
  pointers, over-sized requests, and prohibited sizes when the caller's
  declared capacity is truthful. They do **not** prove provenance, ownership,
  lifetime, or alias/UAF safety; `safe_c.h` states these as "HONEST LIMITS".
- If the caller lies about capacity (e.g. casts a smaller object to a larger
  declared size), a runtime check can be fooled and memory can be corrupted.
  Safe Rust cannot express that program. The canary here keeps capacities
  truthful on both sides, which is exactly why tokens match.
- Safe Rust obtains its guarantees **statically**: a null reference is
  unrepresentable, an out-of-bounds slice index returns `None` or is a
  compile-time error, and two mutable references cannot alias. Refusals are
  `None`/`Err` values, not runtime constraint-handler callbacks. The `Option`-
  based models in `stdc_compare.rs` are *illustrations* of that discipline, not
  runtime checks with the same mechanism as the C layer.
- Return-code families differ (safeclib codes, errno values, NULL, comparison
  results). Tokens deliberately discard those distinctions; exact diagnostics
  remain in `out/c.err`.
- A match is evidence that the *chosen cases* produce the same normalized safety
  outcome. It is not a proof of functional equivalence, performance
  equivalence, or of behavior outside this case matrix.

See `stdlib/safe/safe_c.h` ("WHAT IS ACTUALLY ENFORCED" / "HONEST LIMITS"),
`stdlib/safe/safe_memory.h`, `stdlib/safe/safe_alloc.h`, and
`docs/libc/C_BUG_CLASS_MATRIX.md` for the full statement of what the checked C
layer does and does not enforce.
