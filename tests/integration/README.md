# End-to-end integration suite

This suite proves that the **driver**, the **compiler**, the **hardened libc**
and the **checked SafeCRT library** work together in one program. Everything
lives under `tests/integration/`; the runner only writes build artifacts into
`build/integration/`.

## Exact commands

From the repository root:

```sh
sh tests/integration/run_integration.sh
```

Equivalent manual steps (what the runner does):

```sh
export PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin
export CPLUS_CLANG=/usr/bin/clang

sh scripts/build_safecrt.sh          # build/safe/libcplus_safecrt.a
sh tests/libc/build_libc.sh          # build/libc/libcplus_hardened.a

./build/c+ build --release tests/integration/app.cp \
  -I. -Istdlib/safe -Ilibc/install-include \
  -o build/integration/app -- \
  build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a -lm -pthread

./build/integration/app build/integration/app_io.bin   # expect exit 0 + "integration: ok"

./build/c+ check tests/integration/contract_probe.c    # expect nonzero + expected_diag.txt substring
```

The PATH prefix is intentional: the default Homebrew `clang` on this machine
has a broken sysroot, so the suite forces the SDK-aware `/usr/bin/clang` for
both library builds and for the driver's C backend.

## What is proven

- **Driver lowering and --release link**: `app.cp` is fed to the real
  `./build/c+ build --release` driver and linked against both
  `libcplus_safecrt.a` and `libcplus_hardened.a`. This is not a direct
  `clang` compile.
- **Compiler diagnostic**: `c+ check` rejects `contract_probe.c`, whose
  `values[9]` against `int values[4]` is provable at compile time. The runner
  asserts a nonzero exit and the diagnostic substring recorded in
  `expected_diag.txt`. That substring was copied from the actual current
  `c+ check` output, not guessed.
- **Hardened libc runtime**: ordinary `malloc/calloc/realloc/free`,
  `memset/memcpy/memmove/memcmp`, `strcpy/strnlen`, `snprintf/printf`, and
  `fopen/fwrite/fread/fclose` all execute through the linked runtime
  (`safe_c.h` maps the ordinary spellings to the checked wrappers). A file is
  written and read back and compared byte-for-byte.
- **Checked SafeCRT library**: explicit `malloc_s`, `memcpy_s`, `fopen_s`,
  `free_checked_s` and `snprintf_s` calls are exercised. The suite asserts:
  - metadata/live-count consistency (allocation table returns to baseline),
  - a refused over-capacity ordinary `memset` (destination unchanged, `ERANGE`),
  - a refused checked `memcpy_s` (`ESNOSPC 406`; Annex K zeroes only `dmax`
    bytes and copies nothing),
  - a refused double free via `free_checked_s`,
  - rejection of a `%n` format in checked `snprintf_s`.
- **Logging**: the whole scenario battery runs twice in one process, with
  SafeCRT logging `OFF` and then `TRACE`. Both result snapshots must be
  byte-identical (`memcmp`), and TRACE must emit `[SAFECRT]` metadata lines;
  refusals emit `[CPLUS-SAFE]` diagnostics. Logging is observable but does not
  change outcomes.

The program prints one outcome line per scenario and finishes with
`integration: ok` and exit status 0. The runner prints
`integration suite: <P> passed, <F> failed` and exits nonzero if any check
fails.

## What is NOT proven

- **Linux CI**: this was run locally on macOS with `/usr/bin/clang`. No Linux
  runner, container, or cross-platform CI job is exercised here.
- **SonarCloud**: no static-analysis service upload or gate is part of this
  suite.
- **Live game**: no game, engine, injected DLL, Wine prefix, or retail
  executable is involved. Nothing here demonstrates in-game behaviour.
- **General memory safety**: the refusals are runtime constraint checks for
  the exercised APIs, not a proof of provenance, lifetime, alias or UAF safety
  for arbitrary C code. There is no sanitizer/ASan or fuzzing pass in this
  suite.
