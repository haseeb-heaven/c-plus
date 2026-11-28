# Intel safestringlib (vendored subset)

Upstream: https://github.com/intel/safestringlib
Commit: `f4491915cc682556ed64d4eb6a2461d8eed2eb0c`
License: MIT (see `LICENSE`).

This is a **minimal vendored subset** used by C+ optional checked CRT APIs.
Symbols are build-time renamed via `rename_symbols.h` to `cplus_upstream_*`
to avoid collisions with system CRT/Annex K symbols.

See `PROVENANCE.md` for copied files and local adaptations.
