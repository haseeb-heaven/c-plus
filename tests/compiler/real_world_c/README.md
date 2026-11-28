# Real-world C harness (`real_world_c`)

Downloads unmodified, externally sourced single-file C programs from pinned
URLs and checks that `c+ --legacy-c --sanitize=none` is behaviourally
equivalent to `/usr/bin/clang` on the same source.

For every case in `manifest.json` the harness:

1. Downloads the source from the pinned `https://raw.githubusercontent.com/...`
   URL with `curl -fsSL` and verifies its SHA-256 against the manifest. A
   download or hash failure is a hard failure.
2. Builds with
   `c+ build --legacy-c --sanitize=none --emit-c <generated.c> <src> -o <bin>`
   and builds the same source with
   `/usr/bin/clang -std=gnu89 -O2 <src> -o <ref_bin>`.
3. Emits C: requires exactly one leading `#line N "path"` directive and requires
   the remaining bytes to equal the downloaded original **exactly**.
4. Runs both binaries with identical `args`, `stdin` bytes, environment and
   cwd, with a hard timeout. Compares exact stdout (bytes) and exact exit
   status. Both must also match the manifest's recorded expectation.
5. Writes `results/summary.txt`, `results/summary.json` and per-step logs under
   `results/logs/`.

There is **no `|| true`, no suppression and no skip-on-failure**. Every
download, build, source-preservation, run, stdout and exit mismatch is reported
and makes the harness exit nonzero.

## Run

From the repository root:

```sh
CPLUS_BIN=$PWD/build/c+ python3 tests/compiler/real_world_c/run_suite.py
```

Overrides (all optional): `CPLUS_BIN` (default `<repo>/build/c+`),
`REF_CC` (default `/usr/bin/clang`), `RW_DOWNLOAD` (`1`/`0`), `RW_TIMEOUT`
(per-subprocess seconds), `RW_WORKERS`.

The harness writes only under this directory. It does not build the compiler,
modify other sources or commit anything. Downloaded sources are cached under
`cache/`.

## Corpus

All six cases come from one pinned upstream commit of `TheAlgorithms/C`
(`e5dad3fa8def3726ec850ca66a7f51521f8ad393`). The programs are deterministic:
they use fixed input or built-in self-tests and contain no clock, address,
random or path output. `gcd`, `catalan` and `factorial` read from stdin;
the three hash/cipher programs are self-testing. All emit stable stdout.

This is a behavioural comparison, not a memory-safety proof. `--legacy-c`
disables C+ safety checks by design.
