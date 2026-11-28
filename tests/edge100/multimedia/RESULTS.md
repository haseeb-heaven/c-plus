# EDGE100 multimedia — results

## Final result

```
EDGE100 multimedia 127 173 173 0
```

127 public APIs (every callable in the seven assigned headers), 173 fork-isolated
edge cases, 173 passed, 0 failed. Runner exit code: `0`. Reproduced with a clean
`out/` build twice; identical final line both times.

## Scope

| Header | APIs | Cases | Passed | Failed |
|---|---:|---:|---:|---:|
| `window.hp` | 11 | 20 | 20 | 0 |
| `graphics.hp` | 17 | 23 | 23 | 0 |
| `input.hp` | 10 | 11 | 11 | 0 |
| `image.hp` | 12 | 24 | 24 | 0 |
| `audio.hp` | 32 | 46 | 46 | 0 |
| `gui.hp` | 19 | 20 | 20 | 0 |
| `video.hp` | 26 | 29 | 29 | 0 |
| **total** | **127** | **173** | **173** | **0** |

The API list is enumerated directly from the headers and recorded in
`API_MANIFEST.md` (with header line numbers and per-API case tags). The manifest
is generated from the harness's own dispatch table, so it cannot drift from what
actually runs.

## Edge-category coverage

| Tag | Meaning | Cases |
|---|---|---:|
| `valid` | documented happy path | 92 |
| `null` | NULL pointers / invalid or empty objects | 39 |
| `drop2` | double-`drop()` idempotence | 14 |
| `oob` | out-of-range coordinates, indices, enum values | 9 |
| `dim` | zero / negative / invalid dimensions or flags | 8 |
| `trunc` | truncated image/audio/video media | 5 |
| `fmt` | invalid format / nonexistent renderer driver / bad channel count | 4 |
| `dimbig` | oversized dimensions that must be rejected by checked arithmetic | 2 |

Every API has at least one `valid` case and at least one edge case; categories
that are not meaningful for a given API are not fabricated. Details per API are
in `API_MANIFEST.md`.

## Method

- **Fork-per-case.** The harness forks a child for every case. The child runs
  exactly one case, prints `<header> <method> <tag> PASS|FAIL`, and exits. A
  crash (signal) or hang cannot poison another case — the parent prints
  `<header> <method> <tag> FAIL (signal N)` and keeps going.
- **Bounded timeout 60.** The parent arms `alarm(60)` with a handler that
  `SIGKILL`s the current child; `run.sh` additionally wraps every harness
  invocation in `timeout 60`. Both layers were exercised implicitly (no case
  came close to the bound; the longest full run was ~14 s for all 173 cases).
- **Headless drivers.** `SDL_VIDEODRIVER=dummy`, `SDL_AUDIODRIVER=dummy`, and
  miniaudio's explicit null backend (`audio_context(1, ...)`). No display or
  audio device is required. Nothing was skipped.
- **Build flags** are copied verbatim from
  `tests/stdlib/method_tests/multimedia/run_all.sh`: `pkg-config` for SDL3 and
  libav{format,codec,util}, `third_party/{stb,nuklear,miniaudio}` includes and
  the prebuilt archives in `build/ext` (plus miniaudio's Apple frameworks).
  `--legacy-c --sanitize=none` is used for the same reason as the method suite:
  the third-party headers contain C-only patterns the strict C+ checker rejects.
- **Fixtures** are generated in-process with per-PID `/tmp` paths, so parallel
  agents cannot collide: a small PCM WAV, a 4x3 PNG written through stb, their
  truncated variants, and the existing `build/stdlib-tests/clip.mp4` fixture.

## Exact commands

```sh
sh tests/edge100/multimedia/run.sh
```

`run.sh` builds `out/harness`, loops over all case IDs under `timeout 60`, writes
every result line to `out/results.log`, prints them, then prints the final
`EDGE100` line. It exits `0` only when the failed count is `0`.

For a single case or a fast aggregate run:

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./tests/edge100/multimedia/out/harness <id>
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy ./tests/edge100/multimedia/out/harness all
./tests/edge100/multimedia/out/harness list
```

## Verification notes (no fabricated passes)

- Every `void` API is checked through an observable side effect: e.g.
  `window::drop`/`renderer::drop`/`surface::drop` via `valid()==0` and
  `native()==NULL`; `gui::input_motion` via Nuklear's cursor; `gui::input_end`
  via the grab flags; `audio_sleep_ms` via `CLOCK_MONOTONIC`.
- Constructors whose backend may legitimately succeed or fail are checked with
  the invariant `valid() == err.ok()` rather than a guessed outcome (e.g.
  `sdl(0xffffffff)`, `audio_context(0, ...)`, `audio_device` with 1024 channels).
- A deliberately oversized `image_from_pixels` call uses `INT_MAX x INT_MAX x 4`
  so the header's own checked-arithmetic guard fires *before* `malloc`/`memcpy`.
- `frame::pts` is checked after `drop()` (a freshly allocated `AVFrame` carries
  `AV_NOPTS_VALUE`, so asserting 0 before decode would be wrong).
- `gui::window_begin(NULL, ...)` is checked without a following `window_end()`:
  the NULL-title path returns 0 without opening a Nuklear window, and an
  unmatched `nk_end` aborts.
- FFmpeg prints benign `error reading header` lines to stderr for the truncated
  container; only each case's single result line is counted.

## Development findings (issues found and corrected in the harness, not in library code)

1. An early `image_from_pixels` oversized case passed a 4-byte source with
   `INT_MAX` dimensions; macOS overcommit let the huge `malloc` succeed and
   `memcpy` then segfaulted. The fork-per-case design isolated it as a single
   FAIL. The case now exercises the documented overflow guard instead. This is a
   caller-precondition issue, not a library defect.
2. An unmatched `gui::window_end()` after a NULL-title `window_begin()` tripped
   a Nuklear assertion (SIGABRT). The case was corrected as noted above.
3. `frame::pts()` on a freshly allocated frame is `AV_NOPTS_VALUE`, not 0; the
   check now uses the documented dropped-frame value.

## Known limitations

- Some APIs delegate entirely to SDL/FFmpeg/miniaudio, so out-of-range floats
  (`fill_rect`, `line`, `set_volume`) are verified as "no error / clamped" rather
  than asserting a specific backend result.
- `gui::button` returns a documentedly boolean value that depends on cursor
  state; the case accepts 0 or 1 while `gui::slider`/`widget_bounds` carry the
  deterministic assertions.
- `out/` is generated output, not source; regenerate with `run.sh`.
- No source or header files were modified; all files are confined to
  `tests/edge100/multimedia/`.
