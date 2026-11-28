# Modern Nuklear / gfx GUI examples — aggregate results

Recorded from a **real run** of `sh apps/legacy_examples/modern_gui/run_all.sh` from the
repository root on the toolchain below. This is a snapshot, not a promise: any
group that had not landed at snapshot time would be marked **PENDING** with its
exact reproduction command. Nothing in this file is fabricated or inferred from
a build alone.

## Environment / toolchain

| Item | Value |
| --- | --- |
| Snapshot time (UTC) | `2026-11-23T06:37:10Z` |
| Host | Darwin arm64, macOS 27.2.1 |
| SDL3 | `3.4.18` (`/opt/homebrew/lib/libSDL3.dylib`, flags via `pkg-config sdl3`) |
| Nuklear | vendored `third_party/nuklear/` + `build/ext/libnuklear.a` (or a locally built equivalent inside each group's `bin/`) |
| Backend clang (pinned) | `Apple clang version 16.0.0 (clang-1600.0.26.6)`, `Target: arm64-apple-darwin27.2.0`, `/usr/bin/clang` |
| Default `clang` on the caller's PATH (excluded) | Homebrew `clang version 23.1.6`, targets `arm64-apple-darwin27.2.0`; this host lacks `MacOSX27.sdk`, so direct builds with it fail with `fatal error: 'stdlib.h' file not found`. The aggregate pins `CPLUS_CLANG=/usr/bin/clang` to avoid that. |
| C+ driver | `C+ 0.1.0` / `Language: C+ (C with memory safety, deterministic RAII, lightweight classes)` |
| Rust (context only; not used here) | `rustc 1.100.0 (0ef1bc386 2026-11-09)` |
| Runner | `/usr/bin/timeout` with `-k 10 300` |
| Child PATH | `/usr/bin:/bin:/usr/sbin:/sbin:/Users/haseeb-mir/.local/bin:/Users/haseeb-mir/.cargo/bin:/opt/homebrew/bin` — only tool-providing directories added back; `/opt/homebrew/bin` appears because it provides `pkg-config` for SDL3, while the compiler itself is pinned explicitly |
| `CPLUS_CLANG` | `/usr/bin/clang` |
| `SDL_VIDEODRIVER` | `dummy` (forced by the aggregate for every group) |

Verification performed before the run: `sh -n apps/legacy_examples/modern_gui/run_all.sh`
→ `sh -n: OK`. The aggregate runner uses no Python and no success-override
(`|| true`) masking, and continues after failures.

## Exact final table (verbatim stdout)

```
modern-gui aggregate: timeout=300s (kill after 10s), PATH=/usr/bin:/bin:/usr/sbin:/sbin:/Users/haseeb-mir/.local/bin:/Users/haseeb-mir/.cargo/bin:/opt/homebrew/bin, CPLUS_CLANG=/usr/bin/clang, SDL_VIDEODRIVER=dummy

RUNNER                 TOKEN            EXIT  STATUS       FINAL LINE (verbatim)
------                 -----            ----  ------       ------------------
dashboard              GUI_DASHBOARD    0     PASS         GUI_DASHBOARD 17 6 0 11
games/breakout_ui      GFX_BREAKOUT     0     PASS         GFX_BREAKOUT 8 8 0 3
games/invaders_ui      GFX_INVADERS     0     PASS         GFX_INVADERS 9 4 0 5
settings               GUI_SETTINGS     0     PASS         GUI_SETTINGS 6 6 0 5

gui-agg: runners=4 reported=4 pass=4 blocked=0 fail=0 hard_fail=0
gui-agg: expected tokens: GUI_DASHBOARD GUI_SETTINGS GFX_BREAKOUT GFX_INVADERS
gui-agg: logs: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/modern_gui/<runner>.log ; final lines: /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib/build/modern_gui/final_lines.txt
```

Exact final report lines captured verbatim (also saved in
`build/modern_gui/final_lines.txt`):

```
GUI_DASHBOARD 17 6 0 11
GFX_BREAKOUT 8 8 0 3
GFX_INVADERS 9 4 0 5
GUI_SETTINGS 6 6 0 5
```

Exact stderr from the same run: **(empty)** — no `BLOCKED`, `PENDING`, or
failure diagnostics were emitted.

## Totals and interpretation

| Metric | Value |
| --- | --- |
| Runners discovered | 4 |
| Runners that emitted a final line | 4 |
| PASS | 4 |
| BLOCKED (reported, not a hard failure) | 0 |
| Real FAIL / FAIL_EXIT / NO_REPORT / BAD_TOKEN | 0 |
| Expected tokens | 4 |
| Tokens reported | 4 of 4 |
| PENDING tokens | 0 |
| `hard_fail` | 0 |
| Aggregate process exit | **0** |

The final-line positional contract is
`TOKEN <scenarios> <passed> <failed> <refused>`. The third field (`failed`) is
zero for every group. Each group owns the meaning of its `passed`/`refused`
split: for `GUI_DASHBOARD` and `GUI_SETTINGS` the refused cases are a subset of
the passing expectations, while for `GFX_BREAKOUT` and `GFX_INVADERS` they are
disjoint and sum to the scenario total. All are expected outcomes, not
failures. The aggregate classifies a row as a failure only on a non-zero exit,
a missing/unrecognised final line, or a nonzero `failed`/`fail=` signal.

Per-group transcripts and build logs are kept by each group under its own
directory; the aggregate records only the verbatim final line and exit status.

## Reproduction commands

From the repository root:

```sh
# Syntax check first
sh -n apps/legacy_examples/modern_gui/run_all.sh

# Full aggregate run (the exact command recorded above)
sh apps/legacy_examples/modern_gui/run_all.sh

# Individual groups, if a single row needs re-checking
sh apps/legacy_examples/modern_gui/dashboard/run.sh
sh apps/legacy_examples/modern_gui/settings/run.sh
sh apps/legacy_examples/modern_gui/games/breakout_ui/run.sh
sh apps/legacy_examples/modern_gui/games/invaders_ui/run.sh

# Raw per-runner logs and captured final lines
cat build/modern_gui/final_lines.txt
```

No group was PENDING at snapshot time, so there are no deferred exact commands.
If a group is later removed or its `run.sh` stops landing, the aggregate will
print a `PENDING` row with `PENDING - no runner reported <TOKEN>` and exit 1;
re-run the corresponding `sh apps/legacy_examples/modern_gui/<group>/run.sh` after it lands.

## Non-claims

- No Rust equivalence is claimed. Rust is not used by these apps; its version
  is recorded only as context for the repository's comparison campaigns.
- No claim of 100% safety, GUI correctness, or production readiness is made.
  These are bounded deterministic teaching models with fixed seeds, no
  interactive event loop, no wall clock and no network. The value is the
  reproducible refusal transcript and the observed headless frame.
- Results apply to **macOS arm64 / SDL3 3.4.18 only** (Apple clang 16.0.0,
  `/usr/bin/clang`). No other platform, toolchain, SDL3 version, video driver
  or renderer backend is claimed to behave identically. All frames were
  produced against `SDL_VIDEODRIVER=dummy`; a clean build or headless run is
  not live-display success.
- See [README.md](README.md) for the catalogue of apps, exact build commands,
  APIs used and refusal cases demonstrated.
