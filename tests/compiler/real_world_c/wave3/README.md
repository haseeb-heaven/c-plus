# Wave-3 real-world C compatibility harness

Permanent regression harness owned by the wave-3 blue-defender workstream.

`run_suite.py` pins **12 standalone MIT-licensed C programs from 6 distinct
upstream repositories** (`manifest.json` records exact URL, commit, LICENSE,
SHA-256, stdin/args and expected stdout/exit for every source), then builds each
one with the current `c+` in every **mode × standard** cell:

- modes: `--legacy-c`, `--default`, `--strict` (always `--sanitize=none`),
- standards: `c89`, `c99`, `c11`, `c17`, `c2x` (the only C23-draft dialect the
  host reference `/usr/bin/clang` 16 accepts; it rejects `-std=c23`).

For every cell it:

1. downloads the pinned source and verifies SHA-256;
2. builds the same source with `/usr/bin/clang -std=<std> -O2`;
3. builds with `c+ build <mode> --sanitize=none -std=<std> --emit-c ...`;
4. checks `--emit-c` preservation — **exact** (one leading `#line`, then
   byte-identical original) in `--legacy-c`; **structural** (one `#line 1`
   with the source path) in the safety modes, where the rewriter legitimately
   injects a prelude and may rewrite operators;
5. runs both binaries with identical stdin/args/env/cwd and hard timeouts;
6. compares exact stdout bytes and exit status against both the manifest and
   clang.

No `|| true`, no skips, no suppressed failures. Hard failures and documented
mode rejections are reported separately with exact rc/bytes/stderr.

## Run

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_world_c/wave3/run_suite.py
```

Optional env: `REF_CC`, `RW3_DOWNLOAD=0` (cached sources), `RW3_DL_TIMEOUT`,
`RW3_BUILD_TIMEOUT`, `RW3_RUN_TIMEOUT`, `RW3_WORKERS`.

Outputs (all under this directory): `cache/`, `results/logs/`,
`results/bin/`, `results/generated/`, `results/summary.txt`,
`results/summary.json`. Exit status is 1 if any hard failure occurred.

Measured results and limitations: `docs/compiler/real-world-wave3.md`.
