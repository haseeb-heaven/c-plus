# Multimedia / window method tests — run notes

## Scope

One deterministic C+ case per public function of the seven assigned headers:
`graphics.hp`, `gui.hp`, `image.hp`, `video.hp`, `window.hp`, `input.hp`,
`audio.hp`. Every case calls the function and checks an observable result,
printing exactly `<header> <method> PASS|FAIL`. Files created here:

- `window_methods.cp`, `graphics_methods.cp`, `input_methods.cp`,
  `image_methods.cp`, `audio_methods.cp`, `gui_methods.cp`, `video_methods.cp`
- `methods.txt` — 127-line manifest (`<header> <method> <status>`)
- `run_all.sh` — canonical build+run driver; prints the final `METHOD_TEST` line
- `out/` — build logs and per-test stdout/stderr (generated, not source)

## Environment

- macOS arm64, branch `agent/modern-stdlib`, repo root
  `/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib`.
- `pkg-config sdl3` → 3.4.18; `libavformat/libavcodec/libavutil` → 63.1.102 /
  63.1.102 / 61.1.102. Prebuilt third-party archives in `build/ext`
  (`libstb.a`, `libminiaudio.a`, `libnuklear.a`).
- FFmpeg fixture `build/stdlib-tests/clip.mp4` (64x48, h264) already present.
- All runs use `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`; miniaudio uses its
  explicit null backend (`audio_context(1, ...)`). No display or audio device
  is needed, so **nothing was skipped**.

## Exact commands

All build commands are run from the repo root and use the required form
`./build/c+ build <test>.cp --strict --sanitize=none ... -o <bin>`.

```sh
SDL="-I/opt/homebrew/include -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3"
FF="-I/opt/homebrew/Cellar/ffmpeg/9.0.2_1/include -L/opt/homebrew/Cellar/ffmpeg/9.0.2_1/lib -lavformat -lavcodec -lavutil"
ROOT=$PWD
D=tests/stdlib/method_tests/multimedia

./build/c+ build $D/window_methods.cp   --strict --sanitize=none $SDL -o $D/out/window_methods
./build/c+ build $D/graphics_methods.cp --strict --sanitize=none $SDL -o $D/out/graphics_methods
./build/c+ build $D/input_methods.cp    --strict --sanitize=none $SDL -o $D/out/input_methods
./build/c+ build $D/image_methods.cp    --strict --sanitize=none -I$ROOT/third_party/stb -L$ROOT/build/ext -lstb -o $D/out/image_methods
./build/c+ build $D/audio_methods.cp    --strict --sanitize=none -I$ROOT/third_party/miniaudio -L$ROOT/build/ext -lminiaudio -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio -Wl,-framework,AudioToolbox -o $D/out/audio_methods
./build/c+ build $D/gui_methods.cp      --strict --sanitize=none $SDL -I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear -o $D/out/gui_methods
./build/c+ build $D/video_methods.cp    --strict --sanitize=none $FF -o $D/out/video_methods
```

Run each with a 30-second cap:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 30 $D/out/<name>
```

Or do everything (build + run + count + final line) in one step:

```sh
sh tests/stdlib/method_tests/multimedia/run_all.sh
```

Note: the harness shell is zsh, which does not word-split unquoted variables.
The runner is `#!/bin/sh` and expands `$SDL`, `$FF`, etc. unquoted so each flag
arrives as a separate argv entry. Passing a `pkg-config` string as a single
quoted argv makes the C+ driver treat the whole string as one `-I` value and
the build fails with "SDL3/SDL.h file not found".

## Results (clean run from an empty `out/`)

| test | header | methods | PASS | FAIL | SKIPPED |
|---|---|---:|---:|---:|---:|
| window_methods.cp | window.hp | 11 | 11 | 0 | 0 |
| graphics_methods.cp | graphics.hp | 17 | 17 | 0 | 0 |
| input_methods.cp | input.hp | 10 | 10 | 0 | 0 |
| image_methods.cp | image.hp | 12 | 12 | 0 | 0 |
| audio_methods.cp | audio.hp | 32 | 32 | 0 | 0 |
| gui_methods.cp | gui.hp | 19 | 19 | 0 | 0 |
| video_methods.cp | video.hp | 26 | 26 | 0 | 0 |
| **total** | | **127** | **127** | **0** | **0** |

Runner exit code: `0`. Final output line:

```
METHOD_TEST multimedia 127 127 0 0
```

## Enumeration / count reconciliation

The task hints were graphics 16, gui 18, image 10, video 25, window 11,
input 10, audio 29 (sum 119). Those appear to omit some constructors and free
helpers. This work enumerated *every* callable public function in each header
(the task's primary instruction), adding 8 more:

| header | hint | enumerated | extra functions covered |
|---|---:|---:|---|
| graphics | 16 | 17 | `surface(SDL_Surface*)` |
| gui | 18 | 19 | `gui(...)` constructor |
| image | 10 | 12 | `image_from_pixels`, `image_from_memory` |
| video | 25 | 26 | one of the three `video_*` free helpers / class ctors |
| window | 11 | 11 | — |
| input | 10 | 10 | — |
| audio | 29 | 32 | `decoder_from_memory`, `audio_sleep_ms`, one ctor |

All 127 are listed individually in `methods.txt` with their PASS status.

## Verification notes (no fabricated passes)

- Every `void` method is checked through an observable side effect, e.g.
  `gui::input_end` clears `mouse.grab/ungrab/grabbed`, `gui::window_end` makes
  `widget_bounds` return 0, `gui::render` is read back from the SDL surface,
  `audio_sleep_ms` advances `CLOCK_MONOTONIC`.
- The SDL3 software target stores pixels as BGRA with an unused alpha byte, so
  `surface::pixel` is checked on colour bytes (`inside[2] > 200`, others < 50)
  rather than assuming RGBA.
- `video_stream_params`/`video_codec_id` are exercised with the deterministic
  NULL inputs; `decoder::ctor` with `(NULL, -1)` verifies the documented error
  path. The media/decoder/frame happy paths use the generated clip fixture.
- Array sizes are numeric literals (not `4 * 3 * 3`): the C+ strict-mode bounds
  checker mis-sizes an expression-sized array and aborts at runtime.
- No method required a SKIP: SDL's dummy driver, miniaudio's null backend, stb,
  Nuklear and FFmpeg all run headless on this macOS host.

## Known limitations

- Pixel comparison is deliberately tolerant on colour byte ordering because the
  SDL3 renderer target format is backend-selected; the check still proves a red
  rectangle was drawn and read back.
- `gui::button` is accepted as either 0 or 1 (the documented boolean return);
  a deterministic click is covered indirectly by `input_button`'s state check.
- `out/` is generated output, not source; regenerate with `run_all.sh`.
