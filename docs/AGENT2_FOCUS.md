# Agent 2 focus — apps/GAME_SUITE frozen; stdlib/libc hardening only

Status: **active directive** (recorded 2026-11-28). This is the authoritative
statement of scope for Agent 2 work in this repository. Where an older report
suggests adding an application, game, web server or GUI demo, this document
supersedes it. The index is [docs/README.md](README.md).

## Freeze

The following are **frozen** and are not a destination for new work:

- `apps/` — every application and demo there (web server, SQL-like database,
  headless game engine, 3D scene, GUI, calculator CLI, and the legacy
  examples/demos shims). See [apps/README.md](../apps/README.md).
- `GAME_SUITE` — the game/UI evidence suite reported by
  `sh proof/games_gui_nuklear/run.sh` (`GAME_SUITE 4 4 0`) and the related
  game/app proof folders indexed in [proof/README.md](../proof/README.md).

Consequences of the freeze:

- **No new app, game, web-server or GUI demo** is added — no new sources,
  runners, screenshots or proof folders for that purpose.
- Existing sources, runners, transcripts and proof images are **retained
  unchanged**. They are historical evidence and compatibility baselines; the
  freeze does not delete, rewrite or "modernise" them.
- The recorded verdicts stay as-is and are not targets to extend:
  `APPS 3 3 0`, `SCENE3D 1 4 0 13`, `GUI_APP 30 6 0 24`,
  `CLI_APP_RESULT built=1 pass=17 fail=0 total=17 wincross=ok`,
  `GAME_SUITE 4 4 0`, `PROOF_GAMES 10 0`, `CROSS_PROOF 58 58 0`,
  `SCREENSHOTS 24 24 0`.
- `tools/verify.sh` keeps invoking the existing runners unchanged. This
  directive adds documentation only; it does not remove, skip or replace a
  verification gate.

## Where Agent 2 cycles go instead

Agent 2 (the stdlib/libc/runtime owner) spends its cycles on **stdlib and libc
hardening**:

- closing known contract gaps and placeholder `_s` contracts —
  [libc/CONTRACT_STATUS.md](libc/CONTRACT_STATUS.md),
  [known_gaps.md](../tests/stdlib/agent2_audit/known_gaps.md);
- bounds, lifetime, ownership and allocation fixes in the checked layer —
  [libc/AGENT2_CURRENT_AUDIT.md](libc/AGENT2_CURRENT_AUDIT.md),
  [libc/COVERAGE.md](libc/COVERAGE.md);
- deep-test and adversarial/fuzz coverage of the public API —
  [stdlib/REGRESSION_INVENTORY.md](stdlib/REGRESSION_INVENTORY.md),
  [baseline.md](../tests/stdlib/agent2_audit/baseline.md);
- honest, evidence-backed status reporting —
  [stdlib/AGENT2_FINAL_REPORT.md](stdlib/AGENT2_FINAL_REPORT.md),
  [AGENT2_FINAL_STATUS_2026_11_25.md](AGENT2_FINAL_STATUS_2026_11_25.md).

Verification work belongs in the stdlib/libc suites
(`tests/stdlib/agent2/`, `tests/stdlib/agent2_audit/`, `tests/core/`,
`tests/crt/`, the deep and fuzz runners), not in the frozen app/game suites.

## Non-goals

- Building a new demo, game, GUI or web-server to "show off" a library change.
- Adding new proof folders or screenshots for app/game work.
- Editing, deleting or regenerating frozen sources, runners or proof images.
- Claiming a new app/game verdict as evidence of a stdlib/libc fix.

A library change is evidenced with the stdlib/libc suites and their own
transcripts. Existing app/game evidence is retained only as a historical
regression baseline, not as a place to spend Agent 2 cycles.
