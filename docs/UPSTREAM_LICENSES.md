# Upstream licenses / provenance (Agent 2 / PR #17)

## Intel safestringlib (vendored subset)

- Upstream: https://github.com/intel/safestringlib
- Commit: `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`
- License: MIT (Intel + Cisco notices preserved in `third_party/intel-safestringlib/LICENSE`)
- Local path: `third_party/intel-safestringlib/`
- Files copied: see `third_party/intel-safestringlib/PROVENANCE.md`
- Adaptations: `rename_symbols.h` build-time symbol renaming to avoid CRT/Annex K symbol collisions; `CMakeLists.txt` isolated static lib; small `safeclib_private.h` compile fix for clang C99 (`#include <stdlib.h>` for abort). All adaptations recorded in PROVENANCE.

## rurban/safeclib (reference only, not linked)

- Upstream: https://github.com/rurban/safeclib
- Commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT (upstream)
- Local path: `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-library-upstreams/rurban-safeclib` (not vendored into repo, not linked)

## Platform CRTs (not vendored)

- Apple libc / Microsoft UCRT / glibc / musl: used through installed system headers/libs only. No proprietary CRT source copied.

## Existing vendored deps retained

- yyjson MIT, stb public domain/MIT, miniaudio public domain/MIT-0 style (see upstream LICENSE), Nuklear MIT/public domain dual per upstream file; see existing third_party dirs.

## rurban/ctl (vendored, header-only typed C containers)

- Upstream: https://github.com/rurban/ctl
- Commit: `ede5f455392f2380dd16aead26835eece6cd20e4`
- License: MIT (Gustav Louw, Reini Urban; LICENSE preserved in `third_party/ctl/LICENSE`)
- Local path: `third_party/ctl/` (headers copied verbatim; `PROVENANCE.md`, `UPSTREAM_COMMIT`)
- Use: typed containers (vector, string, deque, list, map, set, ...). Consumed by
  `tests/ctl/` with the upstream POD/`<string.h>` include requirements.
- Constraints: a C library; no memory-safety guarantee beyond its own contract.
  One container type per translation unit; POD element types require `#define POD`.

## rurban/safeclib (vendored as primary checked backend)

- Upstream: https://github.com/rurban/safeclib
- Commit: `90b076e94c4b0fb202dddf948d5d620e5395c1b0`
- License: MIT (Cisco, Reini Urban; COPYING preserved in `third_party/safeclib/COPYING`)
- Local path: `third_party/safeclib/`; PROVENANCE + UPSTREAM_COMMIT present. Selected
  dependency-closed mem/str subset compiled with `-D` symbol prefixing (`cplus_sc_*`).

## musl (vendored reference subset)

- Upstream: https://git.musl-libc.org/cgit/musl
- Commit: `7813e9119eb5fe681b6750b8a033a0490bca0941`
- License: MIT; local path `libc/upstream/musl/` (reference/provenance).
