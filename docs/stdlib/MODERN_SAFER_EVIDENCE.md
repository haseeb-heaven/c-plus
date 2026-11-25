# How much safer and faster are the modern/safer C+ libs — measured evidence (2026-11-25)

All commands below were run on this macOS arm64 host with the restricted
toolchain PATH (`/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin`),
`SDKROOT`/`CPLUS_CLANG` unset, and bounded `timeout`s.

## 1. Every game and app tested end to end

| Runner | Verdict |
|---|---|
| `sh apps/run_all.sh` (web server via curl, SQL-like DB, headless game engine) | `APPS 3 3 0` |
| `sh proof/games_apps/run.sh` | `PROOF_GAMES 10 0` |
| `sh proof/real_apps_cross_platform/run.sh` (8 apps, Mach-O + PE32+) | `CROSS_PROOF 58 58 0` |
| `sh proof/goose_samples/run.sh` (16 modern-C ports, binaries committed) | `GOOSE_PORT 16 16 0` |
| `sh examples/games3d/run.sh` | `SCENE3D 1 4 0 13` |
| `sh examples/gui_app/run.sh` | `GUI_APP 30 6 0 24` (Windows runtime conservative/BLOCKED in `-v`) |
| `sh examples/cli_app/run.sh` | `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok` |
| `sh tests/run_modern_safer_evidence.sh` | one-command aggregate for the above: `GAMEAPP_EVIDENCE 9 9 0` |

Fixed during this verification: `examples/cli_app/cli_calc.cp:55` now seeds the
`strtol` end pointer before dereferencing, which clears the strict
null-pointer diagnostic.

## 2. How much safer (counts are observed checks, not a proof)

| Evidence | Count |
|---|---|
| Aggregate edge/adversarial coverage (`tests/edge1000/run_edge1000.sh`) | `EDGE1000 24791 24790 1` |
| Hardened libc unit suite | `libc tests: 109 passed, 0 failed` |
| Adversarial attack suites (plain + UBSan) | `attack suites: 22 passed, 0 failed` |
| Deterministic libc fuzz | `fuzz_libc: 4800 cases ok` |
| Runtime safety adapters (`run_memory_safety.sh`) | `MEMSAFE 5 5 0`: Fil-C-style 26, SoftBound 51, GSL 80, lifetime 25, Rust-vs-C+ 473 matched / 119 documented differences / 8 both-skip / 0 failures |
| RAII shadowing reproducers (after Agent 1 fix) | `RAII_SHADOW 10 10 0` |
| ABI preservation | `abi_fp: diff is empty`, `stdc_abi: RESULT PASS` |
| All-modern aggregate | `EDGE1000_ABI diff_empty=1` |

The single non-pass in the aggregate is the CLI help `print()` truncation
(`runtime/cplus_runtime.c` copies strings through a 64-byte scratch buffer),
which is Agent 1's file; it was reported on issue #23 and the golden output was
**not** rewritten to hide it. Likewise `ctest` is still RED for 15 compiler
diagnostic cases (Agent 1 scope). None of these results claim Rust/Fil-C
equivalence, memory-safety proof, or release readiness.

## 3. How much faster (measured, host-specific)

`benchmarks/goose_fast/bump_bench.cp`, 200,000 × 32-byte allocations, one run:

```
BENCH bump_ns_per_op 86
BENCH heap_ns_per_op 261
BENCH scope_release_ns 0
BENCH ratio_ge_2 yes
```

That is **~3.0× lower allocation latency** for the VM-reserved data-stack bump
path vs the checked heap on this run. `scope_release_ns 0` means the one-store
scope rewind was below clock resolution. The deterministic gate checks only
bucketed verdicts (`GOOSE_BENCH 4 4 0`); exact timings vary with host load and
are **not** reproducible proof of a fixed speedup, nor Goose equivalence.

## Re-run

```sh
sh tests/run_modern_safer_evidence.sh
sh tests/edge1000/run_edge1000.sh
```
