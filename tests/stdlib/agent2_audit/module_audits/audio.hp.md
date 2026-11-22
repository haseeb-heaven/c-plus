# Safety audit — `stdlib/audio.hp` (audio via vendored miniaudio)

> **LABEL: STATIC SOURCE INSPECTION ONLY — NOT EXECUTED.** No test runner, compiler, or
> host process was run to produce this document. All statements derive from reading
> `stdlib/audio.hp` and the static inventory `docs/stdlib/REGRESSION_INVENTORY.md`. No
> behavior is claimed to be verified by execution. See §7.

## 1. Provenance and method

- Header inspected: `stdlib/audio.hp` (364 lines). Scope comment at `stdlib/audio.hp:1-11`;
  includes at `stdlib/audio.hp:15-20` (miniaudio at line 20).
- Exact counts/unexercised names from `docs/stdlib/REGRESSION_INVENTORY.md`:
  - §3 total **29 public callable entries** (`docs/stdlib/REGRESSION_INVENTORY.md:46,51`).
  - §4 exercised **8 of 29** (`docs/stdlib/REGRESSION_INVENTORY.md:75,80`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:108`.
  - §5 not exercised **21 of 29** (`docs/stdlib/REGRESSION_INVENTORY.md:150,155`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:179`.
- Static, not execution — `docs/stdlib/REGRESSION_INVENTORY.md:6-15,232-234`.

## 2. Public API list with counts

The inventory's 29 entries, with header line numbers:

| Class / group | Entries (header lines) | Count |
|---|---|---|
| `audio_context` | `audio_context` ctor `:26-45`; `valid` `:47-49`; `native` `:52-54`; `drop` `:56-61` | 4 |
| `audio_sound` | `audio_sound` ctor `:69-84`; `valid` `:86-88`; `play` `:90-101`; `stop` `:103-114`; `playing` `:116-118`; `drop` `:120-125` | 6 |
| `audio_engine` | `audio_engine` ctor `:134-147`; `valid` `:149-151`; `native` `:153-155`; `set_volume` `:157-165`; `play` `:168-179`; `load` `:182-185`; `drop` `:187-192` | 7 |
| `audio_device` | (ctor omitted — see below); `valid` `:228-230`; `start` `:232-243`; `stop` `:245-256`; `running` `:258-260`; `drop` `:262-267` | 5 |
| `audio_decoder` | `audio_decoder` ctor `:274-287`; `valid` `:289-291`; `channels` `:293-295`; `sample_rate` `:297-299`; `frame_count` `:301-308`; (read omitted); `drop` `:329-334` | 6 |
| free | `audio_sleep_ms` `:355-362` | 1 |
| **Total** | | **29** |

**Parser omissions found by source inspection** (multi-line signatures missed by
`entries()`; real public callables):
- `audio_device.audio_device(ma_context *, int, unsigned, unsigned, ma_device_data_proc, void *, error *)`
  at `stdlib/audio.hp:199-226` (signature spans lines 199-201).
- `audio_decoder.read(float *, unsigned long long, error *)` at
  `stdlib/audio.hp:310-327` (signature spans lines 311-312).
- `decoder_from_memory(const void *, size_t, error *)` at
  `stdlib/audio.hp:337-352` (signature spans lines 337-338).

Source inspection therefore sees **32 public callables**, while the audited headline count
is the inventory's **29**.

## 3. Public surface safety analysis (STATIC — source inspection only)

- **`audio_context`:** selects the silent `ma_backend_null` when `use_null` is set,
  otherwise auto-detects (`stdlib/audio.hp:26-45`). `drop()` uninitializes the context
  (`:56-61`). Note: `audio_engine`/`audio_device` take a borrowed `ma_context *`; no
  lifetime enforcement — dropping the context before dependent engines/devices is a
  caller-ordering obligation. No copy/move control declared.
- **`audio_sound`:** constructor validates engine/path pointers and maps
  `ma_sound_init_from_file` failure to `CPLUS_EXIT_IO` (`:69-84`). `play`/`stop` are
  validity-checked and report `CPLUS_EXIT_IO` on miniaudio failure (`:90-114`). `playing`
  (`:116-118`) and `drop` (`:120-125`) delegate to miniaudio.
- **`audio_engine`:** constructor builds `ma_engine_config` with the borrowed context and
  maps init failure to `CPLUS_EXIT_RESOURCE` (`:134-147`). `set_volume(float, ...)` calls
  `ma_engine_set_volume` with **no finite/range validation of `volume`** (`:157-165`); any
  float, including NaN/Inf/negative, is passed through. `play(path, ...)` is a
  fire-and-forget file play (`:167-179`). `load` returns an owned `audio_sound` by value
  (`:182-185`) — with no declared move semantics, the struct is returned by value and the
  caller must treat it as move-only; double-`drop` on a shallow copy would double-uninit.
- **`audio_device`:** constructor maps `capture` to device type, sets `sampleRate`,
  `dataCallback`, `pUserData`, and format `ma_format_f32` with requested `channels`
  (`:199-226`). It does **not validate `sample_rate` or `channels`** before passing them to
  `ma_device_init`; invalid values are left to miniaudio. The callback and user pointer are
  stored verbatim; lifetime of `user` is caller-owned. `start`/`stop`/`running`/`drop`
  delegate with validity checks (`:232-267`).
- **`audio_decoder`:** constructor uses `ma_decoder_config_init(ma_format_f32, 0, 0)`
  and maps `ma_decoder_init_file` failure to `CPLUS_EXIT_IO` (`:274-287`). **If `path` is
  NULL the constructor breaks out of the `do`/`while` without calling `error_fail`**
  (`:279-280`) — the error record is left in whatever state `error_reset` (line 277) put
  it, i.e. "ok", while `initialized` stays 0. `decoder_from_memory` relies on this
  sentinel by constructing `audio_decoder(NULL, err)` then overwriting the error
  (`:337-352`). `channels`/`sample_rate` read decoder fields directly (`:293-299`);
  `frame_count` maps miniaudio failure to a silent `0` with no error record (`:301-308`).
  `read` validates `valid()` and non-NULL `frames`, treats `frame_count == 0` as a silent
  no-op, and maps failure to `CPLUS_EXIT_IO` (`:310-327`). Its `frame_count` parameter is
  an `unsigned long long` passed to `ma_decoder_read_pcm_frames` as `ma_uint64`; no check
  that the caller's `frames` buffer actually holds that many samples — buffer capacity is
  entirely caller-trusted.
- **`audio_sleep_ms`:** nanosleep loop that retries on `EINTR` (`:354-362`); self-contained,
  no external pointer handling.

All above is read from source; none of it has been exercised here.

## 4. External dependency requirements

- **miniaudio** (public domain / MIT-0), vendored at `third_party/miniaudio`; included at
  `stdlib/audio.hp:20` (`#include "miniaudio.h"`), documented at `stdlib/audio.hp:1-3`.
  Uses `ma_context_*`, `ma_engine_*`, `ma_sound_*`, `ma_device_*`, `ma_decoder_*`,
  `ma_backend_null`, `ma_format_f32`, `MA_SUCCESS`.
- **C+ runtime / `cplus/cplus.h`** and `stdlib/error.hp` at `stdlib/audio.hp:18-19`.
- **POSIX `nanosleep`/`errno`** for `audio_sleep_ms` (`stdlib/audio.hp:15-17,354-362`).
- **Not a dependency of this module:** libuv, SDL3, libcurl, stb, and ffmpeg. In particular
  ffmpeg is used by `stdlib/video.hp`, not here; decoding/playback is miniaudio's job
  (`stdlib/audio.hp:6`).
- **Host availability (static dependency query only, not a test):**
  `third_party/miniaudio/miniaudio.h` and `miniaudio_impl.c` are present, and
  `build/ext/libminiaudio.a` exists. An existing compile-stage log
  `build/stdlib-tests/audio_test.build.log` shows `-lminiaudio` plus macOS frameworks
  `-Wl,-framework,CoreFoundation`, `CoreAudio`, `AudioToolbox` being passed (with
  "linker input unused" warnings, i.e. compile-only). These frameworks are macOS-only and
  are available on this darwin host. This establishes installability, not executed behavior.

## 5. APIs lacking any deep test

Per inventory §5 (`docs/stdlib/REGRESSION_INVENTORY.md:155,179`), the **21** unexercised
entries are: `audio_context.audio_context`, `audio_context.drop`,
`audio_sound.audio_sound`, `audio_sound.play`, `audio_sound.stop`, `audio_sound.playing`,
`audio_sound.drop`, `audio_engine.audio_engine`, `audio_engine.set_volume`,
`audio_engine.play`, `audio_engine.load`, `audio_engine.drop`, `audio_device.stop`,
`audio_device.running`, `audio_device.drop`, `audio_decoder.audio_decoder`,
`audio_decoder.channels`, `audio_decoder.sample_rate`, `audio_decoder.frame_count`,
`audio_decoder.drop`, `audio_sleep_ms`.

Additionally, source inspection finds these public callables absent from the inventory
and therefore uncredited: `audio_device.audio_device` (`:199-226`), `audio_decoder.read`
(`:310-327`), `decoder_from_memory` (`:337-352`).

**Stronger source-inspection caveat:** no deep-corpus file includes `stdlib/audio.hp`, and
grep finds no deep file containing `audio_context`, `audio_engine`, `audio_device`, or
`audio_decoder` tokens. The 8 "exercised" credits
(`docs/stdlib/REGRESSION_INVENTORY.md:108`: `audio_context.valid`, `audio_context.native`,
`audio_sound.valid`, `audio_engine.valid`, `audio_engine.native`, `audio_device.valid`,
`audio_device.start`, `audio_decoder.valid`) are bare-name lexical collisions on generic
`valid(`, `native(`, `start(` tokens in unrelated deep tests — the D2 upward bias noted at
`docs/stdlib/REGRESSION_INVENTORY.md:222`. Critically, **every constructor is unexercised**,
so even the construction path (backend selection, device/decoder init, error mapping) has
no deep test. Even `audio_device.start`, the one non-`valid`/`native` credit, is a lexical
`start(` match and does not demonstrate successful device initialization. Shallow tests
(`tests/stdlib/audio_test.cp`, which uses `audio_context(1, &err)` null backend at line 93)
and demos exist but are excluded as evidence (`docs/stdlib/REGRESSION_INVENTORY.md:12,230`).

## 6. Environment blockers for execution on this host

- **No deep test to execute:** no `deep_*.cp` includes this header.
- **Dependency is vendored and present** (miniaudio, §4), so a missing library is not the
  blocker. The macOS audio frameworks are available on this darwin host.
- **Runtime audio backend:** real `ma_context_init` requires an audio device; on a
  headless/no-device host this fails. The header's design anticipates this: pass `use_null=1`
  to `audio_context` to select `ma_backend_null` (`stdlib/audio.hp:31-38`). A deep test
  intended for this host would have to use the null backend; the `[D]` credit for
  `audio_device.start` cannot be assumed to use it (no deep test exists to check).
- **Asset files:** `audio_sound`, `audio_engine.play`, and `audio_decoder` take file paths
  (`stdlib/audio.hp:69,168,274`); real execution needs decodable audio files (e.g. WAV) on
  disk — path values are not supplied by the header.
- **Hard-coded framework set:** the build uses macOS CoreAudio/AudioToolbox frameworks;
  that is host-specific and would need adaptation on non-Apple hosts.

I did not run any test or host process that executes the module; no executed behavior is
claimed.

## 7. What cannot be guaranteed

- Static inspection cannot guarantee any miniaudio call succeeds (context/engine/device/
  sound/decoder init, start/stop, volume, read).
- It cannot guarantee `set_volume` behaves safely for NaN/Inf/negative inputs
  (`stdlib/audio.hp:157-165`) or that unvalidated sample rate/channels (`:199-226`) are
  rejected.
- It cannot guarantee the `path == NULL` branch of `audio_decoder` reports an error
  (`:279-280` leaves the error record "ok") or that `frame_count`'s silent `0`
  (`:301-308`) is distinguishable from a genuinely empty stream.
- It cannot guarantee buffer capacity for `audio_decoder.read` (`:310-327`) or lifetime
  safety for borrowed `ma_context *`/`pUserData` and returned-by-value `audio_sound`
  (`:182-185`).
- It cannot guarantee any "exercised" credit reflects a real call: all 8 are lexical
  collisions and no deep test includes the header
  (`docs/stdlib/REGRESSION_INVENTORY.md:222,227,234`).
- No execution-based safety, regression, or runtime-success claim is made for this module.
