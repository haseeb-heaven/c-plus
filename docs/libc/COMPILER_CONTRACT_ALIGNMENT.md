# Compiler contract alignment (Agent 2 -> Agent 1)

This document records the additive alignment of Agent 2's hardened libc contracts with
the compiler-readable schema requested by Agent 1.

## Cross-reference

Source request: [issue #22 comment](https://github.com/haseeb-heaven/c-plus/issues/22),
Agent 1 (2026-11-21):

> added compiler/Analysis/libc_contracts.json (29 ordinary + checked APIs) with
> ownership, transfer, extent, effects, failure, diagnostic category and dynamic-check
> fields; the Clang AST checker now loads this JSON ... RequiredAgent2 contract keys:
> params, ownership, transfer, extent, effects, failure, diagnostics (CPLUS-SAFE-*),
> dynamic. Please emit/align libc/contracts with these names/signatures.

Agent 1 also asked for the compiler diagnostics `[CPLUS-SAFE-FREE] double free of <var>`,
`[CPLUS-SAFE-LIFETIME] use-after-free`, `[CPLUS-SAFE-BOUNDS] copy of N bytes exceeds
destination <var>`, `[CPLUS-SAFE-CRT] checked string copy needs N bytes but capacity is M`
and `[CPLUS-SAFE-REALLOC] realloc warnings` to be referenceable from tests.

**Agent 1's compiler file (`compiler/Analysis/libc_contracts.json`) is not present on this
branch (`agent/modern-stdlib`): `compiler/Analysis/` contains only `BoundsAnalysis.cpp` and
`OwnershipAnalysis.cpp`.** The schema below was therefore implemented from the comment's
written requirements, not by reading Agent 1's JSON. It **must be re-validated against the
real compiler file when PR #16 merges** — in particular field value spellings, exact
diagnostic category text, and whether the checker expects ordinary names (`malloc`) or
bundled names (`cplus_malloc`). This file intentionally does not modify Agent 1's compiler
or runtime code.

## Files added

| File | Purpose |
| --- | --- |
| `libc/contracts/compiler_libc_contracts.json` | `schema_version: 1`, `source: "Agent 2 hardened libc"`, explanatory `note`, and 32 `apis` entries (the 29 required names plus `memcpy_s`, `strcpy_s`, `snprintf_s`). |
| `tests/libc/test_compiler_contract_alignment.py` | Standalone schema/name/diagnostic/dynamic check; prints `compiler contract alignment: ok`. |
| `docs/libc/COMPILER_CONTRACT_ALIGNMENT.md` | This document. |

No existing contract file, compiler source, runtime source, or test was modified.
`libc/contracts/contracts.json`, `ownership_contracts.json`, `libc_contracts.json`,
`symbol_map.json` and `c17_contracts.json` remain unchanged; the new JSON is additive.

## Required keys -> where emitted

Each entry in `apis` has **exactly** the nine required keys (no omissions, no extras).
`params` entries have exactly `name`, `type`, `nullable`, `extent`; `failure` has exactly
`result_code`, `conditions`; `dynamic` has exactly `checks`, `limitations`.

| Required key | Emitted as | Source of truth / examples |
| --- | --- | --- |
| `name` | Standard C spelling, in header parameter order | `malloc`, `fprintf`, `snprintf`, ...; bundled implementations are the `cplus_*` symbols in `libc/install-include/cplus_libc.h`. Parameter order matches the standard signature so Agent 1's format-argument indices (e.g. `fprintf=1`, `snprintf=2`, `scanf=0`, `fscanf=1`) can be applied. |
| `params` | Array of `{name,type,nullable,extent}` | `libc/install-include/cplus_libc.h`, `stdlib/safe/safe_c.h` (opt-in mappings), `libc/src/string/memory.c`, `libc/src/string/string.c`, `libc/src/string/extra.c`, `libc/src/stdio/stdio.c`. `nullable` marks arguments for which `NULL` is accepted (`realloc.ptr`, `free.ptr`, `fread/fwrite.ptr`, `snprintf.str` when `size == 0`, `snprintf_s.dest` when `dmax == 0`). Variadic arguments are represented by a trailing `{name:"...",type:"..."}` entry to preserve indices. |
| `ownership` | `returns_owned` / `borrow` / `consumes` | `libc/contracts/ownership_contracts.json` vocabulary; allocators return owned blocks, string/memory/format APIs borrow, `free`/`fclose` consume. |
| `transfer` | `"return"` / `"arg"` / `null` | `libc/src/stdlib/alloc.c` (`cplus_transfer_ownership`, generation bumps), `stdlib/safe/src/safe_alloc.c`. `null` means no ownership change. |
| `extent` | Human-readable extent invariant | e.g. `memcpy`: `n bytes at dest/src; caller must prove n <= destination capacity`; `snprintf_s`: `dmax <= tracked/object capacity of dest`. Derived from `libc/src/string/bounded.c` `check_capacity`, `cplus_alloc_remaining_capacity` and `__builtin_object_size`. |
| `effects` | Non-empty string array | `allocates`, `zero-init`, `frees-on-success`, `writes dest`, `reads src`, `advances stream`, `checked CRT copy`, etc. Read directly from the implementation bodies. |
| `failure` | `{result_code, conditions:[...]}` | `NULL`/`-1`/`EOF`/short-count/`errno_t` sentinels plus the real errno/constraint paths: `ENOMEM`, `EINVAL`, `EOVERFLOW`, `EBADF`, `ERANGE`, `ESNOSPC 406`, UAF refusal (400), injected allocation failure. Sources: `libc/src/stdlib/alloc.c`, `libc/src/stdio/stdio.c`, `stdlib/safe/src/safe_memory.c`, `stdlib/safe/src/safe_string.c`, `stdlib/safe/src/safe_format.c`. |
| `diagnostics` | Array of the five exact Agent 1 category strings | `free`: FREE/LIFETIME; `realloc`: REALLOC/LIFETIME; `memcpy`/`memmove`/`memset`/`memcmp`: BOUNDS; `strcpy`/`strncpy`/`strcat`/`strncat`: CRT; checked `_s` variants add LIFETIME. Read-only and format/file APIs carry an empty array — the library does not emit those categories for them. |
| `dynamic` | `{checks:[...], limitations:[...]}` | Actual runtime guards only: `cplus_safe_reject_uaf` / `cplus_safe_ptr_was_freed`, bounded 1024-entry recent-free caches (open-addressed in `libc/src/stdlib/alloc.c`, direct-mapped in `stdlib/safe/src/safe_alloc.c`), `cplus_alloc_remaining_capacity` for live/interior heap pointers, `__builtin_object_size(p,1)`, unknown-capacity strict/default mode, ABA generation guard in `cplus_realloc`, allocation-failure injection, `size*nmemb` overflow guard in `fread`/`fwrite`, bounded 256-entry stdio live/closed tables, `%n`/non-literal-format checks in `cplus_*_s_checked`. |

## Honesty notes

- This is runtime diagnosis, not compiler enforcement. `diagnostics` names categories that
  Agent 1's compiler may emit; the library itself emits generic `[CPLUS-SAFE] <api>: ...`
  lines through `cplus_runtime_violation` (`stdlib/safe/src/safe_runtime.c`, trap exits 70)
  and metadata-only log lines. No entry claims a static proof.
- Unknown capacity is never passed off as verified capacity: default mode warns and keeps
  raw-C behaviour, strict mode (`cplus_libc_set_unknown_capacity_mode(1)` /
  `cplus_safe_set_unknown_capacity_mode(1)`) refuses with `EINVAL`.
- Standard APIs and checked `_s` APIs are distinct entries. Plain `strcpy`/`sprintf` have
  no destination-capacity argument; only `strcpy_s`/`snprintf_s` (and the opt-in auto
  wrappers) bound the write.
- Double/invalid-free and alias-invalidation detection are heuristic and bounded, not a
  provenance or arbitrary-alias guarantee. Each affected entry states this in
  `dynamic.limitations`.
- `fread` carries the BOUNDS category as the potential violation; the library checks
  NULL arguments and `size*nmemb` overflow only, which is stated in its limitations.

## Verification (exact commands and results)

Run from `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` on branch
`agent/modern-stdlib`:

```sh
$ python3 tests/libc/test_compiler_contract_alignment.py
compiler contract alignment: ok
$ echo $?
0

$ python3 tests/libc/validate_contracts.py
contract validation: ok (5 files validated, 202 C17 APIs)
$ echo $?
0

$ python3 -m json.tool libc/contracts/compiler_libc_contracts.json >/dev/null && echo JSON_OK
JSON_OK
$ echo $?
0
```

Additional robustness runs:

```sh
$ python3 -O tests/libc/test_compiler_contract_alignment.py
compiler contract alignment: ok          # exit 0; checks are explicit, not bare assert
$ cd /tmp && python3 /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/tests/libc/test_compiler_contract_alignment.py
compiler contract alignment: ok          # exit 0; contract path resolves from __file__
```

`validate_contracts.py` was not edited. Its `REQUIRED_FILES` list still contains the
original five files, so the new alignment file is deliberately outside that validator's
scope; the standalone test above is the machine check for the new file.

## Re-validation checklist for PR #16 merge

1. Diff `compiler/Analysis/libc_contracts.json` against `apis` here; confirm the 29 names
   and any extra checked names match.
2. Confirm the checker's expected key spellings and value domains (especially `ownership`,
   `transfer`, `failure` and `dynamic`) match this file.
3. Confirm the five `CPLUS-SAFE-*` strings are the exact text Agent 1 emits; adjust both
   the JSON and `ALLOWED_DIAGNOSTICS` in the test if the final text differs.
4. Confirm whether the checker expects ordinary (`malloc`) or bundled (`cplus_malloc`)
   names, and add a name alias field only if Agent 1 requires it (the current nine-key
   contract forbids extra keys).
5. Re-run all three verification commands above, then run the compiler-side contract
   loading test from Agent 1's side.
