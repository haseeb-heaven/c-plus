# Safety audit — `stdlib/video.hp`

> **STATIC — SOURCE INSPECTION ONLY. NOT EXECUTED.** Nothing in this document was
> compiled, linked, injected, or run. No test runner was invoked. No runtime,
> live-game, or "safe in practice" claim is made anywhere in this audit.

## 1. Provenance and scope

| Item | Value |
| --- | --- |
| Branch | `agent/modern-stdlib` |
| Header inspected | `stdlib/video.hp` (300 lines, 26 definitions by source inspection) |
| Count authority | `docs/stdlib/REGRESSION_INVENTORY.md` (snapshot generated 2026-11-23 04:24 IST) |
| Enumeration method | `tests/stdlib/coverage.py::entries()` imported verbatim by the inventory (inventory §1, items 1–2) |
| Corpus | only `tests/stdlib/deep_*.cp` (28 files) + 5 sibling `deep_*.stdin` (inventory §1 item 3, §6 D8) |

All counts and exercised/unexercised names below are copied from the inventory, not
recomputed. The safety observations in §4 are STATIC reading of `stdlib/video.hp`
and the FFmpeg contracts it references; they are not verified behavior.

## 2. Public API surface — exact counts from the inventory

Inventory §3: `video.hp` = **25 public callable entries**.
Inventory §4: **3 exercised**. Inventory §5: **22 not exercised**.

Entries by class/group, with source line and inventory classification:

**Free functions**

| # | Entry | Source | Status (inventory) |
| --- | --- | --- | --- |
| 1 | `video_error_text` | `stdlib/video.hp:21-26` | NOT exercised |
| 2 | `video_codec_id` | `stdlib/video.hp:76-78` | NOT exercised |

**`frame`**

| # | Entry | Source | Status (inventory) |
| --- | --- | --- | --- |
| 3 | `frame.frame` (constructor) | `stdlib/video.hp:31-36` | NOT exercised |
| 4 | `frame.valid` | `stdlib/video.hp:38-40` | EXERCISED `[L]` |
| 5 | `frame.width` | `stdlib/video.hp:42-44` | NOT exercised |
| 6 | `frame.height` | `stdlib/video.hp:46-48` | NOT exercised |
| 7 | `frame.format` | `stdlib/video.hp:50-52` | NOT exercised |
| 8 | `frame.pts` | `stdlib/video.hp:54-56` | NOT exercised |
| 9 | `frame.drop` | `stdlib/video.hp:58-63` | NOT exercised |

**`decoder`**

| # | Entry | Source | Status (inventory) |
| --- | --- | --- | --- |
| 10 | `decoder.decoder` (constructor) | `stdlib/video.hp:89-136` | NOT exercised |
| 11 | `decoder.valid` | `stdlib/video.hp:138-140` | EXERCISED `[L]` |
| 12 | `decoder.width` | `stdlib/video.hp:142-144` | NOT exercised |
| 13 | `decoder.height` | `stdlib/video.hp:146-148` | NOT exercised |
| 14 | `decoder.next_frame` | `stdlib/video.hp:151-192` | NOT exercised |
| 15 | `decoder.drop` | `stdlib/video.hp:194-205` | NOT exercised |

**`media`**

| # | Entry | Source | Status (inventory) |
| --- | --- | --- | --- |
| 16 | `media.media` (constructor) | `stdlib/video.hp:213-242` | NOT exercised |
| 17 | `media.valid` | `stdlib/video.hp:244-246` | EXERCISED `[L]` |
| 18 | `media.stream_count` | `stdlib/video.hp:248-250` | NOT exercised |
| 19 | `media.video_index` | `stdlib/video.hp:252-254` | NOT exercised |
| 20 | `media.video_width` | `stdlib/video.hp:256-261` | NOT exercised |
| 21 | `media.video_height` | `stdlib/video.hp:263-268` | NOT exercised |
| 22 | `media.video_codec` | `stdlib/video.hp:270-277` | NOT exercised |
| 23 | `media.duration_ms` | `stdlib/video.hp:279-283` | NOT exercised |
| 24 | `media.open_decoder` | `stdlib/video.hp:286-289` | NOT exercised |
| 25 | `media.drop` | `stdlib/video.hp:291-297` | NOT exercised |

**Counting-fidelity observation (STATIC, not a recount).** Source inspection also
finds `video_stream_params` at `stdlib/video.hp:67-74`. Its signature spans two
lines, so it does not match the single-line `SIG` regex at
`tests/stdlib/coverage.py:24-25` and is absent from both inventory lists. The
official count of 25 is used verbatim as required; this note records the
discrepancy only.

## 3. Which APIs lack any deep test

- **22 of 25 entries are listed as unexercised** (inventory §5, `video.hp` row,
  reproduced in §2 above).
- The **3 credited as exercised** (`frame.valid`, `decoder.valid`, `media.valid`)
  are **bare-name matches**, not uses of `video`. No deep-test file includes
  `stdlib/video.hp` — a search of the 28-file corpus for `stdlib/video.hp` and for
  the tokens `frame`, `decoder`, and `media` returns no matches. `valid(` occurs in
  deep tests on *other* classes, and `coverage.py:87-88` matches unqualified names.
  This is the upward-bias caveat documented at inventory §6 D2, D7.
- Therefore, by source inspection, **all 25 entries lack a genuine deep test that
  opens a media file, constructs a decoder, or decodes a frame**. In particular,
  the central decode path `decoder.next_frame` (`:151-192`) and the stream/duration
  accessors (`:248-283`) have no deep-test evidence.

## 4. STATIC safety analysis — not executed

**Positive source-level guards observed**

- `video_error_text` (`:21-26`) checks `buf == NULL`/`size == 0` and force-terminates
  on `av_strerror` failure (`:24-25`).
- `video_stream_params` (`:67-74`) rejects a NULL format, negative stream, and
  stream index `>= nb_streams` before indexing `format->streams[stream]` (`:69-72`).
- `video_codec_id` (`:76-78`) maps NULL to `AV_CODEC_ID_NONE`.
- The `frame` constructor (`:31-36`) sets an error on `av_frame_alloc` failure;
  `valid` (`:38-40`) gates all field reads (`:42-56`); `drop` (`:58-63`) frees via
  `av_frame_free(&ptr)` and NULLs the pointer (idempotent).
- The `decoder` constructor (`:89-136`) uses a `do/break` error path: it rejects a
  NULL owner/negative stream (`:102-105`), checks `avcodec_find_decoder`, then
  cleans up the allocated codec with `avcodec_free_context` on an `open2` failure
  (`:120-126`) or `av_packet_alloc` failure (`:127-133`).
- `decoder::next_frame` (`:151-192`) resets the error, rejects invalid arguments,
  distinguishes `AVERROR_EOF`, `AVERROR(EAGAIN)`, and hard errors, uses a `draining`
  flag to flush exactly once (`:169-181`), skips non-matching stream packets
  (`:183-184`), and reports FFmpeg error text through `video_error_text`.
- `media` constructor (`:213-242`) closes the input with `avformat_close_input` if
  `avformat_find_stream_info` fails (`:230-237`); `drop` (`:291-297`) is idempotent.
- `duration_ms` (`:279-283`) guards `AV_NOPTS_VALUE` before dividing by the constant
  `AV_TIME_BASE/1000` (no divide-by-zero).
- Error strings built in stack buffers `why[256]` (`:93`, `:158`, `:215`) are copied
  into the error object's fixed buffer by `error_fail_cat`
  (`stdlib/error.hp:607-624`), so no dangling pointer to a local is retained.

**Conditions the wrapper does not check — relied upon from FFmpeg or the caller**

- **Borrowed-format lifetime (key STATIC risk).** `decoder` stores `owner_format`
  borrowed from `media` (`:81`, `:96`) and uses it in `next_frame` via
  `av_read_frame` (`:172`). If `media` is dropped before the `decoder`, the decoder
  holds a dangling `AVFormatContext *`. The wrapper does not keep `media` alive or
  track cross-object ownership; the usage order exists only in the header comment
  (`:3-4`) and `media::open_decoder` (`:286-289`).
- **`media(NULL, err)` is a silent failure** (STATIC observation). The NULL path
  check (`:221-222`) `break`s without calling `error_fail`, so the returned object
  is invalid while `err` remains cleared. This differs from the other constructors,
  which set an error on bad input.
- **No-video-stream files.** `av_find_best_stream` (`:238-239`) may return negative;
  `initialized` is still set to 1 (`:240`), so `valid()` returns true with
  `video_stream < 0`. Accessors then return safe defaults (`:256-277`), and
  `open_decoder` produces an error-bearing invalid decoder via `:102-105`. This is a
  semantic trap, not a memory-safety finding.
- `next_frame` writes into `out->native_frame` (`:159`) without verifying that the
  `frame` was allocated by this library or that it is not concurrently in use;
  FFmpeg is relied upon to resize buffers. `frame::native_frame` is a private
  member accessed directly (`:153`, `:159`), which depends on C+ lowering rather
  than standard C++ access rules.
- No cap on packets skipped in the `stream_index != stream` loop (`:183-184`);
  termination relies on reaching end-of-file.
- FFmpeg decoding is not globally serialized by this wrapper; thread-safety of
  concurrent calls on one `AVCodecContext` is outside its scope.
- Return-by-value of `decoder` from `open_decoder` (`:286-289`) relies on C+ move/RAII
  lowering; no explicit copy/move constructor is present in source.

## 5. External dependency requirements

| Dependency | Requirement | Provenance |
| --- | --- | --- |
| FFmpeg | `pkg-config libavformat libavcodec libavutil`; headers `libavcodec/avcodec.h`, `libavformat/avformat.h`, `libavutil/error.h`, `libavutil/imgutils.h` | `stdlib/video.hp:1,9,14-17`; `docs/MODERN_STDLIB.md:141` (FFmpeg 9.0.2, "system build", LGPL by default, GPL if `--enable-gpl`, "headers + pkg-config libav*, never copied") |
| C+ runtime | `cplus/cplus.h`, `stdlib/error.hp` | `stdlib/video.hp:18-19`; error helpers at `stdlib/error.hp:526-530,581-624` |

Licensing note from source: the linked FFmpeg build decides LGPL vs GPL terms
(`stdlib/video.hp:7-8`). Local environment check (not a test run): `pkg-config`
reports `libavformat 63.1.102`, `libavcodec 63.1.102`, `libavutil 61.1.102`, and
`/opt/homebrew/include/libavcodec/avcodec.h` exists. Dependency availability is not
evidence that any `video` API works.

## 6. Environment blockers

1. **No deep-test harness.** No `deep_*.cp` includes `stdlib/video.hp` (§3). The
   corpus is frozen at 28 `.cp` + 5 `.stdin` files (inventory §6 D8); producing real
   decode evidence requires adding tests and recomputing.
2. **No media fixture.** `tests/stdlib/data/` contains only `sample.json`; no
   `.mp4`/`.mkv`/`.avi`/`.webm` fixture exists anywhere in the repository (outside
   `build/` and `third_party/`). `media` construction and `next_frame` cannot be
   exercised without a real, decodable file.
3. **Codec availability.** `avcodec_find_decoder` (`:107`) succeeds only for codecs
   compiled into the host FFmpeg build; results are environment-dependent and were
   not probed by any test.
4. **No build/link target.** Root `CMakeLists.txt` builds `cplus_core`, `c+`, and
   `cplus_runtime` only (`CMakeLists.txt:87-98`, `:226-237`); CTest at `:268-273`
   runs `tests/run_tests.sh` and `demos/run_demos.sh`. No target in this repository
   compiles a `.hp` including FFmpeg headers or links `pkg-config libav*`.
5. **Licensing decision.** Whether the linked FFmpeg is LGPL or GPL (`:7-8`) is an
   external build configuration this audit cannot inspect or guarantee.

## 7. What cannot be guaranteed — no runtime safety claim

- This audit does **not** guarantee that `video.hp` is correct, memory-safe,
  hardened, leak-free, or regression-free. It records only what the source text
  appears to do and what the inventory records.
- The three "exercised" entries (§3) are name-only credits; **no runtime behavior of
  any `video` API was observed**, including the entire demux/decode path.
- The borrowed-format lifetime risk (§4), silent-NULL-path behavior, and
  no-video-stream semantics are STATIC observations requiring execution or formal
  analysis to confirm or refute; they are not findings proven at runtime.
- Per the inventory's scope statement (`docs/stdlib/REGRESSION_INVENTORY.md:232-234`),
  the underlying call-site inventory is static and is not a safety proof; this
  document inherits that limitation and adds no execution evidence.
