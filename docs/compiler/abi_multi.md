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

### KNOWN_OPEN: incompatible cross-TU ownership declarations (issue #4 residual gap)

**Still OPEN.** A single-TU compiler cannot compare declarations of the same
external symbol in different translation units, so an owner-side release
boundary declared with an incompatible ownership/pointer signature is **not
diagnosed** in any mode. This is the residual diagnosis gap of issue #4
(duplicate `.hp` symbols were fixed; the declaration-mismatch diagnosis was
not); also `docs/LIMITATIONS.md` Bug 35. No compiler code was changed to
produce the evidence below.

Fixture (two real TUs, unchanged native C ABI — `int *` and `long *` are both
plain 8-byte LP64 object pointers; no new type, token or wrapper):

- `tests/known_gaps/abi_mismatch/cross_tu_owner_def.c` — TU A **defines**
  `void resource_release(int *)`, releasing the owned pointer with
  `cplus_release`; `resource_acquire` obtains it from `cplus_alloc`.
- `tests/known_gaps/abi_mismatch/cross_tu_owner_decl.c` — TU B **declares the
  same symbol** as `void resource_release(long *)` (incompatible pointer /
  ownership signature) and calls it.

Reproduce with the **prebuilt** compiler only (no rebuild). Let `$C` be
`<repo>/build/c+`, `$R` be `<repo>/tests/known_gaps/abi_mismatch`, and `$RT` be
`<repo>/build/libcplus_runtime.a`:

```sh
$C build $R/cross_tu_owner_def.c  --strict --sanitize=none -c -o $R/results/work/cross_tu_owner_def.strict.o
$C build $R/cross_tu_owner_decl.c --strict --sanitize=none -c -o $R/results/work/cross_tu_owner_decl.strict.o
/usr/bin/clang -std=gnu11 -O1 \
  $R/results/work/cross_tu_owner_def.strict.o \
  $R/results/work/cross_tu_owner_decl.strict.o $RT \
  -o $R/results/work/prog.strict
$R/results/work/prog.strict
```

Observed output (STRICT; `--legacy-c` and `--default` produce the same clean
result, with no note at all):

```
build cross_tu_owner_def.c  --strict ... -> rc=0
  note: [CPLUS-MULTITU] STRICT analysis is per-translation-unit: functions
        'cplus_alloc', 'cplus_fatal', 'cplus_release' have no definition in
        this TU; cross-TU control/data flow is not safety-checked here
build cross_tu_owner_decl.c --strict ... -> rc=0
  note: [CPLUS-MULTITU] STRICT analysis is per-translation-unit: functions
        'resource_acquire', 'resource_release' have no definition in this TU;
        cross-TU control/data flow is not safety-checked here
clang ... -o prog.strict                 -> rc=0
./prog.strict                            -> rc=0
  no cross-TU diagnostic
```

`nm -g` confirms both objects use the **same** symbol: TU A has `T
_resource_release`, TU B has `U _resource_release`. Neither TU emits an
`error:` or `warning:` about the conflicting ownership/pointer type, the link
succeeds, and the program prints the sentinel. The only STRICT output is the
`[CPLUS-MULTITU]` note, which explicitly discloses that cross-TU flow is not
safety-checked. **This confirms the gap remains OPEN: there is no compiler-side
diagnosis of incompatible cross-TU ownership declarations.** Verbatim
absolute-path commands, per-step rc/stdout/stderr and the `verdict: KNOWN_OPEN`
self-check are in `tests/known_gaps/abi_mismatch/results/summary.{txt,json}`;
regenerate with `python3 tests/known_gaps/abi_mismatch/run.py`.

- Wrong or missing ABI declarations across TUs (e.g. `int` vs `double`)
  cannot be detected by a per-TU analysis; the ownership-declaration instance
  is reproduced above and in `tests/known_gaps/abi_mismatch/` (Bug 35).
- Cross-TU allocation paired with release in another TU still requires an
  explicit validated contract; STRICT rejects it, it does not prove it.
- Shared-library symbol interposition at runtime, opaque-handle misuse
  without a create/destroy contract, and callback-based deferred frees are
  outside the per-TU proof (`docs/compiler/abi_ownership.md`,
  `docs/compiler/pointer-provenance.md`).
- No Windows host or Windows CRT ABI claim; see
  `docs/compiler/windows_wine_highball_evidence.md` for the limited
  Windows-target smoke only.
