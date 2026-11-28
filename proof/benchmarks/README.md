# Benchmark evidence

Single host (Apple clang 16 / arm64, 2026-11-28). Every binary printed the same
checksum for its workload, so no work was optimized away. C3 and Fil-C are
absent on this host: `NOT_TESTED`.

## One category: 2M-iteration integer mix (same checksum 16776346274400)

| Language | Compile time | Runtime | Time in seconds | Winner/Score |
|---|---:|---:|---:|---|
| C+ `--release --sanitize=none` | 1.364 s | 0.003604 s | 1.802e-9 s/iter | 4th runtime |
| Rust `-O` | 0.796 s | 0.002208084 s | 1.104e-9 s/iter | **fastest runtime** |
| Zig `ReleaseFast` | 6.710 s | 0.00253 s | 1.265e-9 s/iter | 3rd runtime |
| Goose native (`goose build`) | 0.220 s | 0.002486 s | 1.243e-9 s/iter | **fastest compile** |
| C+ `--default --sanitize=none` | 1.017 s | 0.01878 s | 9.390e-9 s/iter | guarded mode |
| C3 | NOT_TESTED | NOT_TESTED | NOT_TESTED | compiler not installed |
| Fil-C | NOT_TESTED | NOT_TESTED | NOT_TESTED | compiler not installed |

All values are seconds: compile time converted from ms, runtime converted from
ns, and `Time in seconds` is ns/iter converted to seconds.

## Long-running complex workload

`benchmarks/complex_workload.c` at scale 100, compiled with `c+ --legacy-c`.
Checksums were identical on repeat: no phase was optimized away.

| Phase | Time (s) |
|---|---:|
| matrix | 100.496 |
| hash | 0.041 |
| sort | 0.006 |
| arena | 1.772 |
| primes | 0.006 |
| bubble | 0.698 |
| **TOTAL** | **103.019** |

## Memory model

| Model | Language | ns/op | Gate |
|---|---|---:|---|
| bump | C+ | 30 | `MEMORY_MODEL_TEST 3 3 0` |
| heap | C+ | 328 | - |
| bump | Goose | 61 | - |
| heap | Goose | 163 | - |

## Raw logs

- Round data: [language_rounds.txt](language_rounds.txt), [zig_rounds.txt](zig_rounds.txt), [goose_rounds.txt](goose_rounds.txt)
- Heavy loop: [heavy/](heavy/)
- Allocation: [results/scoped_bump.out](results/scoped_bump.out), [results/scoped_apps.out](results/scoped_apps.out), [bin/bump_bench](bin/bump_bench)
- Screenshots: [screenshots/](screenshots/)
- Pinned hashes: [SHA256SUMS.txt](SHA256SUMS.txt)
- Sources: [bench.cp](../../benchmarks/language_rounds/bench.cp), [bench.rs](../../benchmarks/language_rounds/bench.rs), [bench.zig](../../benchmarks/language_rounds/bench.zig), [bench.goose](../../benchmarks/language_rounds/bench.goose)

## License and provenance

C+ is inspired by Fil-C, the C3 language and Goose. GooseLang
(<https://github.com/aardappel/goose>, Apache-2.0) supplied the scoped/arena
ideas; TrapC and Rust are safety references, not code sources. No source was
copied unless vendored with its own license. This is not a claim of equivalence
to any of them, nor of 100% memory safety. Details:
[docs/DESIGN_PROVENANCE.md](../../docs/DESIGN_PROVENANCE.md),
[docs/libc/SAFER_UPSTREAM_INTEGRATION.md](../../docs/libc/SAFER_UPSTREAM_INTEGRATION.md),
[docs/security/upstream-techniques.md](../../docs/security/upstream-techniques.md).
