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
| `sh apps/games3d/run.sh` (real app, not a demo) | `SCENE3D 1 4 0 13` |
| `sh apps/gui_app/run.sh` (real app, not a demo) | `GUI_APP 30 6 0 24` (this runner's Windows `-v` capture is conservative/BLOCKED) |
| `sh apps/cli_app/run.sh` (real app, not a demo) | `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok` |
| `sh tests/run_modern_safer_evidence.sh` | one-command aggregate for the above: `GAMEAPP_EVIDENCE 9 9 0` |

The three suites above were moved from `examples/{games3d,gui_app,cli_app}` into
`apps/`; the old `examples/.../run.sh` entries are compatibility shims only.
Their screenshots, rendered from actual runner stdout:
[games3d.svg](../../proof/all_apps_games_screenshots/screenshots/games3d.svg),
[gui_app.svg](../../proof/all_apps_games_screenshots/screenshots/gui_app.svg),
[cli_app.svg](../../proof/all_apps_games_screenshots/screenshots/cli_app.svg).
The separate cross-platform pack
[`proof/real_apps_cross_platform/`](../../proof/real_apps_cross_platform/README.md)
captures genuine PE32+ runs under Highball/Wine (`CROSS_PROOF 58 58 0`); that
is host/engine-specific evidence and does not change the app runners' own
Windows scope.

Fixed during this verification: `apps/cli_app/cli_calc.cp:55` now seeds the
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

## 4. Goose-style heap-free path in the modern libs (2026-11-25)

- `stdlib/web.hp` gained `web_server_init()` / `web_server_shutdown()` so
  `apps/webserver/app.cp` keeps its server handle on the **stack**: no heap
  allocation in that app. `web_server_start()` remains the additive malloc
  convenience wrapper; app output is unchanged (`APPS 3 3 0`).
- `stdlib/sqldb.hp` and `stdlib/gameeng.hp` use fixed slots (no per-object
  heap); `benchmarks/goose_modern/bench.cp` runs 20,000 of their operations
  inside one `stdlib_scratch_scope` using VM data-stack bump payloads.
- Measured on this host: allocation-only bump **64 ns/op** vs checked-heap
  **175 ns/op** (~2.7x); full modern workload (sqldb+game+bump) **591 ns/op**.
  Deterministic gate: `GOOSE_MODERN_BENCH 3 3 0` (op count + OK marker only; the ratio is host/load-dependent evidence).
- No Goose code copied; Goose's 2 GiB reservation/compile-time root inference
  is not replicated. "No heap" here means by construction in these apps and
  headers, not a runtime proof that the checked allocator is never called.
