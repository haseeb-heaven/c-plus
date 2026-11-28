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
contracts and tests. This branch also closes the enumerated ISO C17 gaps with
202 additional PLATFORM_BACKED `cplus_*` wrappers covering `<fenv.h>`, `<time.h>`,
`<wchar.h>`, `<wctype.h>`, `<complex.h>`, math `*f`/`*l`, `<stdlib.h>` exits and
`<inttypes.h>` wide conversions. Every one delegates to a named host C runtime
function; none is counted as a bundled or upstream implementation.

- `docs/libc/ISO_C_AUDIT.md` — honest header-by-header classification.
- `docs/libc/API_TEST_MATRIX.md` — API → source → test → host backend → contract mapping.
- `libc/contracts/c17_contracts.json` — machine-readable companion to the matrix.

Remaining ISO C families and limitations are tracked in `docs/libc/COVERAGE.md`.
No false completeness claim: a wrapper never makes a raw C pointer safe, and
host delegation is not the same as a bundled implementation.

- `install-include/cplus_libc_safe.h` makes unchanged `malloc`/`memset`/
  `memcpy`/`strcpy`/`fread`/`printf` calls capacity-checked using only this
  static library (no SafeCRT dependency), with an explicit strict mode for
  unknown capacity. See `docs/libc/ORDINARY_C_SAFETY.md`.

