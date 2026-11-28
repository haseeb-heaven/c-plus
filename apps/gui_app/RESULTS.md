# gui_app — exact results (snapshot)

Recorded from a real run of `sh apps/gui_app/run.sh` from the repository
root. This is a snapshot, not a promise: re-run the command to reproduce it.

## Environment / toolchain

| Item | Value |
| --- | --- |
| Snapshot time (UTC) | `2026-11-24T06:02:00Z` |
| Host | Darwin arm64, macOS 27.2.1 |
| C+ driver | `C+ 0.1.0`, backend `host Clang/LLVM` (`./build/c+ --version`) |
| Backend clang | Apple clang 16.0.0 (`clang-1600.0.26.6`), target `arm64-apple-darwin27.2.0`, `/usr/bin/clang` |
| SDL3 | 3.4.18 (`pkg-config --cflags --libs sdl3` → `-I/opt/homebrew/include -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3`) |
| Video driver | `dummy` (`SDL_VIDEODRIVER=dummy` forced by `run.sh`) |
| Hardened libc | `build/libc/libcplus_hardened.a` (linked via `--`) |
| Windows cross compiler | `x86_64-w64-mingw32-gcc (GCC) 16.2.0`, target `x86_64-w64-mingw32` |
| Timeouts | `timeout 30` per scenario, `timeout 300` for the whole runner |

`stdlib/widgets.hp` compiled with **0 errors** in `--default` mode. The build
log contains 29 advisory `[CPLUS-*]`/clang warnings about unverified pointer
extents and possible division by zero in the headers; `--default` warns rather
than rejects for those, and the frozen output checks exercise the runtime
paths that could otherwise hide a defect.

## Build command (verbatim shape)

```sh
./build/c+ build apps/gui_app/gui_app.cp --default --sanitize=none -I. \
  $(pkg-config --cflags --libs sdl3) -o apps/gui_app/bin/gui_app \
  -- build/libc/libcplus_hardened.a
```

Exit 0; `apps/gui_app/bin/gui_app` exists and links
`/opt/homebrew/opt/sdl3/lib/libSDL3.0.dylib`.

## Final lines (verbatim)

```
GUI_APP_WINDOWS_COFF compile_only=1 native_probe=PASS object=PASS runtime=BLOCKED
GUI_APP 30 6 0 24
```

`run.sh` process exit: **0**. No `FAIL`, `MISMATCH` or `ERROR` line appeared in
the run log. stderr was empty for all 30 cases, both runs.

## Totals

| Metric | Value |
| --- | --- |
| Scenarios | 30 |
| Expected successes | 6 (5 with dummy driver + 1 with `SDL_VIDEODRIVER` unset) |
| Expected refusals | 24 (18 `refuse <case>` + 6 invalid commands/arguments) |
| Failed | 0 |
| Byte-for-byte output mismatches | 0 |
| Nondeterministic second runs | 0 |

## Verbatim transcripts (representative)

`describe 0`:

```
GUI_WIDGETS scenario=0 title=login count=6 columns=3 grid=240x90
GUI_LAYOUT cell=80x45 origin=0,0
WIDGETS 6
W 1 4 1 1 78 43 3 root
W 2 2 81 1 78 43 3 user
W 3 3 161 1 78 43 3 name
W 4 2 1 46 78 43 3 pass
W 5 3 81 46 78 43 3 secret
W 6 1 161 46 78 43 3 OK
GUI_SERIAL bytes=157 crc32=a52b03af
GUI_HIT p=5,5 index=0 id=1 label=root
GUI_HIT p=85,5 index=1 id=2 label=user
GUI_RESULT describe:0,0,0 OK
```

`render 320 240 2` (tail; the core lines above are identical to `describe 2`):

```
GUI_HIT p=5,5 index=0 id=20 label=dlg
GUI_HIT p=5,45 miss=1
GUI_SDL window=320x240 hidden=1 frame=1 widgets=4 driver=dummy
GUI_RESULT render:320,240,2 OK
```

Disabled-widget evidence: scenario 2 disables position 2 (`YES`, flags becomes
`2` = visible only), so the hit at `p=5,45` (inside that cell) is correctly a
`miss=1`.

Refusal evidence (exact first lines, all with exit 0 and empty stderr):

```
GUI_REFUSED tag=small-grid category=2 code=101 message=widgets: grid cell too small
GUI_REFUSED tag=duplicate-id category=1 code=108 message=widgets: duplicate id
GUI_REFUSED tag=long-label category=1 code=108 message=widgets: NULL, oversized or control-character label
GUI_REFUSED tag=null-hit-output category=1 code=102 message=widgets: NULL hit-test output
GUI_REFUSED tag=small-serialize-buffer category=5 code=101 message=widgets: serialization buffer too small
GUI_REFUSED tag=bad-render-huge category=2 code=101 message=gui_app: window dimensions exceed GUI_MAX_DIM
```

The full frozen expected transcripts are in `apps/gui_app/expected/*.out`.

## Windows PE compile-only evidence

```
WIN_PROBE bytes=157 crc32=a52b03af OK
WIN_CROSS_COFF Intel amd64 COFF object file, no line number info, not stripped, 10 sections, symbol offset=0x6e6, 33 symbols, 1st section name ".text"
GUI_APP_WINDOWS_COFF compile_only=1 native_probe=PASS object=PASS runtime=BLOCKED
```

The native probe's CRC32 (`a52b03af`) matches the demo's `GUI_SERIAL` checksum,
so the plain-C mirror used for the cross-compile is anchored to the real C+
program. The mingw step is `x86_64-w64-mingw32-gcc -O2 -c` — compile only.

## Non-claims

- No Windows runtime execution, no `.exe`, no Wine/Highball success. Runtime is
  BLOCKED in this repository and was not attempted.
- The COFF object proves only that the portable core mirror compiles for the
  mingw target. It does **not** verify the SDL3 GUI layer, C+ lowering or C+
  runtime on Windows.
- Results apply to macOS arm64 / SDL3 3.4.18 / Apple clang 16 only. All frames
  were produced with `SDL_VIDEODRIVER=dummy`; a headless frame is not
  live-display success.
- No Rust equivalence, 100% safety, production readiness or interactive-game
  claim is made.
