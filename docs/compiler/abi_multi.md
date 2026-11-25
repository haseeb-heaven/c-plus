# Cross-module ownership and ABI verification

**Claim boundary.** All evidence here is for the native C ABI on the host
toolchain (macOS arm64, LP64). No new ABI, wrapper type or provenance token
was introduced. Symbol names, struct layouts, calling conventions and libc
compatibility are checked by the existing suites listed below.

## First verified (skipped — already independently proven)

| Scenario | Evidence | Verdict |
| --- | --- | --- |
| Multi-file compile/link/run, both link directions, `nm` symbol assertions | `tests/compiler/abi_mixed/` (12 steps, 0 failures) | COMPLETE |
| Pointer/scalar/struct/union/bitfield/enum layout, callback conventions, `qsort`, variadic calls, exported symbols, clang-oracle cross-checks | `tests/compiler/abi_real_world/` (36 checks; clang oracle runs) | COMPLETE |
| Function-pointer and callback conventions across modules | `abi_safety/callback_conventions`, `abi_ownership/callbacks_fnptr` | COMPLETE |
| Opaque handle create/use/destroy contract shape | `abi_ownership/opaque_handle` (3/3) | COMPLETE for the contract shape |
| Variadic ABI correctness | `abi_ownership/variadic_call`, `abi_real_world/variadic_abi` | COMPLETE |
| Allocator ownership across modules (malloc↔free, managed↔release, negatives with runtime exits 104/105) | `abi_ownership/managed_cross_tu`, `managed_cross_compiler`, `libc_malloc_to_free`, `libc_malloc_from_cplus`, `neg_allocator_mismatch`, `neg_double_release_cross_tu` | COMPLETE |
| GCC/Clang/Rust cross-compiler scenarios | `tests/compiler/cross_compiler/` (26 checks, 6 benchmarks) | COMPLETE |
| Source preservation and exact C symbol names | `tests/compiler/source_preservation/` (4/4), `abi_safety/symbol_names` | COMPLETE |
| STRICT fail-closed gate for bodiless pointer-effect externs | `compiler/Driver/Driver.cpp` `unverifiedPointerExterns` | COMPLETE |

Detailed per-case evidence: `tests/compiler/abi_gate/results/`,
`tests/compiler/abi_ownership/results/summary.txt`, and the per-suite
`results/summary.{txt,json}` files.

## Regression found and fixed

The default warnings-as-errors policy promoted Clang's
`'-l<lib>': 'linker' input unused [-Werror,-Wunused-command-line-argument]`
on the per-file `-c` compile step into a hard error. That broke every
static/shared-library link through `c+ build --default|--strict` while
`--legacy-c` still passed. `compiler/Driver/Driver.cpp` now strips
linker-only inputs (`-L`, `-l`, `-Wl,`, `-shared`) from compile steps and
keeps them on the final link. Re-verified: ABI gate 5/5 and abi_ownership
42/42 (201 steps).

New focused suite: `tests/compiler/abi_link_flags/run_suite.py` — static
archive link in all three modes, shared-library link with
`DYLD_LIBRARY_PATH`, `check` with linker flags, and STRICT fail-closed vs
LEGACY acceptance for an uncontracted cross-TU allocator boundary.
Evidence: `tests/compiler/abi_link_flags/results/summary.{txt,json}`.

## OPEN / not claimed

- Wrong or missing ABI declarations across TUs (e.g. `int` vs `double`)
  cannot be detected by a per-TU analysis; recorded in
  `tests/known_gaps/abi_mismatch/` and `docs/LIMITATIONS.md` (Bug 35).
- Cross-TU allocation paired with release in another TU still requires an
  explicit validated contract; STRICT rejects it, it does not prove it.
- Shared-library symbol interposition at runtime, opaque-handle misuse
  without a create/destroy contract, and callback-based deferred frees are
  outside the per-TU proof (`docs/compiler/abi_ownership.md`,
  `docs/compiler/pointer-provenance.md`).
- No Windows host or Windows CRT ABI claim; see
  `docs/compiler/windows_wine_highball_evidence.md` for the limited
  Windows-target smoke only.
