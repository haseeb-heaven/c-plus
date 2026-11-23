# Full-coverage fixture suite (permanent)

Executes the previously BLOCKED / never-executed safety fixtures and records
evidence. This directory is self-contained; no other tree is modified.

## Run

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/full_coverage/run_full_coverage.py
```

Optional environment: `CPLUS_CLANG` (default
`/opt/homebrew/opt/llvm/bin/clang`). The SDK comes from
`xcrun --show-sdk-path`.

## Discovery

Fixtures are discovered automatically (no hard-coded names) from:

- `tests/safety/*.c` / `*.cp`
- `tests/safety/p0new/`, `sanitizer/`, `runtime_crashes/`,
  `rust_compare/c/`, `fnptr_bounds/`, `integration/`
- `tests/known_gaps/` including subdirectories that contain fixtures;
  subdirectories with only a README are excluded.

Both the literal and `tests/safety/`-relative directory spellings are probed.

## Execution and expectations

Every fixture runs DEFAULT check, STRICT check and STRICT build+run with a
hard 30 s timeout. The `sanitizer/` and `runtime_crashes/` corpora also get
ASan and UBSan builds/runs with `CPLUS_CLANG` and `-- -isysroot <sdk>`.

Expectations are derived from adjacent `.err` / `.stderr` / `.exit` files,
filename conventions (`*_ok`, `*_reject`, `safe_*`, `trap`/`crash`) and embedded
`// EXPECT:` / `// DIAG:` markers. `tests/known_gaps/` README declares its
`.err` files as known-absent diagnostics, so those are recorded as OBSERVED
rather than asserted as live expectations.

With no expectation, the actual outcome is always recorded as **OBSERVED** —
never skipped and never silently passed.

## Classification

- **PASS** — expectation existed and every check held.
- **FAIL** — expectation existed and a check diverged. Only this sets exit 1.
- **OBSERVED** — no live expectation; full outcomes recorded explicitly.
- **TOOLCHAIN_UNAVAILABLE** — clang/SDK/c+ missing or a toolchain error
  signature appeared; recorded distinctly from source failures.

Outputs: `summary.txt`, `summary.json`, and capped exact-prefix evidence logs
in `logs/` (binaries in `out/`).
