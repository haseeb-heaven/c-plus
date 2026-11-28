# Pinned real C apps/games harness (`real_apps`)

Permanent differential harness for unmodified, externally sourced C
applications and games. It downloads ten programs from a full pinned upstream
commit, verifies their SHA-256, and runs each one under:

- `c+ build --legacy-c --sanitize=none --emit-c ... -- -std=<std>`
- `c+ build --default --sanitize=none -- -std=<std>` (where the source builds)
- `c+ build --strict --sanitize=none -- -std=<std>` (where the source builds)
- `/usr/bin/clang -std=<std> -O2`
- `/opt/homebrew/bin/gcc-14 -std=<std> -O2` (when available)

with `<std>` in `c89`, `c99`, `c11`, `c17`.

## What is checked

1. `curl -fsSL` download from the URL in `manifest.json`; SHA-256 must match
   the pinned value exactly. No fallback source, no vendored upstream code.
2. LEGACY `--emit-c` output: exactly one leading `#line N "path"` directive,
   followed by bytes **identical** to the downloaded source.
3. Exported globals from `nm -g --defined-only`, with one leading Mach-O
   underscore stripped:
   - LEGACY must equal the clang build's symbol set exactly.
   - DEFAULT/STRICT must preserve every *application* symbol; the extra
     documented `cplus_*` runtime exports are recorded, not hidden.
4. Every binary is run with the manifest's exact `args`, `stdin`, environment
   and cwd. Byte-exact stdout and exact exit status must match the manifest
   expectation **and** both reference compilers.
5. Builds, runs and `nm` all use a hard 45-second timeout. There is no
   `|| true`, no suppression and no silent skip. Any unexpected failure makes
   the process exit 1.

## Expected rejections and defects

A DEFAULT/STRICT build rejection is classified by matching its **exact
diagnostic** against documented rules in `docs/SAFE_MODE_SPEC.md`,
`docs/ARITHMETIC_SAFETY.md` and `docs/compiler/compatibility-matrix.md`:

- `SAFE_MODE_RULE` — documented fail-closed behaviour (FFI extent/unknown
  contracts, verified arithmetic rules, RAII across non-local jumps, proven
  errors, etc.).
- `DEFECT` — an undocumented rejection or a Safe-mode runtime behaviour
  difference on a program that the references run correctly.

A standard that no compiler builds (e.g. C99 loop declarations under
`-std=c89`) is recorded as `REFERENCE_REJECTED` with the exact diagnostics of
all involved compilers, plus the manifest's `standard_support` check. It is
not counted as a pass and is never silently skipped.

## Run

From the repository root:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_apps/run_suite.py
```

Optional environment overrides: `REAL_APPS_CLANG`, `REAL_APPS_GCC`,
`REAL_APPS_WORKERS`, `REAL_APPS_DOWNLOAD=0` (reuse cache),
`REAL_APPS_TIMEOUT` (cannot exceed the hard 45 s cap), `REAL_APPS_CURL`.

The harness writes `summary.txt` and `summary.json` here, plus per-step logs,
binaries and generated C under the ignored `results/` directory. Downloads
live in the ignored `cache/` directory. It does not modify any other file,
rebuild `build/`, or commit anything.

## Corpus / licence

Ten cases from one pinned commit of `TheAlgorithms/C`
(`e5dad3fa8def3726ec850ca66a7f51521f8ad393`): two interactive converters, a
merge-sort utility, a Hanoi puzzle solver, a checksum utility, a string search
tool, a root-finding solver, an arbitrary-base converter, a prime sieve and a
Hamming-distance calculator. Several have built-in deterministic self-tests;
the interactive ones use fixed stdin recorded in the manifest.

The pinned tree's `LICENSE` is **GPL-3.0-or-later** (verified at the pin, not
inferred). The source is downloaded for local build/test only; no upstream
source is copied into or redistributed by this repository.
