# Proof — games and apps created in this campaign

This folder contains **runnable binaries**, their **exact captured output**, and
the commands used to build them. It is evidence for the three applications added
to this repository during the modern-stdlib work.

> Evidence, not a universal safety proof. Results are for the recorded host,
> toolchain and inputs only. No Windows runtime, live-game or remote-CI claim.

## Contents

| Path | What it is |
| --- | --- |
| `binaries/scene3d_demo` | 3D game demo — mesh + scene graph + quaternion transforms |
| `binaries/gui_app` | GUI app — widget layout, hit-test, serialization + headless SDL3 frame |
| `binaries/cli_calc` | CLI app — command registry, bounded argv parsing, dispatch |
| `binaries/SHA256SUMS.txt` | SHA-256 of every binary |
| `transcripts/*.build.log` | Full build output for each binary |
| `transcripts/*.run.txt` | Exact stdout/stderr and exit codes from running each binary |
| `run.sh` | Reproducible verifier: checks hashes, re-runs binaries, checks markers |
| `games3d.md` | Per-app proof: source, libraries, exact output, refusals, reproduce |
| `gui_app.md` | Per-app proof: source, libraries, exact output, refusals, reproduce |
| `cli_app.md` | Per-app proof: source, libraries, exact output, refusals, reproduce |

## Host and toolchain

| Item | Value |
| --- | --- |
| OS / arch | Darwin / arm64 (`uname -s` / `uname -m`) |
| Compiler | Apple clang 16.0.0 (`clang-1600.0.26.6`) |
| C+ driver | `./build/c+`, C+ 0.1.0, host Clang/LLVM backend |
| Linked archive | `build/libc/libcplus_hardened.a` |
| GUI dependency | SDL3 3.4.18 via `pkg-config`; `SDL_VIDEODRIVER=dummy` for headless runs |
| Timeouts | `timeout 30` on every run |

## Exact results

```
scene3d_demo  SCENE_DONE status=OK nodes=7 meshes=3         EXIT=0
gui_app       GUI_RESULT describe:0,0,0 OK                  EXIT=0
gui_app       GUI_RESULT render:0,0,0 REFUSED 108           EXIT=0 (defined refusal)
cli_calc      CLI add 2 3 5 / CLI mul 6 7 42                EXIT=0
```

Full transcripts are in `transcripts/`. The 3D demo's transcript includes
`CHECKSUM fnv1a32=b61bdf2b bytes=1779 lines=40`, matched byte-for-byte against
`examples/games3d/expected/transcript.expected`.

## Reproduce

```sh
sh proof/games_apps/run.sh
```

The verifier recomputes SHA-256 for all three binaries and re-runs each one,
checking for its exact marker line and expected exit code. To rebuild from
source, use the exact commands in `transcripts/*.build.log` (or the owning
`examples/<app>/run.sh`).

## Honest scope

- These are **macOS arm64 binaries**. They run here and the output is captured
  verbatim. No claim is made for Windows, Linux or other macOS versions.
- Windows PE compile-only evidence for these apps lives in
  `tests/modern_libs_cross/RESULTS.md` (`MODCROSS pe 8 8 0`); Windows **runtime**
  is BLOCKED in this environment (Highball/Wine stalls).
- SDL3 runs in a dummy/hidden driver for deterministic output — this is a
  headless GUI logic proof, not an interactive rendering benchmark.
- A binary existing and printing a marker is not a proof of memory safety.
  Safety evidence is in `docs/security/` and the red/blue, fuzz and edge suites.
