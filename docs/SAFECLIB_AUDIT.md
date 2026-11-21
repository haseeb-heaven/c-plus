# Safeclib integration audit (Agent 2, PR #17)

## Upstream
- Repo: https://github.com/rurban/safeclib
- Pinned commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT (Cisco 2012-2013, Reini Urban 2017). `third_party/safeclib/COPYING` preserved.
- Vendored: full `src/` and `include/` in `third_party/safeclib/` with `PROVENANCE.md`, `UPSTREAM_COMMIT`.

## Audit summary
- **API inventory:** safeclib provides Annex K-style mem/str/wchar/io/os/misc/ext APIs (205 C sources). C+ currently integrates a dependency-closed mem/str subset: `memcpy_s, memmove_s, memset_s, memcmp_s, memzero_s, strcpy_s, strncpy_s, strcat_s, strncat_s, strnlen_s, strcmp_s, strtok_s`, plus constraint handlers.
- **Build system:** upstream uses autotools/CMake; C+ builds selected sources via `scripts/build_safecrt.sh` with generated config.h and `-D` symbol prefixing (`cplus_sc_*`) to avoid host libc/CRT collisions.
- **Dependencies:** selected subset depends on safeclib private headers, mem primitives, constraint handlers. No host libc replacement; linked as static `libcplus_safecrt.a`.
- **Platform support:** upstream targets POSIX/Windows; C+ currently verifies on macOS arm64 local host and has Linux CI workflow. Windows/MSVC not verified in this session.
- **Tests/license:** upstream includes extensive tests; C+ runs its own CRT unit/adversarial/fuzz/example suites against integrated APIs. Upstream test suite not fully imported/run here.

## Intel safestringlib status
- Vendored: `third_party/intel-safestringlib`, MIT, pinned `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`.
- Overlapping mem/str APIs are no longer linked as primary runtime; safeclib is primary. Intel objects remain compiled only for reference in build script and sources retained for provenance/reference until full parity/removal decision is verified.
- No two competing linked implementations for the same integrated C+ `cplus_*` API.

## Constraint handling
- Default: ignore (safeclib default). C+ exposes `cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE|ABORT)` and `cplus_safe_get_constraint_mode()`; wired to safeclib mem/str constraint handlers.
- Abort mode is process-fatal by design; tests cover ignore/default and mode toggling.

## Not-safe-by-name audit
- `_s` suffix does not imply full memory safety. Safeclib checks runtime constraints (null, bounds, overlap where specified) but cannot prove caller ownership/lifetime/provenance, alias UAF, data races, or unknown-capacity raw pointers.
- printf/scanf/wide/io/platform APIs are audited separately before claims; not broadly integrated as "safe" by name.

## Platform/support matrix (honest)
| Platform | Build | CRT tests | ASan/UBSan | Status |
|---|---:|---:|---:|---|
| macOS arm64 (local) | yes | 34/0 unit, all CRT checks pass | local toolchain runtime issue before test start | verified locally |
| Linux x86_64 (GitHub Actions) | workflow configured | workflow configured | workflow configured | remote result not independently verified here |
| Windows MSVC | not run | not run | N/A | not verified |

## Remaining limitations
- Only selected mem/str safeclib APIs integrated; many ext/wchar/io/os/misc APIs unintegrated.
- Intel safestringlib still vendored; remove only after full parity and tests.
- Compiler-side ownership/borrow/lifetime/provenance safety requires Agent 1; libc alone is not Rust-equivalent.
- Upstream safeclib full test suite and cross-platform CI evidence incomplete.
