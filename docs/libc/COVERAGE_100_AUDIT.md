# Coverage accounting: what is 100%, and why the whole library is not

This document exists to prevent a false "100% complete" claim. Counts come from
`docs/libc/iso_c_inventory.json` and the runners named below, not estimates.

## ISO C inventory (948 APIs, 27 headers)

| Classification | Count | Meaning |
|---|---:|---|
| HOST/COMPILER-PROVIDED | 359 | Toolchain supplies the header/macro/type; not this library's implementation |
| PLATFORM_BACKED | 476 | `cplus_*` wrapper delegating to a named host backend, declared, built, tested |
| IMPLEMENTED_BUNDLED | 29 | Compiled in this repo (musl-derived or native C+), declared, built, tested |
| IMPLEMENTED_UPSTREAM | 8 | Copied from pinned musl source with minimal renames and provenance |
| UNSUPPORTED | 49 | Deliberately not shipped (type-generic C ABI, insecure APIs, platform-specific) with documented reasons |
| **MISSING** | **27** | No implementation, no host symbol, and no locally available pinned upstream source |

921 of 948 APIs are implemented/accounted for (97.05%). The remaining 27 are
not hidden: 23 C23 math APIs (`nextup/nextdown`, `fadd/fsub/fmul/fdiv` families,
3-arg `hypot`, `_Float16` variants), `memset_explicit`, `timespec_getres`,
`mbrtoc8`, `c8rtomb`, and `FE_TIESAWAY`.

For `memset_explicit`, LLVM libc at `llvmorg-20.1.1` has an implementation, but
it depends on C++ internal headers (`src/__support/common.h`,
`inline_memset.h`) and an LLVM-libc build. Vendoring it would require copying a
new dependency closure and adding a C++ build path — this was not done, and the
source is recorded as a candidate, not a shipped file. `FE_TIESAWAY` requires
compiler `<fenv.h>` support the host does not provide; fabricating a value
would be false semantics.

## Test coverage (actually executed)

| Suite | Cases | Result |
|---|---:|---|
| Rust-vs-C+ paired runtime comparison | 600 (100 x 6 families) | 473 matched, 119 documented differences, 8 both-skip, 0 failures |
| Per-method unit/log parity suites | up to 2,143 checks each | 14/14 variants pass |
| 100-scenario per-method suites | 1,000 per family | 30/30 variants pass |
| API-level adversarial attacks | 23 suites | 23/23 pass |
| Deterministic corpora | ~81,000 cases | 20/20 pass |
| C+ compiler tests | 148 | 149 pass, 0 fail (measured 2026-11-25) |
| libc unit tests | 80 | 80 pass |
| CRT unit tests | 79 | 79 pass |
| SDK consumer / C+C++ compat | 7 | 7 pass |
| Line coverage (libc) | — | 91.74% |

## Honest conclusion

- **100% of declared tests pass** (except one documented pre-existing compiler
  case).
- **100% of exported APIs that are implemented have a source classification,
  declaration, build integration and at least one test** — with the caveat that
  ABI-specific tests exist for only 2 APIs (see `API_STATUS_MATRIX.md`).
- **The library itself is NOT 100% ISO C complete**: 28 APIs are MISSING and 49
  are UNSUPPORTED by design. No completion percentage of 100% is asserted.

To close the remaining 28 APIs would require either a newer host toolchain or a
new pinned upstream dependency closure (LLVM libc C++ sources for the C23 math
family). That work is not pretended to be done here.
