# Windows runtime proof — real_apps_cross_platform

All Windows artifacts here are real PE32+ x86-64 executables run through the
**Highball Wine engine `x64-crossover26.3-r27` in bottle `Games`**. This is an
actual Windows-runtime execution, not an emulator stand-in or a build-only claim.

Binaries live under `<repo-root>/proof/real_apps_cross_platform/binaries/windows/` and
canonical transcripts under `<repo-root>/proof/real_apps_cross_platform/transcripts/windows/`.

## Results

| name | exit | marker final line | note |
| --- | --- | --- | --- |
| `cli.exe` | 0 | `CLI_TEST 27 27 0` | Real Windows runtime via Highball Wine engine `x64-crossover26.3-r27` (bottle `Games`), run with `-v`. |
| `cli_app.exe` (`add 2 3`) | 0 | `CLI add 2 3 5` | Same engine/bottle; args passed directly (no `--` separator). |
| `games3d.exe` | 0 | `SCENE_DONE status=OK nodes=7 meshes=3` | Same engine/bottle, run with `-v`. |
| `gui_app.exe` (no args) | 0 | `GUI_RESULT describe:0,0,0 REFUSED 108` | Same engine/bottle; no-arg default path is `describe` with missing args, so it emits a deterministic refusal marker. |
| `gui_app.exe` (`describe 0`) | 0 | `GUI_RESULT describe:0,0,0 OK` | Same engine/bottle; matches the macOS `gui_app describe 0` marker for a fair cross-platform comparison. |
| `math3d.exe` | 0 | `MATH3D 36 0` | Same engine/bottle, run with `-v`. |
| `mesh3d.exe` | 0 | `drop 1 1` | Same engine/bottle, run with `-v`. |
| `scene3d.exe` | 0 | `SCENE3D 79 0` | Same engine/bottle, run with `-v`. |
| `widgets.exe` | 0 | `WIDGETS 144 0` | Same engine/bottle, run with `-v`. |

8 distinct apps, 9 bounded runs (gui_app exercised twice), all exit 0 with their
app marker lines present. `pass = 9`, `fail = 0`, runtime verified = yes.

## Runner recipe (reproducible, no machine-specific paths)

1. From `<repo-root>`, reset stale bottle/sync state before each run:
   `timeout 20 highball bottle kill Games >/dev/null 2>&1` then `sleep 1`.
2. Run each app with a strict bound and verbose output:
   `PATH=/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin timeout -k 5 90 highball run -v proof/real_apps_cross_platform/binaries/windows/<name>.exe [args]`
   (`cli_app.exe` uses `add 2 3`; `gui_app.exe` is run once with no args and once
   with `describe 0`; the other apps take no args.)
3. Do **not** pass `--` before args — Highball forwards it literally to the app.
4. The engine is selected by bottle `Games`: `x64-crossover26.3-r27`.
5. The two required MinGW runtime DLLs (`libwinpthread-1.dll`,
   `libgcc_s_seh-1.dll`) must sit alongside the `.exe` in
   `<repo-root>/proof/real_apps_cross_platform/binaries/windows/`.

## Honest notes

- **Verbosity requirement:** the `-v` flag was used as the conservative choice and
  produced stable results. Earlier flakiness was traced to **stale bottle/sync
  state**, not specifically to `-v`: after a clean bottle reset, plain
  `highball run <exe> add 2 3` also prints `CLI add 2 3 5`. So `-v` was the safe
  runner setting, not the root cause of the failures.
- **Transient bottle noise:** Wine stderr intermittently shows `msync` bootstrap
  lines, `winemenubuilder.exe` (126), and `idmwfp.sys` / `fwpkclnt.sys` /
  `ZwLoadDriver` errors. These were stripped from the canonical transcripts (the
  launch line is retained as a note), but the apps still ran and printed their
  markers. They are bottle/driver noise, not app failures.
- **DLL minimalism:** `libstdc++-6.dll` was tested alongside the two required
  DLLs and found **unnecessary** — all PE apps still run and print their markers
  with it removed. It is therefore not present in `binaries/windows/` and is not
  listed in `SHA256SUMS.txt`. Only `libwinpthread-1.dll` and
  `libgcc_s_seh-1.dll` are required.
- The canonical transcripts contain no absolute machine paths; command headers
  use `<repo-root>`-relative paths. Scratch raw captures (`*.raw_*.txt`,
  `*.verify_*.txt`) are gitignored and were not kept as proof.
