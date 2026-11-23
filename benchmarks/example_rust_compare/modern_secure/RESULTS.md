# modern_secure — safe-Rust models vs modern_gui / secure_apps examples

Scope: `benchmarks/example_rust_compare/modern_secure/` only. No source files
under `examples/modern_gui/` or `examples/secure_apps/` were edited; no
`git add`/`git commit` was performed by this work.

## Headline

**8 examples, 82 fixed cases, 8 MATCH, 0 DIFFERENT, 0 BLOCKED, 0 PENDING.**
`compare.sh` exit status 0. Every `MODERN_COMPARE` line was produced by an
actual build + bounded run of both sides; no verdict was fabricated.

```
MODERN_COMPARE dashboard   MATCH stdout+exit identical for 20 fixed case(s)
MODERN_COMPARE settings    MATCH stdout+exit identical for 9 fixed case(s)
MODERN_COMPARE breakout_ui MATCH stdout+exit identical for 10 fixed case(s)
MODERN_COMPARE invaders_ui MATCH stdout+exit identical for 9 fixed case(s)
MODERN_COMPARE vault       MATCH stdout+exit identical for 10 fixed case(s)
MODERN_COMPARE firewall    MATCH stdout+exit identical for 12 fixed case(s)
MODERN_COMPARE ratelimit   MATCH stdout+exit identical for 10 fixed case(s)
MODERN_COMPARE auditlog    MATCH stdout+exit identical for 2 fixed case(s)
```

## What was compared

For each C+ example, a **safe Rust** model (`.rs`, no `unsafe`, no FFI) encodes
the deterministic observable core: scenario tables and state machines, the
vault's fixed-capacity credential map and bounded audit records, the ordered
firewall rule checks and CONTRAST evidence, the token-bucket refill/debit
state machine, and the audit event/refusal model. Both sides were built and
run under `timeout 30` with identical argv (empty stdin except two explicit
stdin cases), `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy` for GUI programs,
and the log file removed before every `auditlog` run. stdout bytes and exit
status were compared; stderr is diagnostic only.

| name | C+ source | Rust model | fixed cases | result |
|------|-----------|------------|-------------|--------|
| dashboard | examples/modern_gui/dashboard/dashboard.cp | dashboard.rs | 20 | MATCH |
| settings | examples/modern_gui/settings/settings_panel.cp | settings.rs | 9 | MATCH |
| breakout_ui | examples/modern_gui/games/breakout_ui/breakout_ui.cp | breakout_ui.rs | 10 | MATCH |
| invaders_ui | examples/modern_gui/games/invaders_ui/invaders_ui.cp | invaders_ui.rs | 9 | MATCH |
| vault | examples/secure_apps/vault/vault.cp | vault.rs | 10 | MATCH |
| firewall | examples/secure_apps/firewall/firewall.cp | firewall.rs | 12 | MATCH |
| ratelimit | examples/secure_apps/ratelimit/ratelimit.cp | ratelimit.rs | 10 | MATCH |
| auditlog | examples/secure_apps/auditlog/auditlog.c | auditlog.rs | 2 | MATCH |

Stdout SHA-256 (identical for C+ and Rust in every case):

| name | sha256 |
|------|--------|
| dashboard | 83ce52173628131ea5342acf176aad816fb47b78d3c9469a788b4c15ce5b9bca |
| settings | 6a7fb2ff67ed6568353e720e2f018230add3423017544066ff47168c48dff4e4 |
| breakout_ui | dea8cd8ec4174fbb38a3be440a3991a81d95a61cd999bd24abae67fcb837f988 |
| invaders_ui | dfd2f52c30fb90c0e17140fed409d9abd58940b6624cdce92d90dece1f29d555 |
| vault | 189c12acecba234cde17d1a8292ac5ad3c4bf678e76df0a05f37a5413d0d33b4 |
| firewall | d0a3550ebb2e2bbed37b6c6da3c7d58e2cf46c5dabc29c8b994b8643aa6881fb |
| ratelimit | 362225eee409be8e105da3a116e13e3a72908c62078f5e197c99981a6ae9f302 |
| auditlog | a454fd83700f97c3c6e5dcd1fccf126b10141db1cc6ebbe422c60d30d5da02f0 |

## Build and run

From the repository root:

```sh
sh benchmarks/example_rust_compare/modern_secure/compare.sh
```

The script rebuilds every C+ source with its documented flags, rebuilds each
Rust model with `rustc -O --edition 2021`, runs both under `timeout 30`, and
prints one `MODERN_COMPARE <name> <verdict> <reason>` line per example. Logs,
binaries, per-case stdout/stdout diffs live under
`benchmarks/example_rust_compare/modern_secure/.build/`.

Toolchains observed: `build/c+` (host Clang backend), `rustc 1.100.0`,
`timeout 30`, SDL3 via `pkg-config sdl3`, Nuklear via `build/ext/libnuklear.a`,
and `clang -std=c99 -pthread` with `build/libc/libcplus_hardened.a` for the
audit-log API example.

## Semantics of the verdicts

- `MATCH` — stdout byte-identical and exit status equal for every fixed case.
- `DIFFERENT` — at least one case differed (would make the script exit nonzero).
- `BLOCKED` — C+ or Rust build failed, or a run exceeded 30 s; inconclusive.
- `PENDING` — the C+ source or the Rust model had not appeared yet; the output
  includes the exact refresh command `sh <this-dir>/compare.sh`. No MATCH is
  ever claimed for a missing source or model.

## Explicit non-equivalence

A MATCH means **only** that stdout and exit status agree for the fixed cases
above. The Rust models are deliberately **not equivalent implementations** of
the C+ programs:

- the GUI models never create an SDL3 window, renderer, Nuklear context or
  surface; they emit the transcript the headless C+ build produces under
  `SDL_VIDEODRIVER=dummy`, including its `painted=1` / `hud_nonbg=1` probes;
- the settings model hard-codes the six documented scenario outcomes rather
  than validating live UTF-8 and calling real widget APIs;
- the vault model uses a `Vec`-backed table rather than the C+ fixed-capacity
  byte buffers plus 64-byte canary, and never handles real credentials;
- the firewall model re-implements the ordered rule checks but does not go
  through `stdlib/string.hp` or `stdlib/error.hp`;
- the ratelimit model uses Rust arithmetic rather than C+ checked builtins;
- the auditlog model emits the fixed transcript without opening, appending to
  or reading a log file.

These are **teaching examples and runtime-refusal evidence, not security
proofs and not production code**. In particular, passing a comparison does not
establish GUI correctness, memory safety, absence of undefined behavior in the
C+ original, or that the secure-app models would resist a real adversary.
