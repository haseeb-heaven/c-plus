# Canonical repository layout

This is the authoritative map. Legacy folders listed below are compatibility
layers only; new work goes in the canonical locations.

```text
apps/                 real applications and game suites (verified runners)
  webserver/          HTTP app on stdlib/web.hp (heap-free stack server)
  sqldb/              SQL-like DB app on stdlib/sqldb.hp
  gameeng/            headless game engine app on stdlib/gameeng.hp
  games3d/           3D game/scene app suite (moved from apps/legacy_examples/)
  gui_app/           GUI app suite (moved from apps/legacy_examples/)
  cli_app/           CLI app suite (moved from apps/legacy_examples/)
benchmarks/           measured speed evidence (goose_fast, goose_modern)
compiler/ runtime/   C+ compiler and runtime (Agent 1 owned)
libc/ stdlib/        checked libc and C+ standard/safer libraries
  stdlib/goose_mem.hp  single Goose-style memory facade
  stdlib/no_heap.hp    opt-in compile-time heap refusal
proof/               deterministic evidence: binaries, screenshots, transcripts
  all_apps_games_screenshots/  one screenshot + .md per app/game
  games_gui_nuklear/           snake, maze, Nuklear UI + SVG proof
  games_apps/ real_apps_cross_platform/ goose_samples/  other evidence packs
tests/               all verification runners (edge1000, PR31/32/33, libc, CRT)
tools/               generators and build helpers
third_party/         pinned MIT/Apache/zlib upstreams (do not format/edit)
docs/                authoritative docs; CORE_PHILOSOPHY.md is normative
```

## Compatibility (temporary)

- `apps/legacy_examples/{games3d,gui_app,cli_app}/run.sh` are 3-line shims forwarding to
  `apps/...`; they exist only so older CI/docs references keep working.
- `apps/legacy_demos/` is the legacy compiler-demo suite, still referenced by compiler CI.
- `scratch/` is ignored working storage (bug-catalog fixtures were removed from
  tracking; local copies may remain).

## Rules

- New app/game code → `apps/`. New library code → `stdlib/` or `libc/`.
- Every app/game has a runner + golden output + screenshot `.md` under
  `proof/all_apps_games_screenshots/`.
- Generated binaries/logs are never committed outside `proof/*/bin`.
- No new top-level folder without updating this document.
