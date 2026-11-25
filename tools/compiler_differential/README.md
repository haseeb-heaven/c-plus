# Compiler differential harness

Compares the native C+ binary against every available reference C compiler on
the same deterministic C corpus. C+ is always invoked as
`build --legacy-c --sanitize=none`; the reference compilers use `-std=c11 -O0`
plus, on Darwin, `-isysroot $(xcrun --show-sdk-path)` so Homebrew clang does not
inherit its broken default SDK configuration.

## Run

```sh
CPLUS_BIN=$PWD/build/c+ python3 tools/compiler_differential/run_differential.py
```

Configuration is via environment variables:

| variable | default | meaning |
| --- | --- | --- |
| `CPLUS_BIN` | `$PWD/build/c+` | native C+ executable under test |
| `DIFF_BUILD_TIMEOUT` | `120` | seconds per compile |
| `DIFF_RUN_TIMEOUT` | `20` | seconds per execution |
| `DIFF_WORKERS` | `4` | parallel case/compiler pairs |

Exit status: `0` when every comparison passes, `1` when any mismatch exists,
`2` for a configuration error (missing binary, corrupt corpus, no compilers).
No failure is suppressed and no `|| true` is used: every return code, timeout
and output difference is recorded and reported.

## Files

- `corpus.py` — splitmix64-seeded, deterministic fixture definitions.
- `generate_corpus.py` — regenerates `tests/compiler/differential/cases/*.c`
  and `manifest.json` byte-for-byte from the master seed.
- `run_differential.py` — discovery, compilation, execution, comparison and
  reporting.

## Comparison rules

Exit status and **exact** stdout/stderr must match. The only normalization is
replacement of the absolute paths the harness itself injects (work directory,
source, both binaries, `CPLUS_BIN`, repository root, home) with `<PATH>`; no
numeric, whitespace or diagnostic text is altered. Both compile steps must
succeed; a compile failure, timeout or missing executable is a mismatch.

## Results

See `tests/compiler/differential/README.md` for the result layout.
