# Ordinary C API safety (no `_s` rewrite required)

Goal: unchanged ISO C calls such as `malloc`, `memset`, `memcpy`, `strcpy`,
`fread` and `printf` must benefit from C+ hardening. Callers should not have to
replace every call with a checked variant.

## How to activate

Include the layer first (or force-include it) and link both libraries:

```c
#include "stdlib/safe/safe_c.h"     /* enables the mappings on inclusion */
```

```sh
clang -std=c99 -I. -Istdlib/safe -Ilibc -Ilibc/install-include \
  -Ithird_party/safeclib/include -Ithird_party/intel-safestringlib/include \
  app.c build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
  -lm -pthread -o app
```

Define `CPLUS_SAFE_C_DISABLE` before the include to get declarations only.

`runtime/include/cplus/cplus.h` (Agent 1's compiler ABI header) is deliberately
not modified; activation is explicit and reviewed, never a silent interposition
of host libc symbols.

## What the ordinary names map to

| Ordinary call | Hardened target | Enforcement |
|---|---|---|
| `malloc/calloc/realloc/free` | `malloc_s/calloc_s/realloc_s/free_s` | overflow + failure checks, live metadata, double/invalid-free refusal |
| `memset`, `memcpy`, `memmove`, `memcmp` | `cplus_*_s_auto` | capacity from `tracked remaining capacity` for heap + `__builtin_object_size(p,1)` for stack/global; overrun refused |
| `strcpy`, `strncpy`, `strcat`, `strncat` | `cplus_str*_s_auto` | same capacity source, plus terminator/constraint checks |
| `fread`, `fwrite` | `cplus_fread`, `cplus_fwrite` | `size * count` overflow guard, NULL/stream guards |
| `printf`, `fprintf` | `cplus_printf`, `cplus_fprintf` | Clang `format(printf, m, n)` attributes → compile-time argument checking |

## Capacity provenance (the critical fix)

The first implementation queried tracked allocation size only by **base**
pointer. A call like `memset(buffer + offset, 0x41, n)` therefore looked
"unknown" and was allowed — exactly the failure mode reported against the
original demo. `cplus_safe_remaining_capacity()` now:

- finds the live tracked block that contains `ptr` (interior pointers included),
- returns `base + size - ptr`, including **0** at the one-past-the-end address,
- returns `(size_t)-1` only when the pointer is genuinely not in a tracked block.

Default mode preserves raw-C behaviour for genuinely unknown capacity but
emits a warning and makes **no safety claim**. Strict mode
(`cplus_safe_set_unknown_capacity_mode(1)`) refuses the operation.

## Observed behaviour (regression evidence)

`tests/crt/unit/test_safe_c_api.c` and `scratch/demo.c` use only ordinary calls:

```
Requested allocation: 64 bytes
iteration 0 ... iteration 4
[CPLUS-SAFE] memset_s: buffer overflow refused (requested 16 > capacity 0, ESNOSPC 406)
```

Exit status is 70 (deterministic violation), not SIGSEGV. Both tests are clean
under UBSan.

## Honest limits

- A multiplication that wraps **before** reaching `malloc` cannot be repaired
  by a libc; constant cases are for the compiler to reject, and variable cases
  need a checked arithmetic helper. This layer protects the write against the
  allocation that actually happened.
- Format-string validity remains the caller's responsibility for raw
  `printf`/`fprintf`; the checked `_s` format API is the safe path for
  untrusted formats and rejects `%n`.
- Overlap in raw `memcpy` is still UB; use `memmove`.
- Unknown capacity is never presented as verified capacity.
- This is runtime diagnosis with tracked provenance for this allocator, not a
  proof of alias/lifetime safety for arbitrary C pointers.

## Two independent safe ordinary-C layers

Both shipped libraries now protect unchanged C calls; neither requires `_s`
spelling and neither interposes host libc symbols.

| Layer | Header | Link | Constraint reporting |
|---|---|---|---|
| Plain C+ libc | `libc/install-include/cplus_libc_safe.h` | `build/libc/libcplus_hardened.a` | `cplus_libc_log` + `errno = ERANGE` |
| C+ SafeCRT | `stdlib/safe/safe_c.h` | plus `build/safe/libcplus_safecrt.a` | safeclib constraint handler (ESNOSPC 406) |

Both use tracked capacity for `cplus_malloc` blocks, including interior
pointers and the one-past-the-end address, plus `__builtin_object_size(p,1)`
for stack/global objects. Both distinguish unknown capacity from verified
capacity and offer a strict fail-closed mode
(`cplus_libc_set_unknown_capacity_mode(1)` /
`cplus_safe_set_unknown_capacity_mode(1)`).

Evidence: `tests/libc/test_libc_safe_api.c` (plain libc only) and
`tests/crt/unit/test_safe_c_api.c` (C+ SafeCRT), both clean under UBSan.
