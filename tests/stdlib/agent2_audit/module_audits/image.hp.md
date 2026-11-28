# Safety audit — `stdlib/image.hp` (image decode/encode via vendored stb)

> **LABEL: STATIC SOURCE INSPECTION ONLY — NOT EXECUTED.** No test runner, compiler, or
> host process was run to produce this document. All statements derive from reading
> `stdlib/image.hp` and the static inventory `docs/stdlib/REGRESSION_INVENTORY.md`. No
> behavior is claimed to be verified by execution. See §7.

## 1. Provenance and method

- Header inspected: `stdlib/image.hp` (203 lines). Scope comment at `stdlib/image.hp:1-10`;
  includes at `stdlib/image.hp:14-20` (stb headers at lines 19-20).
- Exact counts/unexercised names from `docs/stdlib/REGRESSION_INVENTORY.md`:
  - §3 total **10 public callable entries** (`docs/stdlib/REGRESSION_INVENTORY.md:46,59`).
  - §4 exercised **1 of 10** (`docs/stdlib/REGRESSION_INVENTORY.md:75,88`), name at
    `docs/stdlib/REGRESSION_INVENTORY.md:124`.
  - §5 not exercised **9 of 10** (`docs/stdlib/REGRESSION_INVENTORY.md:150,163`), names at
    `docs/stdlib/REGRESSION_INVENTORY.md:195`.
- Static, not execution — `docs/stdlib/REGRESSION_INVENTORY.md:6-15,232-234`.

## 2. Public API list with counts

The inventory's 10 entries, with header line numbers:

| Group | Entries (header lines) | Count |
|---|---|---|
| `image` | `image` ctor `:30-57`; `valid` `:59-62`; `width` `:64-66`; `height` `:68-70`; `channels` `:72-74`; `size_bytes` `:76-81`; `pixel` `:84-89`; `write_png` `:91-112`; `write_bmp` `:114-130`; `drop` `:132-140` | 10 |
| **Total** | | **10** |

**Parser omissions found by source inspection** (multi-line signatures missed by
`entries()`; real public callables):
- `image_from_pixels(const unsigned char *, int, int, int, error *)` at
  `stdlib/image.hp:143-173` (signature spans lines 146-147).
- `image_from_memory(const unsigned char *, int, int, error *)` at
  `stdlib/image.hp:175-201` (signature spans lines 176-177).

Source inspection therefore sees **12 public callables**, while the audited headline count
is the inventory's **10**.

## 3. Public surface safety analysis (STATIC — source inspection only)

- **`image` constructor:** zeroes fields, treats `path == NULL` as an empty image with no
  error (`:28-38`), validates `desired_channels` in `0..4` (`:39-43`), then calls
  `stbi_load` (`:44-45`). On decode failure it reports `stbi_failure_reason()` and resets
  dimensions (`:46-52`); on success with `desired_channels > 0` it pins
  `channels_in_use` to the request (`:53-55`).
- **`valid` / metadata:** `valid()` requires non-NULL pixels and positive cols/rows/
  channels (`:59-62`); `width`/`height`/`channels` return fields directly (`:64-74`).
  `size_bytes` multiplies `(size_t)cols * rows * channels` (`:76-81`) — this product is
  **not** overflow-checked here (unlike `image_from_pixels`, `:155-161`); it relies on stb
  having produced a self-consistent image.
- **`pixel` (borrowed pointer):** validates `valid()` and `0 <= x/y < dimensions`
  (`:84-86`), then computes `pixels + (y*cols + x) * channels` (`:87-88`). Returns NULL for
  out-of-range/invalid. No lock (stb images are plain buffers). The returned pointer is
  valid only while the image lives — the header states this at `:83`. The multiplication
  uses `size_t` and is safe only if `size_bytes` itself did not overflow.
- **`write_png`:** validates path and `valid()` (`:92-100`), checks
  `cols > INT_MAX / channels_in_use` for stride overflow (`:101-105`), then calls
  `stbi_write_png` with `stride = cols * channels_in_use` (`:106-107`). Failure maps to
  `CPLUS_EXIT_IO` (`:108-109`).
- **`write_bmp`:** validates path and `valid()` (`:114-123`) and calls `stbi_write_bmp`
  (`:124-125`); no explicit stride overflow check (bmp writer computes its own).
- **`drop`:** calls `stbi_image_free` and resets all fields (`:132-140`).
  **No copy/move control is declared**; the header explicitly says an image is move-only
  and must be transferred with `cplus_move` (`:143-145`). Copying the struct would
  double-free in `drop()`.
- **`image_from_pixels`:** validates source/dimensions (`:151-154`), checks the
  `width * channels * height` product against `SIZE_MAX` in two steps (`:155-161`),
  allocates with `malloc` (chosen to match `stbi_image_free`, `:162-164`), copies with
  `memcpy` (`:168`), and fills the fields (`:169-171`). This is the only routine here with
  explicit size overflow math.
- **`image_from_memory`:** validates `desired_channels 0..4`, non-NULL data, `size > 0`
  (`:180-188`), then `stbi_load_from_memory` (`:189-190`) with the same failure/dimension
  reset behavior as the path constructor (`:191-199`). The source buffer is not retained
  (`:175`).

All above is read from source; none of it has been exercised here.

## 4. External dependency requirements

- **stb_image / stb_image_write** (public domain / MIT), vendored in `third_party/stb`;
  included at `stdlib/image.hp:19-20`. Header comment requires building
  `third_party/stb/stb_impl.c` once and adding `third_party/stb` to the include path
  (`stdlib/image.hp:8-9`). Uses `stbi_load`, `stbi_load_from_memory`,
  `stbi_failure_reason`, `stbi_image_free`, `stbi_write_png`, `stbi_write_bmp`.
- **C+ runtime / `cplus/cplus.h`**, `stdlib/error.hp`, plus `<limits.h>`/`<stdlib.h>`/
  `<string.h>` at `stdlib/image.hp:14-18`.
- **Not a dependency of this module:** libuv, SDL3, libcurl, ffmpeg, miniaudio. ffmpeg is
  `stdlib/video.hp`; there is no image codec in this header — decoding is stb's job
  (`stdlib/image.hp:6`).
- **Host availability (static dependency query only, not a test):**
  `third_party/stb/stb_image.h`, `stb_image_write.h`, and `stb_impl.c` are all present.
  An existing compile-stage log `build/stdlib-tests/image_test.build.log` shows `-lstb`
  and the `build/ext` library path being passed (with "linker input unused" warnings,
  i.e. compile-only), and `build/ext` contains stb artifacts. Dependency presence is
  established; no executed behavior is implied.

## 5. APIs lacking any deep test

Per inventory §5 (`docs/stdlib/REGRESSION_INVENTORY.md:163,195`), the **9** unexercised
entries are: `image.image`, `image.width`, `image.height`, `image.channels`,
`image.size_bytes`, `image.pixel`, `image.write_png`, `image.write_bmp`, `image.drop`.

Additionally, source inspection finds the two helper constructors absent from the
inventory and uncredited: `image_from_pixels` (`:143-173`) and `image_from_memory`
(`:175-201`).

**Stronger source-inspection caveat:** no deep-corpus file includes `stdlib/image.hp`, and
grep finds no `image_from` token in any deep file. The single "exercised" credit
(`docs/stdlib/REGRESSION_INVENTORY.md:124`: `image.valid`) is a bare-name lexical collision
on the generic `valid(` token in an unrelated deep test — the D2 upward bias at
`docs/stdlib/REGRESSION_INVENTORY.md:222`. This is the weakest-covered module in the audited
set: only `valid()` is credited, and even that is spurious; there is no deep test of
construction, decoding, pixel access, encoding, or the overflow checks. Shallow tests
(`tests/stdlib/image_test.cp`) and demos exist but are excluded as evidence
(`docs/stdlib/REGRESSION_INVENTORY.md:12,230`).

## 6. Environment blockers for execution on this host

- **No deep test to execute:** no `deep_*.cp` includes this header; only a shallow test and
  a demo exist, and those are excluded from the deep corpus.
- **stb is vendored and present** (§4), so a missing library is not the blocker; the image
  build requires compiling `third_party/stb/stb_impl.c` once and adding the include path
  (`stdlib/image.hp:8-9`).
- **Asset files / buffers:** the path constructor and writers need real image files and a
  writable output path (`stdlib/image.hp:30,91,114`); `image_from_memory` needs a valid
  encoded buffer (`:175-201`). No such assets are supplied by the header, and repository
  policy for this workspace is to avoid relying on local game assets — a deep test would
  have to generate or embed its own fixtures.
- **Filesystem access:** `write_png`/`write_bmp` require a writable directory; otherwise
  they return `CPLUS_EXIT_IO` (`:108-109,126-127`). No environment hook supplies a path.
- **No display/network/audio dependency** — this module is the least environment-sensitive
  of the five, but still needs valid image data to do anything meaningful.

I did not run any test or host process that executes the module; no executed behavior is
claimed.

## 7. What cannot be guaranteed

- Static inspection cannot guarantee `stbi_load`, `stbi_load_from_memory`, or either
  writer succeeds for any given file/buffer/path.
- It cannot guarantee `size_bytes`'s unchecked product (`:76-81`) never overflows for
  arbitrary stb output, or that `pixel`'s `size_t` arithmetic (`:87-88`) is safe in that
  case — the overflow guard exists only in `image_from_pixels` (`:155-161`).
- It cannot guarantee a decoder never returns dimensions/channels that make the stride
  checks in `write_png` insufficient (`:101-107`), or that decode failure reasons are
  always non-NULL (`:47-49,192-193`).
- It cannot guarantee move-only discipline: a shallow copy of `image` would double-free
  via `drop` (`:132-140`); the header relies on callers using `cplus_move` (`:143-145`).
- It cannot guarantee the single "exercised" credit reflects a real call: it is a lexical
  collision and no deep test includes the header
  (`docs/stdlib/REGRESSION_INVENTORY.md:222,227,234`).
- No execution-based safety, regression, or runtime-success claim is made for this module.
