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
