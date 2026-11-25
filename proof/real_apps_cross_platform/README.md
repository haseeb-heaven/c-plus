# Real apps, cross-platform proof

## What this folder proves

Eight real programs/libraries written in safe modern **C+ 0.1.0** (bounded
standard-library APIs, the hardened `libcplus` runtime, checked refusals on
hostile input) were built once from the sources in this repository and then:

- **run natively on macOS arm64** — genuine Mach-O 64-bit arm64 executables,
  executed with exit 0 and exact expected marker lines; and
- **run as genuine Windows PE32+ x86-64 executables under Highball/Wine** —
  engine `x64-crossover26.3-r27`, bottle `Games`, renderer `dxvk`, invoked as
  `highball run -v <exe> [args]` with the two required MinGW runtime DLLs
  (`libwinpthread-1.dll`, `libgcc_s_seh-1.dll`) placed alongside the `.exe`.
  All eight PE apps executed with exit 0 and the same marker lines.

This is execution evidence for the recorded host, runtime, engine and fixed
inputs — not a portability guarantee and not a memory-safety proof (see
[Honest scope](#honest-scope)).

## Host and toolchain (exact)

| component | value |
| --- | --- |
| Host OS / architecture | Darwin, `arm64` (`uname -s` / `uname -m`) |
| macOS compiler | Apple clang `16.0.0` (clang-1600.0.26.6) |
| Language driver | `build/c+` — C+ `0.1.0`, host Clang/LLVM backend |
| Windows cross-compiler | mingw-w64 `14.0.0` GCC (`x86_64-w64-mingw32-gcc`), producing `PE32+ executable (console) x86-64, for MS Windows` |
| Windows runtime | Highball CLI `0.10.22`, Wine engine `x64-crossover26.3-r27`, bottle `Games`, renderer `dxvk`, `-v` verbose mode |

Build command used for each source (`sh tests/modern_libs_cross/run.sh`):

```sh
./build/c+ build <source> --default --sanitize=none -I. -o <bin> -- build/libc/libcplus_hardened.a
# gui_app additionally: $(pkg-config --cflags --libs sdl3)
```

## The eight apps

Paths are relative to this folder; `binaries/` and `transcripts/` are split by
platform. Every listed run exited 0 and printed the exact marker shown
(macOS stderr empty; Windows Highball/Wine bottle noise documented in
`transcripts/windows/RESULTS.md`).

| app | source path | macOS result | Windows result | binaries | transcripts | screenshot |
| --- | --- | --- | --- | --- | --- | --- |
| math3d | `tests/stdlib/math3d/test_math3d.cp` | `MATH3D 36 0` (exit 0) | `MATH3D 36 0` (exit 0) | `binaries/macos/math3d`, `binaries/windows/math3d.exe` | `transcripts/macos/math3d.txt`, `transcripts/windows/math3d.txt` | `screenshots/math3d-macos.svg`, `screenshots/math3d-windows.svg` |
| scene3d | `tests/stdlib/scene3d/test_scene3d.cp` | `SCENE3D 79 0` (exit 0) | `SCENE3D 79 0` (exit 0) | `binaries/macos/scene3d`, `binaries/windows/scene3d.exe` | `transcripts/macos/scene3d.txt`, `transcripts/windows/scene3d.txt` | `screenshots/scene3d-macos.svg`, `screenshots/scene3d-windows.svg` |
| mesh3d | `tests/stdlib/mesh3d/test_mesh3d.cp` | `drop 1 1` (all checks 1, exit 0) | `drop 1 1` (all checks 1, exit 0) | `binaries/macos/mesh3d`, `binaries/windows/mesh3d.exe` | `transcripts/macos/mesh3d.txt`, `transcripts/windows/mesh3d.txt` | `screenshots/mesh3d-macos.svg`, `screenshots/mesh3d-windows.svg` |
| widgets | `tests/stdlib/widgets/test_widgets.cp` | `WIDGETS 144 0` (exit 0) | `WIDGETS 144 0` (exit 0) | `binaries/macos/widgets`, `binaries/windows/widgets.exe` | `transcripts/macos/widgets.txt`, `transcripts/windows/widgets.txt` | `screenshots/widgets-macos.svg`, `screenshots/widgets-windows.svg` |
| cli | `tests/stdlib/cli/test_cli.cp` | `CLI_TEST 27 27 0` (exit 0) | `CLI_TEST 27 27 0` (exit 0) | `binaries/macos/cli`, `binaries/windows/cli.exe` | `transcripts/macos/cli.txt`, `transcripts/windows/cli.txt` | `screenshots/cli-macos.svg`, `screenshots/cli-windows.svg` |
| games3d | `examples/games3d/scene3d_demo.cp` | `SCENE_DONE status=OK nodes=7 meshes=3` (exit 0) | `SCENE_DONE status=OK nodes=7 meshes=3` (exit 0) | `binaries/macos/games3d`, `binaries/windows/games3d.exe` | `transcripts/macos/games3d.txt`, `transcripts/windows/games3d.txt` | `screenshots/games3d-macos.svg`, `screenshots/games3d-windows.svg` |
| gui_app | `examples/gui_app/gui_app.cp` | `GUI_RESULT describe:0,0,0 OK` (`describe 0`, exit 0) | `GUI_RESULT describe:0,0,0 OK` (`describe 0`, exit 0); default no-arg run gives `GUI_RESULT describe:0,0,0 REFUSED 108` | `binaries/macos/gui_app`, `binaries/windows/gui_app.exe` | `transcripts/macos/gui_app.txt`, `transcripts/windows/gui_app_describe0.txt`, `transcripts/windows/gui_app.txt` | `screenshots/gui_app-macos.svg`, `screenshots/gui_app-windows.svg` |
| cli_app | `examples/cli_app/cli_calc.cp` | `CLI add 2 3 5` (`add 2 3`, exit 0) | `CLI add 2 3 5` (`add 2 3`, exit 0) | `binaries/macos/cli_app`, `binaries/windows/cli_app.exe` | `transcripts/macos/cli_app.txt`, `transcripts/windows/cli_app.txt` | `screenshots/cli_app-macos.svg`, `screenshots/cli_app-windows.svg` |

Short per-app notes with APIs, exact markers and binary/transcript names live
in `math3d.md`, `scene3d.md`, `mesh3d.md`, `widgets.md`, `cli.md`, `games3d.md`,
`gui_app.md` and `cli_app.md` beside this file.  Summary sheets:
`transcripts/macos/RESULTS.md` and `transcripts/windows/RESULTS.md`.

### gui_app invocation note

macOS was invoked as `./gui_app describe 0` and Windows has a matching
`highball run -v ./gui_app.exe describe 0` capture
(`transcripts/windows/gui_app_describe0.txt`); both print
`GUI_RESULT describe:0,0,0 OK`.  The default Windows transcript
`transcripts/windows/gui_app.txt` uses no arguments, so the app's `describe`
command emits a defined refusal (`GUI_RESULT describe:0,0,0 REFUSED 108`, exit
0).  This is an argument difference, not a platform difference; the SVG for
Windows reflects the default no-arg transcript.  See `gui_app.md`.

## Why the Windows runtime is no longer blocked

Earlier local notes (`tests/cross_platform_apps/RESULTS.md` and
`tests/modern_libs_cross/RESULTS.md`) recorded Windows runtime as **BLOCKED**
with `highball-wine-stall` on a trivial executable.  That blocker no longer
applies here: after resetting stale bottle/sync state
(`timeout 20 highball bottle kill Games`, then a short pause), placing the two
required MinGW DLLs next to each `.exe`, and using `highball run -v`, all eight
PE executables completed and printed their exact app markers with exit 0.
`-v` was the conservative runner setting; the root cause of earlier flakiness
was stale bottle state, not the absence of verbose mode.  Only
`libwinpthread-1.dll` and `libgcc_s_seh-1.dll` are required —
`libstdc++-6.dll` was tested and found unnecessary, so it is not shipped.

## Honest scope

- **Runtime environment:** Windows execution is verified **only** through
  Highball/Wine with engine `x64-crossover26.3-r27`, bottle `Games` and
  renderer `dxvk` on this host.  This is **not** a claim about all Windows
  configurations, native Windows hardware, or other Wine versions/bottles.
- **No live-game claim:** `games3d` is a deterministic, headless 3D scene demo
  built from `math3d.hp`/`mesh3d.hp`/`scene3d.hp`; it is not a game and does not
  touch a GPU.  No retail game or live-game behaviour is asserted here.
- **No full memory-safety proof:** exact marker lines and clean refusals are
  evidence for the listed deterministic inputs only.  They are not a proof of
  absence of memory errors, and are not a substitute for the dedicated
  sanitizer/hostile-input/security suites elsewhere in the repository.
- **Screenshots:** the SVGs in `screenshots/` are **rendered terminal
  transcripts of real captured output**, produced by POSIX sh + awk in
  `screenshots/render.sh`.  They are **not fabricated GUI captures**: no window
  pixels, icons or 3D frames were painted.  The window chrome and `OK`/`REFUSED`
  colouring are decoration; the exact captured text is embedded, XML-escaped,
  in each SVG's `<desc>`.  All 16 SVGs are well-formed XML and hashed in
  `screenshots/SHA256SUMS.txt`.
- **Runtime noise:** Highball/Wine may emit transient `msync`,
  `winemenubuilder.exe` and driver-load messages on stderr; these are bottle
  noise, stripped from the canonical transcripts, and are not app failures.

## Reproduce

From the repository root (`PATH` may need `$HOME/.local/bin` prepended if
Highball is installed per-user; `run.sh` also accepts a `HIGHBALL` override):

```sh
# Full bounded verifier (checksums, file types, transcripts, live re-runs, SVGs)
sh proof/real_apps_cross_platform/run.sh
```

It prints one `PASS`/`FAIL` line per check and finishes with
`CROSS_PROOF <checks> <pass> <fail>`, exiting nonzero iff `fail > 0`.

Manual equivalents:

```sh
cd proof/real_apps_cross_platform

# Integrity and binary format
(cd binaries/macos   && shasum -a 256 -c SHA256SUMS.txt)
(cd binaries/windows && shasum -a 256 -c SHA256SUMS.txt)
(cd screenshots      && shasum -a 256 -c SHA256SUMS.txt)
file binaries/macos/* binaries/windows/*.exe binaries/windows/*.dll

# macOS live re-runs (exact markers)
(cd binaries/macos && env -u SDKROOT -u CPLUS_CLANG timeout 30 ./cli_app add 2 3)
(cd binaries/macos && env -u SDKROOT -u CPLUS_CLANG SDL_VIDEODRIVER=dummy timeout 30 ./games3d)

# Windows live re-runs (engine x64-crossover26.3-r27; DLLs beside the exe)
timeout 20 highball bottle kill Games >/dev/null 2>&1
(cd binaries/windows && timeout -k 5 90 highball run -v ./cli_app.exe add 2 3)
(cd binaries/windows && timeout -k 5 90 highball run -v ./games3d.exe)

# Screenshots from the captured transcripts
(cd screenshots && ./render.sh && xmllint --noout *.svg)
```

## Verifier result

`sh proof/real_apps_cross_platform/run.sh` (overall bound 900 s) reported:

```
CROSS_PROOF 58 58 0
```

Breakdown: 3 checksum manifests, 18 file types (8 Mach-O + 8 PE32+ console +
2 PE32+ DLL), 17 transcript markers (8 macOS + 8 Windows + the extra Windows
`gui_app describe 0` capture), 4 live re-runs (`cli_app add 2 3` and `games3d`
on both platforms), and 16 well-formed SVGs.  Exit status 0 iff `fail` is 0.
