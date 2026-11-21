# C+ hardened libc (selectively linked)

This directory contains a **selectively linked** bundled C standard-library layer
for the C+ project. It is **not** a full drop-in replacement for the host system
CRT. Standard C headers/functions keep familiar names and signatures; checked
`*_s` APIs are optional additions; C+ `malloc_s`-family extensions are documented
separately and are not claimed to be Microsoft/Apple CRT APIs.

## Layout

- `include/` - installable standard C headers (compatible signatures; partial coverage)
- `src/` - bundled implementations (real code, not wrappers-only)
- `checked/` - optional Annex-K-style / checked additions and adapters
- `platform/` - platform probes / select wrappers
- `contracts/` - machine-readable contract metadata for compiler coordination
- `log/` - opt-in reentrancy-safe logging hooks
- `cmake/` - install/export files
- `upstream/` - vendored upstream sources with attribution

## Status

Implemented first: allocation, memory, string, errno basics, minimal stdio/file
contracts and tests. Remaining ISO C families are tracked in `docs/libc/COVERAGE.md`.
No false completeness claim.
