# ADV1000 / multimedia — run notes

## Scope

1000 deterministic adversarial scenarios across the seven assigned headers:
`stdlib/window.hp`, `stdlib/graphics.hp`, `stdlib/input.hp`, `stdlib/image.hp`,
`stdlib/audio.hp`, `stdlib/gui.hp`, `stdlib/video.hp`.

Files created here (only this directory is touched):

- `adv1000_multimedia.cp` — the single C+ harness (fixed seed, 50 kinds, 1000 ids)
- `run_all.sh` — canonical build + run + digest driver (`timeout 120`)
- `results/` — generated binaries, fixtures, `scenarios.log`, digests (gitignored)
- `NOTES.md` — this file

## Method

- One binary links every library used by the method-test group: SDL3, stb,
  miniaudio (+ Apple frameworks), Nuklear and FFmpeg, with the **same link
  flags** as `tests/stdlib/method_tests/multimedia/run_all.sh`.
- Headless throughout: `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`, and
  miniaudio is constructed with its explicit null backend (`audio_context(1)`).
- Determinism: every scenario id 0..999 derives all parameters from a fixed
  SplitMix64 seed and its own id via `adv_rnd()`; no clock, PID or address.
  `kind = id % 50` gives 50 distinct kinds (6–9 per header); the id itself
  makes every scenario distinct.
- Sentinel model: `struct adv_arena` places byte guards (`0xA5/0x5A/0x3C/
  0xC3/0x7E/0xE7`) around byte, int and float out-parameter regions. Every
  case arms them, calls one API, then checks all six guards plus all 128
  pre-seeded out-parameters. A single changed byte fails the case.
- Coverage: 0×0 / negative / oversized dimensions, invalid pixel formats
  (8-bpp SDL surface), NULL handles, out-of-range pixels and coordinates,
  truncated / crafted-oversized PNG/WAV/MP4 media, zero-size buffers, NULL
  paths and double-drop. Each case asserts the documented refusal or defined
  result rather than merely "did not crash".

## Exact commands

Build (from repo root; `/bin/sh` word-splits the pkg-config strings):

```sh
SDL="$(pkg-config --cflags --libs sdl3)"
FF="$(pkg-config --cflags --libs libavformat libavcodec libavutil)"
ROOT=$PWD
D=tests/adversarial_1000/multimedia
./build/c+ build $D/adv1000_multimedia.cp --strict --sanitize=none \
  $SDL \
  -I$ROOT/third_party/stb -L$ROOT/build/ext -lstb \
  -I$ROOT/third_party/miniaudio -L$ROOT/build/ext -lminiaudio \
  -Wl,-framework,CoreFoundation -Wl,-framework,CoreAudio \
  -Wl,-framework,AudioToolbox \
  -I$ROOT/third_party/nuklear -L$ROOT/build/ext -lnuklear \
  $FF -o $D/results/adv1000_multimedia
```

Run (120 s bound; log path and clip fixture path are argv):

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 120 \
  $D/results/adv1000_multimedia \
  $D/results/scenarios.log build/stdlib-tests/clip.mp4
```

Everything in one step:

```sh
sh tests/adversarial_1000/multimedia/run_all.sh
```

The runner prints the 1000 `S ...` lines, a `DIGEST sha256 ...` line, and the
authoritative final line last:

```
ADV1000 multimedia 1000 <refused> sentinel_ok <failures>
```

`sentinel_ok` is a token (not a count) to match the aggregate runner's
`*_ok` integrity contract. The per-case `<sentinel_ok>` field is 0/1.

## Results

(Filled after the verified clean run — see below.)

## Reconciliation and toolchain notes

- `S`-lines written to stdout and to `results/scenarios.log` are identical;
  the log contains exactly 1000 lines and the digest is their SHA-256.
- The C+ strict checker uses a file-wide name→type and name→array-length map:
  two different arrays or class objects sharing a variable name are lowered
  with the wrong length/type. All locals therefore use type-unique names
  (`img_src36`, `im_src16`, `wav_buf`, `err_buf`, `sfc_null`, `rnd_null`,
  `snd_null`, `dev_null`, `vdec_null`, `im_a`, `ad_a`, ...).
- Nuklear asserts if the same window is begun twice in one frame; the
  extreme-rect case uses a fresh title per id and only calls `window_end()`
  when `window_begin()` succeeded (a clipped begin ends itself). Calling
  `ui.render(0)` here would invoke an expensive full font-atlas upload, so it
  is avoided.
- SDL's dummy driver accepts positive window dimensions up to `INT_MAX`
  without allocating a raster, so oversized window dimensions are reported as
  a **defined result** (valid handle, no corruption), not a refusal. The
  header's guard for non-positive dimensions is verified as a refusal, and
  oversized dimensions embedded in a crafted PNG IHDR are verified as refused
  by stb_image. This is deliberate, honest classification.

## Evidence, not proof

These are deterministic contract probes. They demonstrate that the listed
paths refuse or return defined results with guards intact on this host; they
cannot establish the absence of all defects in the multimedia stack.
