# RED20 / red_multimedia — FINDINGS

Agent 9/20, branch `agent/modern-stdlib`.  Attack target: the seven multimedia
headers `stdlib/{graphics,gui,image,video,window,input,audio}.hp`.
**Only `tests/redblue20/red_multimedia/` was written** — no source/header edits,
no `git add`/`commit`.

## 1. Method (honest summary)

`harness.cp` generates **2520 deterministic cases** (42 kinds × 60 ids, fixed
SplitMix64 seed `0x9D2C5680A5B4F1E7`; no clock, PID or address in a
classification).  **Every case runs in a forked child** with `RLIMIT_CORE=0`
and a 2 s watchdog; a pipe carries a `struct report` back before `_exit()`, so
a crash cannot kill the harness.  Canary brackets (0xA5/0x5A/0x3C/0xC3/0x7E/
0xE7) surround every byte/int/float out-region.  Precedence is
`CRASH > CORRUPT > UNEXPECTED_OK > REFUSED`:

- **CRASH** — child died by signal (`WIFSIGNALED`), or exited without a report.
- **CORRUPT** — child survived but a canary bracket changed.
- **UNEXPECTED_OK** — child survived, canaries intact, but a hostile input was
  nonetheless reported as success by the API.
- **REFUSED** — input was refused, or produced a defined, safe result, with all
  canaries intact.

Build uses the **exact flags from**
`tests/stdlib/method_tests/multimedia/run_all.sh` (`--legacy-c
--sanitize=none`, SDL3 + stb + miniaudio(+Apple frameworks) + Nuklear +
FFmpeg).  Runtime uses `SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`,
miniaudio's explicit null backend, and `timeout 60`.

Reproduce with:

```sh
sh tests/redblue20/red_multimedia/run.sh
```

## 2. Authoritative result

Two consecutive runs produced the identical classification vector and
byte-identical scenario logs apart from the timing column:

```
RED20 multimedia 2520 24 33 180 2283
```

| class | count | of which genuine defect |
|---|---:|---|
| CRASH | 24 | 24 (7 + 9 + 8, see §3.3–§3.5) |
| CORRUPT | 33 | 33 (see §3.2) |
| UNEXPECTED_OK | 180 | 120 genuine (§3.1) + 60 defined SDL behaviour (§4) |
| REFUSED | 2283 | — |

Wall time ≈ 19 s of case work (`sum` of the `ms` column in `scenarios.log`),
well inside the 60 s bound; harness exit status `0`.

Per-kind non-REFUSED breakdown (60 cases per kind):

| kind | header | attack | outcome |
|---:|---|---|---|
| 1 | window | NULL title | 60 × UNEXPECTED_OK (defined SDL behaviour — not a defect) |
| 14 | image | absurd dimensions (`INT_MAX × INT_MAX × 4`, `2 × INT_MAX × 4`) | 7 × CRASH (SIGSEGV) |
| 22 | audio | `audio_decoder(NULL)` | 60 × UNEXPECTED_OK |
| 24 | audio | `audio_decoder::read` count > buffer capacity | 33 × CORRUPT |
| 28 | gui | `gui::slider` with no active window/layout | 9 × CRASH (SIGABRT) |
| 32 | video | `media(NULL)` | 60 × UNEXPECTED_OK |
| 38 | video | decoder used after its `media` owner is dropped | 8 × CRASH (SIGSEGV) |

Evidence files: `results/scenarios.log` (`id kind outcome observed ms`, one
line per case), `results/harness.out`, `results/harness.err` (`FIND` lines),
`results/digests.sha256`.  The raw log digest changes between runs only because
of the timing column; the **classification stream** (`id kind outcome observed`,
field 5 removed) is byte-identical across runs and hashes to SHA-256
`fa4d2104a5ba053c2b20b21ec161a9e86fec3b73ff150bc5f7fe244c821a2bbb`.

---

## 3. Genuine findings

### 3.1 Silent success on a NULL path in `audio_decoder` and `media` — 180 cases

**Severity: medium.  Files:** `stdlib/audio.hp:279-280`, `stdlib/video.hp:221-222`.

Both constructors do:

```c
error_reset(err);
...
if (path == NULL)
    break;            /* no error_fail() */
```

so `valid()` is 0 but `err.ok()` stays **1** and `failed_now()` is **0**.  A
caller that follows the library's own documented pattern (construct, then
check the error) believes construction succeeded.  Focused repro output:

```
audio_decoder(NULL): valid=0 err.ok=1 failed_now=0
media(NULL):        valid=0 err.ok=1 failed_now=0
image(NULL):        valid=0 err.ok=1 failed_now=0 (DOCUMENTED)
```

`image.hp:28-29` explicitly documents the no-error NULL-path behaviour, and
`decoder_from_memory` (`audio.hp:342-344`) *does* record an error for a NULL
buffer.  The two constructors above have no such documented exemption, so the
inconsistency is a real contract gap.  Kinds 22 and 32 contribute 60 cases each
(ids 22, 64, 106, … and 32, 74, 116, …).

### 3.2 `audio_decoder::read` has no buffer-capacity bound — 33 CORRUPT cases

**Severity: high (memory safety).  File:** `stdlib/audio.hp:311-327`.

`read(float *frames, unsigned long long frame_count, error *err)` forwards
`frame_count` straight to `ma_decoder_read_pcm_frames()` with no maximum and no
capacity parameter.  With a **256-float guarded buffer** and a valid in-memory
WAV, the harness requests `frame_count = 64 + (id-seed % 512)`; when that
exceeds 256 the decoder writes past the buffer and overwrites the trailing
0xC3 guard.  Example: id 66 (`frame_count = 443`), id 150 (`frame_count = 351`).
33 of 60 kind-24 cases are CORRUPT; the other 27 (count ≤ 256) are REFUSED.

Honest caveat: the API signature makes the caller responsible for sizing
`frames`, so this is an unbounded-output design hazard rather than a violation
of an explicit capacity promise.  The header offers neither a capacity
parameter nor a documented cap, so a hostile or miscomputed `frame_count`
silently corrupts caller memory while `err.ok()` remains 1.

### 3.3 `gui::slider` aborts the process outside a window/layout — 9 CRASH cases

**Severity: medium.  File:** `stdlib/gui.hp:141-145`.

`slider()` checks `valid()`, `value != NULL` and `minimum <= maximum`, then
unconditionally calls `nk_slider_int()`.  Nuklear asserts `ctx->current` at
`third_party/nuklear/nuklear.h:26607` when no window/layout is active, so a
perfectly legal call sequence terminates the process:

```
Assertion failed: (ctx->current), function nk_slider_float, file nuklear.h, line 26607.
FIND id=70 kind=28 CRASH observed=6      # signal 6 = SIGABRT
```

9 of 60 kind-28 cases abort (ids 70, 196, 322, 448, 784, 1036, 1330, 1540,
2170).  Other widget methods (`button`, `label`, `widget_bounds`) are not
affected in the same way; `slider` should check the active layout/window state
before delegating.

### 3.4 Decoder outlives `media` owner → use-after-free — 8 CRASH cases

**Severity: high (lifetime).  File:** `stdlib/video.hp:286-289` (`open_decoder`).

`media::open_decoder` returns a `decoder` that borrows `owner_format` without
retaining or referencing it.  Dropping the `media` before the `decoder` and
then calling `next_frame()` calls `av_read_frame()` on a freed `AVFormatContext`:

```
FIND id=38 kind=38 CRASH observed=11     # signal 11 = SIGSEGV
```

8 of 60 kind-38 cases crash (ids 38, 290, 1214, 1466, 2180, 2222, 2390, 2474).
This requires the caller to violate an ownership ordering, but the header
provides no guard, even though the error API already has `CPLUS_EXIT_OWNERSHIP`
and `CPLUS_EXIT_USE_AFTER_FREE` codes for exactly this situation.

### 3.5 Absurd dimensions accepted by `image_from_pixels` → SIGSEGV — 7 CRASH cases

**Severity: medium.  File:** `stdlib/image.hp:146-173` (checks at 155-160,
`malloc` at 163, `memcpy` at 168).

The overflow checks only detect *size_t wrap*.  A byte count that fits in
`size_t` but is physically absurd (`INT_MAX × INT_MAX × 4` ≈ 1.84e19, or
`2 × INT_MAX × 4` ≈ 17 GB) passes them; `malloc` can return an overcommitted
pointer and the unchecked `memcpy` from the caller's 4096-byte source buffer
then reads out of bounds:

```
FIND id=434 kind=14 CRASH observed=11    # signal 11 = SIGSEGV
```

7 of 60 kind-14 cases crash (ids 434, 812, 896, 1064, 1106, 1400, 1820).
The harness deliberately supplies an undersized source for an implied multi-
gigabyte request, so caller input is also at fault; still, a checked public
API should reject implausible dimensions/totals instead of attempting the
allocation and copy.

---

## 4. UNEXPECTED_OK that is *not* a defect (honest classification)

Kind 1 (60 cases) marks `window(NULL, 16, 16, 0, &err)` as UNEXPECTED_OK because
the harness expected a refusal.  SDL3 explicitly defines a NULL window title as
an empty title, so `valid() == 1` here is **defined, safe behaviour**.  These 60
cases are reported as UNEXPECTED_OK for transparency but are **not** a header
bug.  Excluding them, the genuine UNEXPECTED_OK count is **120** (§3.1).

## 5. What was attacked and safely refused (2283 cases)

Zero findings were produced by: zero/negative/`INT_MIN`/`(int)SIZE_MAX` window
dimensions; NULL window handles and NULL out-parameters; window/surface/
renderer/gui/audio/video double-drop and post-drop method calls; out-of-range
and extreme surface pixel coordinates (including `INT_MIN`/`INT_MAX` and a
4-byte-output guard proving `surface::pixel` writes exactly four bytes);
invalid low-bpp pixel formats (RGB24/RGB565/INDEX8/UNKNOWN) and a crafted short
pitch; NULL renderer and all renderer methods; NaN/inf/huge rect and line
coordinates; malformed SDL events, out-of-range scancodes and guarded
mouse/keyboard state; invalid `image_from_pixels` dimensions/channels (including
channels > 4 passed to PNG/BMP writers, which stb correctly rejects);
truncated/garbage image and audio buffers; truncated MP4 (`moov atom not found`
is refused by FFmpeg); NULL decoder/frame/stream parameters; out-of-range
stream indices; and null/degenerate audio-device rates and channel counts.

## 6. Limitations

- This is deterministic contract probing, not proof of absence of other bugs.
- Crash-heavy kinds 14, 28 and 38 are exercised on 1 in 8 ids to keep the whole
  suite under `timeout 60`; the reported counts are exact for the shipped
  seed and the attack is still reproduced on every qualifying id.
- Some crashes (SIGABRT from Nuklear, SIGSEGV from UAF/oversized memcpy) are
  caused by an adversarial caller sequence; that is stated per finding rather
  than presented as an unqualified API contract breach.
- The timing column in `scenarios.log` varies between runs; all classifications
  and the `RED20` counts are deterministic.
- Everything ran headless (SDL dummy video/audio + miniaudio null backend); no
  live-game or display result is claimed.
