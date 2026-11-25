# BLUE20 multimedia header safety — PROOF

**Agent:** BLUE-TEAM agent 19/20 · **Branch:** `agent/modern-stdlib`
**Scope (owned path only):** `tests/redblue20/blue_multimedia/`
**Headers under test:** `stdlib/window.hp`, `graphics.hp`, `gui.hp`, `input.hp`,
`image.hp`, `audio.hp`, `video.hp` (the seven public multimedia/window headers).

## Exact result

```
BLUE20_DIGEST 3198bcd67ffcaaab
BLUE20 multimedia 2400 2400 0
```

| metric | value |
|---|---|
| fixed-seed cases | **2400** (≥ 2000 required) |
| passed | **2400** |
| failed | **0** |
| fork / signal / timeout / token failures | **0** |
| canary violations | **0** |
| wall time (measured) | ~13 s (bounded by `timeout 60`) |
| per-case digest (FNV-1a over log) | `3198bcd67ffcaaab` |
| `results.log` SHA-256 | `e9c9d8125f7fb01652677532c3f319425228cb6ba39add966cd9325fe2f60df6` |
| exit status | `0` |

Two consecutive full runs produced byte-identical stdout, an identical
2400-line `results.log`, and the same digest — the result is deterministic.

## Method

1. **Headless fixtures built once by the parent.** SDL3 with
   `SDL_VIDEODRIVER=dummy` / `SDL_AUDIODRIVER=dummy`, a hidden 64×48 window,
   a software renderer + snapshot surface, a Nuklear GUI, a 4×3 image plus a
   valid PNG, a valid WAV plus truncated WAV/PNG fixtures, a miniaudio null
   backend (context + engine + decoder), and an FFmpeg media/decoder/frame
   built from `build/stdlib-tests/clip.mp4`. A 32-byte truncated MP4 is also
   generated.
2. **Fork per case.** For every case the parent forks a fresh child. The child
   derives all parameters from one fixed SplitMix64 seed
   (`0xB10C0000C0FFEE15`) mixed with the case id — no PID, address, clock, or
   host state. `kind = id % 50`, so each of the 50 refusal/defined scenarios
   is exercised 48 times. The child arms the canaries, runs exactly one
   documented contract, and writes an exact pass token on a pipe.
3. **Bounded timeout 60 (three layers).** Each child calls `alarm(60)`; the
   parent enforces a 60 s `CLOCK_MONOTONIC` deadline with a `waitpid(WNOHANG)`
   poll loop and `SIGKILL`s a hung child (marking it FAIL); `run.sh` wraps the
   whole binary in `timeout 60`.
4. **The BLUE properties checked per case** (a case passes only if the token,
   exit status, outcome, *and* canaries all agree):
   - **invalid dimensions refused** — window `0×N`/`N×0`/negative/oversized,
     image non-positive dims and channel counts out of `0..4`.
   - **invalid handles / formats refused** — NULL window/renderer/surface/gui/
     context/format handles, 8bpp surface pixel format, garbage SDL event type.
   - **null handles guarded** — every NULL path returns its documented value
     and never dereferences (`size()` keeps out-params at sentinel,
     `set_title(NULL)`, `handle_event(NULL)`, `window_begin(NULL)`,
     `slider(NULL)`, NULL-event input helpers, `decoder(NULL)`,
     `next_frame(NULL)`, `video_stream_params(NULL)`, etc.).
   - **truncated media refused** — 16-byte PNG buffer, 16-byte WAV buffer/file,
     32-byte truncated MP4, and a crafted PNG with `0xffffffff` IHDR dimensions.
   - **double-drop inert** — window/surface/gui/image/decoder/frame handles are
     dropped twice and remain invalid with no crash or corruption.
   - **canaries intact** — 32-byte byte guards bracket the byte/int/float
     out-parameter arena (with `0x5A5A5A5A` / `12345.5f` sentinels), plus an
     independent 64-byte global canary band. Any changed guard byte fails the
     case. No canary was ever touched.
5. **Multimedia link flags** (identical to
   `tests/stdlib/method_tests/multimedia/run_all.sh`): `sdl3`,
   `stb`, `miniaudio` + Apple frameworks, `nuklear`, and
   `libavformat/libavcodec/libavutil` via `pkg-config` / `build/ext`.

   The build uses `--legacy-c` (real C backend) rather than `--strict`, for the
   same documented reason as the sibling multimedia method suite: the third-
   party headers contain C-only patterns, and this proof deliberately invokes
   anti-patterns (double drop, null handles) that the C+ safety AST rejects
   statically. Runtime safety is established by the fork/canary/token checks
   above, not by the static checker.

## Coverage (all PASS, 48 executions each scenario unless noted)

- `window`: `zero_dims`, `negative_dims`, `oversized_dims`, `size_null_handle`,
  `set_title_null`, `double_drop`
- `graphics`: `surface_null_pixel`, `pixel_out_of_range`, `pixel_negative`,
  `invalid_pixel_format`, `renderer_null`, `double_drop`
- `gui`: `null_handles`, `handle_event_null`, `window_begin_null`,
  `button_slider_bad`, `extreme_rect`, `double_drop`
- `input`: `null_event`, `oob_scancode`, `garbage_event`, `poll_null`,
  `mouse_null`, `text_null`
- `image`: `null_source`, `bad_dimensions`, `oversized_dims`, `truncated_png`,
  `huge_ihdr`, `bad_channels`, `pixel_oob`, `write_null_path`, `double_drop`
- `audio`: `truncated_wav`, `zero_size_buffer`, `null_path`, `truncated_file`,
  `read_null_frames`, `read_zero_count`, `null_engine_path`, `null_context`,
  `double_drop`
- `video`: `error_text_null`, `stream_params_null`, `codec_id_null`,
  `decoder_null`, `media_null`, `truncated_media`, `next_frame_null`,
  `double_drop`

`double_drop` is exercised 288 times (7 APIs × 48) and `oversized_dims` 96
times (window + image × 48); every other named scenario runs 48 times. No
`FAIL`, `E`, signal, timeout, or canary-violation line appears in `results.log`.

## Reproduce

```sh
cd /Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib
sh tests/redblue20/blue_multimedia/run.sh
```

Artifacts (all inside the owned directory): `out/proof` (binary),
`out/results.log` (2400 canonical PASS lines), `out/results.sha256`,
`out/proof.out`, `out/build.log`. The only stderr output is the expected FFmpeg
`moov atom not found` diagnostic from the 48 truncated-MP4 cases.

## Scope note

This is strong, reproducible evidence over 2400 deterministic adversarial
cases covering the documented refusal/safe-result contracts of the seven
headers; like any finite test campaign it does not establish the absence of
all possible defects outside those contracts.
