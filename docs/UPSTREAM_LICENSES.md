# Upstream Licenses & Provenance

Full integration audit and security/ownership evaluation live in [`docs/libc/SAFER_UPSTREAM_INTEGRATION.md`](libc/SAFER_UPSTREAM_INTEGRATION.md).

## 1. rurban/safeclib (primary linked checked CRT backend)
- Upstream: https://github.com/rurban/safeclib
- Commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT (Cisco 2012-2013, Reini Urban 2017; `third_party/safeclib/COPYING` preserved)
- Local path: `third_party/safeclib/`
- Linked via `scripts/build_safecrt.sh` (`-DCPLUS_SAFECLIB_BUILD`, `cplus_sc_*` wrappers). Local fixes recorded in `third_party/safeclib/PROVENANCE.md` (`src/str/strtok_s.c`, `src/cplus_sc_wrappers.c`).

## 2. intel/safestringlib (vendored reference only; NOT linked)
- Upstream: https://github.com/intel/safestringlib
- Commit: `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`
- License: MIT (`third_party/intel-safestringlib/LICENSE` preserved)
- Local path: `third_party/intel-safestringlib/`
- Status: Excluded from `scripts/build_safecrt.sh` so two competing backends never link simultaneously for the same API.

## 3. musl libc (selective MIT primitives)
- Upstream: https://git.musl-libc.org/cgit/musl
- Commit: `7813e9119eb5fe681b6750b8a033a0490bca0941`
- License: MIT (`libc/upstream/musl/PROVENANCE.md`)
- Scope: Selected memory/string/numeric primitives in `libc/src/` (`musl_extra.c`, `musl_stdlib_extra.c`).

## 4. rurban/ctl (vendored header-only C containers)
- Upstream: https://github.com/rurban/ctl
- Commit: `ede5f455392f2380dd16aead26835eece6cd20e4`
- License: MIT (`third_party/ctl/LICENSE` preserved)
- Local path: `third_party/ctl/` (`ctl/vector.h` hardened for empty `pop_back`, overflow in `fit`, and `realloc` failure preservation).

## 5. exceptions4c, yyjson, stb, miniaudio, Nuklear
- `exceptions4c`: MIT (`third_party/exceptions4c/PROVENANCE.md`) — C99 `(void)` prototype fix in `exceptions4c.h`.
- `yyjson` (MIT), `stb` (MIT/PD), `miniaudio` (MIT-0/PD), `Nuklear` (MIT/PD): original third-party headers/licenses preserved.
- Platform CRTs (Apple libc, glibc, UCRT, musl): linked via system toolchain only; never copied or globally replaced.
