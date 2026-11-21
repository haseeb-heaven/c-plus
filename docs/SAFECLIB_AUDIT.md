# Safeclib integration audit (Agent 3, main safer-libraries maintenance)

## Upstream
- Repo: https://github.com/rurban/safeclib
- Pinned commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT (Cisco 2012-2013, Reini Urban 2017). `third_party/safeclib/COPYING` preserved.
- Vendored: full `src/` and `include/` in `third_party/safeclib/` with `PROVENANCE.md` and `UPSTREAM_COMMIT`.
- Local adaptations: build-time symbol prefixing/wrappers for C+ integration (`scripts/build_safecrt.sh`, `third_party/safeclib/src/cplus_sc_wrappers.c`); no upstream license/provenance files removed.

## Audit summary
- **API inventory:** safeclib provides Annex K-style mem/str/wchar/io/os/misc/ext APIs (205 C sources). C+ integrates a dependency-closed safeclib-backed mem/str subset: `memcpy_s, memmove_s, memset_s, memcmp_s, memzero_s, strcpy_s, strncpy_s, strcat_s, strncat_s, strnlen_s, strcmp_s, strtok_s`, plus constraint handlers. C+ exposes them as `cplus_*` adapters in `stdlib/safe`.
- **Build system:** upstream uses autotools/CMake; C+ builds selected sources via `scripts/build_safecrt.sh` with a generated config.h and `-D` symbol prefixing (`cplus_sc_*`) to avoid host libc/CRT collisions. Intel safestringlib remains vendored for provenance/reference but is not compiled/linked by `build_safecrt.sh`.
- **Dependencies:** selected subset depends on safeclib private headers, mem primitives, and constraint handlers. No host libc replacement; linked as static `build/safe/libcplus_safecrt.a`.
- **Platform support:** upstream targets POSIX/Windows; C+ verifies locally on macOS arm64. Linux/Windows CI status is not claimed as independently verified by this local run.
- **Tests/license:** upstream includes extensive tests; C+ runs its own CRT unit/adversarial/fuzz/example suites against integrated APIs. Upstream safeclib full test suite not imported/run here.

## Intel safestringlib status
- Vendored: `third_party/intel-safestringlib`, MIT, pinned `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`.
- Not compiled/linked by `scripts/build_safecrt.sh`; safeclib is the primary backend for integrated mem/str `cplus_*` APIs.
- Sources retained for provenance/reference until full parity/removal decision is verified. No two competing linked implementations for the same integrated C+ `cplus_*` API.

## Constraint handling
- Default: `CPLUS_SAFE_CONSTRAINT_IGNORE` (safeclib ignore behavior).
- API: `cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE|CPLUS_SAFE_CONSTRAINT_ABORT)` and `cplus_safe_get_constraint_mode()` in `stdlib/safe/safe_error.h`.
- Implementation: `stdlib/safe/src/safe_error.c` stores mode atomically, applies selected handler to both mem and str constraint handlers through vendored wrappers, and falls back to IGNORE for invalid mode values.
- Abort mode is process-fatal by design; tests cover ignore/default, invalid-mode fallback, and mode toggling.

## Adapter consistency (current maintenance pass)
- Safeclib-backed mem/str adapters: return safeclib `errno_t`; log TRACE on entry and ERROR on non-zero constraint/error results. `strtok_s` logs WARN on null result; `strnlen_s` is a size query and logs TRACE only.
- Alloc adapters: validate out/inout args, return `CPLUS_SAFE_*` codes, log ERROR on invalid/overflow/OOM, DEBUG on success.
- IO adapters: validate args/stream pointers, return `CPLUS_SAFE_*`/errno as implemented, log WARN/ERROR on failures, DEBUG on success, TRACE on hot read/write paths.
- Format adapters: platform-backed bounded `vsnprintf` family; null format/invalid dest+dmax return -1, set `errno=EINVAL`, and log ERROR. printf/fprintf/snprintf-family format/runtime failures return -1, set `errno` (preserving the underlying libc value), and log ERROR; scanf-family negative returns log WARN (EOF/error) and leave `errno` as set by libc. Bounded variants follow the C99 `snprintf` truncation contract (bounded, NUL-terminated, returns would-be length); `sprintf_s/vsprintf_s` are aliases of that bounded behavior, not Annex K.
- Logging remains metadata-only, OFF by default, and avoids dumping user buffers/secrets.

## Not-safe-by-name audit
- `_s` suffix/runtime checks do not imply full memory safety. Safeclib checks runtime constraints (null, bounds, overlap where specified) but cannot prove caller ownership/lifetime/provenance, alias UAF, data races, or unknown-capacity raw pointers.
- Platform-backed printf/scanf/stdio adapters are not claimed as Annex K-identical across platforms; they are audited as bounded/checked adapters where applicable.

## Verification (local, 2026-05-22)
Commands run in `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` on branch `agent/modern-stdlib`:

| Command | Result |
|---|---|
| `sh scripts/build_safecrt.sh` | pass; produced `build/safe/libcplus_safecrt.a` |
| `sh tests/crt/run_all_crt.sh` | pass; `CRT corpus: 10 passed, 0 failed`, `safecrt tests: 6 passed, 0 failed`, `examples: 26 passed, 0 failed`, `fuzz smoke: 4 passed, 0 failed`, `ALL CRT CHECKS PASSED` |
| `sh tests/crt/unit/run_unit_tests.sh` | pass; `crt unit tests: 38 passed, 0 failed` |
| `sh tests/libc/run_libc_tests.sh` | pass; `libc tests: 25 passed, 0 failed` |

Evidence reported by existing CRT runner output also includes the CRT corpus native probe and C+ safe-mode comparison. I did not independently re-verify remote GitHub Actions CI from this local session.

## Platform/support matrix (honest)
| Platform | Build | CRT/unit/libc tests | ASan/UBSan | Status |
|---|---:|---:|---:|---|
| macOS arm64 (local) | yes | `run_all_crt.sh` pass; CRT unit 38/0; libc 25/0 | not run in this pass | verified locally |
| Linux x86_64 (GitHub Actions) | workflow/configured in repo history | workflow/configured in repo history | workflow/configured in repo history | not independently verified here |
| Windows MSVC | not run | not run | N/A | not verified |

## Remaining limitations
- Only selected mem/str safeclib APIs integrated; many ext/wchar/io/os/misc APIs unintegrated.
- Intel safestringlib still vendored for provenance/reference; remove only after full parity and tests justify removal.
- Compiler-side ownership/borrow/lifetime/provenance safety requires Agent 1; libc alone is not Rust-equivalent.
- Upstream safeclib full test suite and cross-platform CI evidence incomplete.
