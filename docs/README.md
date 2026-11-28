# Documentation index

Start here. Everything below is verification evidence for the C+ standard
library layer: a checked, additive C API that keeps the ordinary C ABI.

> **Active scope directive (2026-11-28):** `apps/` and `GAME_SUITE` are **frozen** —
> no new app, game, web-server or GUI demo. Agent 2 cycles go to stdlib/libc
> hardening. See [AGENT2_FOCUS.md](AGENT2_FOCUS.md).

> **Non-negotiable guardrails:** no new features/keywords; no fake or inflated
> benchmarks; small atomic commits under 300 lines; never silence warnings or
> tests; C ABI sacred. Full rules in
> [AGENT2_FOCUS.md § Non-negotiable guardrails](AGENT2_FOCUS.md#non-negotiable-guardrails).

## Current authoritative documents

| Topic | Document |
| --- | --- |
| **Agent 2 scope: apps/GAME_SUITE freeze** | [AGENT2_FOCUS.md](AGENT2_FOCUS.md) |
| **Core memory philosophy (scoped-memory-style)** | [CORE_PHILOSOPHY.md](CORE_PHILOSOPHY.md) |
| **Design provenance (Fil-C / C3 / Goose / scoped)** | [DESIGN_PROVENANCE.md](DESIGN_PROVENANCE.md) |
| Architecture and layout | [ARCHITECTURE.md](ARCHITECTURE.md), [STRUCTURE.md](STRUCTURE.md) |
| Standard library API | [STDLIB.md](STDLIB.md), [MODERN_STDLIB.md](MODERN_STDLIB.md) |
| Checked C layer | [SAFE_CRT.md](SAFE_CRT.md), [LOGGING.md](LOGGING.md) |
| Safer `_s` ABI categories | [SAFE_S_ABI_PRESERVING.md](SAFE_S_ABI_PRESERVING.md), [SAFE_S_NON_ABI_PRESERVING.md](SAFE_S_NON_ABI_PRESERVING.md), [SAFE_S_COMPATIBILITY_MATRIX.md](SAFE_S_COMPATIBILITY_MATRIX.md) |
| Allocators / ownership | [stdlib/allocator/](stdlib/allocator/), [stdlib/MODERN_ALLOCATORS.md](stdlib/MODERN_ALLOCATORS.md) |
| ABI preservation | [ABI_SAFE_LAYER.md](ABI_SAFE_LAYER.md), [abi/](abi/), [abi/ABI_GATE.md](abi/ABI_GATE.md), [abi/STRICT_C_ABI_BOUNDARY.md](abi/STRICT_C_ABI_BOUNDARY.md) |
| Security evidence | [security/](security/) — start with `RED_BLUE_20_FINAL.md` |
| Limitations and honest scope | [LIMITATIONS.md](LIMITATIONS.md), [THREAT_MODEL.md](THREAT_MODEL.md) |
| Modern 3D/GUI/CLI libraries | [stdlib/MODERN_LIBS_3D_GUI_CLI.md](stdlib/MODERN_LIBS_3D_GUI_CLI.md) |
| Modern/safer measured evidence | [stdlib/MODERN_SAFER_EVIDENCE.md](stdlib/MODERN_SAFER_EVIDENCE.md) |
| Edge-case campaigns | [stdlib/EDGE100_FINAL.md](stdlib/EDGE100_FINAL.md) |
| Captured runtime demonstrations | [../apps/legacy_examples/screenshots/README.md](../apps/legacy_examples/screenshots/README.md) |

## Verified apps (real programs, not demos)

**Frozen baseline.** This section records existing evidence only; `apps/` and
`GAME_SUITE` are frozen, so these verdicts are not a prompt to add new apps or
games. See [AGENT2_FOCUS.md](AGENT2_FOCUS.md).

The three formerly under `apps/legacy_examples/{games3d,gui_app,cli_app}` are now real apps
in `apps/`. `apps/legacy_examples/.../run.sh` entries are only compatibility shims. Every
verdict below is the owning runner's exact final line; every screenshot is
rendered from real captured stdout.

| App | Runner | Verified verdict | Screenshot | Proof |
| --- | --- | --- | --- | --- |
| 3D scene app (`games3d`) | `apps/games3d/run.sh` | `SCENE3D 1 4 0 13` | [games3d.svg](../proof/all_apps_games_screenshots/cli_transcripts/games3d.svg) | [games3d.md](../proof/all_apps_games_screenshots/real/games3d_real.png) |
| GUI app (`gui_app`) | `apps/gui_app/run.sh` | `GUI_APP 30 6 0 24` | [gui_app.svg](../proof/all_apps_games_screenshots/cli_transcripts/gui_app.svg) | [gui_app.md](../proof/all_apps_games_screenshots/cli_transcripts/gui_app.svg) |
| Calculator CLI (`cli_app`) | `apps/cli_app/run.sh` | `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok` | [cli_app.svg](../proof/all_apps_games_screenshots/cli_transcripts/cli_app.svg) | [cli_app.md](../proof/all_apps_games_screenshots/README.md) |

The other three apps (`webserver`, `sqldb`, `gameeng`) are verified together by
`sh apps/run_all.sh` → `APPS 3 3 0`. Cross-platform evidence for eight programs
is in [proof/real_apps_cross_platform](../proof/real_apps_cross_platform/README.md)
(`CROSS_PROOF 58 58 0`); the screenshot index is
[proof/all_apps_games_screenshots](../proof/all_apps_games_screenshots/README.md).

## Conventions

- **Evidence, not proof.** A passing finite campaign is evidence for the
  recorded inputs, host and toolchain — not a memory-safety theorem.
- **No false claims.** Windows runtime is claimed only for the recorded
  Highball/Wine pack (`CROSS_PROOF 58 58 0`), not as general portability;
  compile-only scopes stay labelled as such. ASan/UBSan on this host, Rust
  equivalence and live-game success are explicitly marked BLOCKED or
  non-equivalent where applicable.
- `FINAL_*`, `AGENT*_*` and `*_PROOF.md` files are historical campaign records.
  Prefer the consolidated documents linked above; older reports are retained
  only as traceable evidence and are not the current summary.
