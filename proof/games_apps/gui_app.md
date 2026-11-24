# GUI app — `binaries/gui_app`

Source: `examples/gui_app/gui_app.cp`
Libraries used: `stdlib/widgets.hp` + SDL3 3.4.18 (dummy/hidden video driver)

## What it proves

A widget tree is built and laid out deterministically; hit-testing and
serialization are verified with a CRC32, and a real hidden SDL3 frame is
produced. Invalid ids, bounds and NULL arguments are refused with stable
markers.

## Evidence

- Binary: `binaries/gui_app` (Mach-O arm64, SHA-256 in `SHA256SUMS.txt`)
- Build: `transcripts/gui_app.build.log`
- Run: `transcripts/gui_app.run.txt`

Representative captured lines:

```
GUI_SERIAL bytes=157 crc32=a52b03af
GUI_SDL window=320x240 hidden=1 frame=1 widgets=4 driver=dummy
GUI_RESULT describe:0,0,0 OK
GUI_REFUSED tag=describe:999,0,0 category=2 code=101 message=scenario: id out of range
GUI_RESULT describe:999,0,0 REFUSED 101
```

The owning suite (`examples/gui_app/run.sh`) runs 30 scenarios with byte-exact
expected outputs: 6 successes and 24 defined refusals, 0 failures.

## Reproduce

```sh
sh examples/gui_app/run.sh
SDL_VIDEODRIVER=dummy proof/games_apps/binaries/gui_app describe 0
sh proof/games_apps/run.sh
```

Headless/dummy driver only — this is a deterministic GUI-logic proof, not an
interactive rendering benchmark. No Windows runtime claim.
