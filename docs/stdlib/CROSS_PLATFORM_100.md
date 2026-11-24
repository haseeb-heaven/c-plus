# 100 cross-platform apps (macOS native + Windows PE)

`tests/cross_platform_apps/` generates 100 deterministic apps across 10
real-world domains (game engine, snake game, desktop, web, server, database,
audio, video, graphics, utilities). Each app has a fixed seed, validates
hostile input and exits 0 only with intact state and documented refusals.

- **macOS**: `sh tests/cross_platform_apps/run_macos.sh` →
  `MACOS_APPS built=100 pass=100 fail=0 build_fail=0`.
- **Windows PE**: `sh tests/cross_platform_apps/run_windows_build.sh` →
  `WINDOWS_BUILD compiled=100 verified_pe=100 fail=0` (genuine PE32+ x86-64).
- **Windows runtime (Wine/Highball)**: BLOCKED — Highball 0.10.22 with the
  installed CrossOver wine engine stalls on a trivial hello.exe (>300 s,
  uninterruptible wine process); a bounded 20 s probe returns 124. No Windows
  execution claim is made. `run_windows_runtime.sh` is ready to run all 100
  when the local Wine runtime is usable.

Full evidence: `tests/cross_platform_apps/RESULTS.md`. Runtime evidence on
macOS and PE-artifact verification on Windows; not a cross-platform proof.
