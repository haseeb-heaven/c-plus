# olive.c still-frame demo

A minimal, self-contained proof that the [olive.c](https://github.com/tsoding/olive.c)
single-header 2D software renderer builds and produces a real framebuffer in
this repository. It renders one deterministic frame (background fill, smooth
triangle, translucent circle adapted from upstream `demos/triangle.c`) and
writes a binary P6 PPM to `out/frame.ppm`.

No windowing, assets, fonts, or network access are needed at run time.

## Provenance

Fetched 2026-11-28 with `gh api repos/tsoding/olive.c/contents/<path>` at
upstream commit `e2a3edb585ea643717f4f0d7f927ee83c9ec472c`; only these files
were used:

- `olive.c` — verbatim upstream single-header renderer.
- `LICENSE` — verbatim upstream license text.
- `triangle_demo.c` — a small adaptation of upstream `demos/triangle.c`:
  same shapes/colors, fixed pose instead of animation, plus PPM output and a
  checksum print. Upstream `vc.c` (the interactive platform layer) was not used.

`assets/`, `wasm/`, `docs/`, `fonts/` and the rest of the upstream repo were
not copied.

## License

olive.c is Copyright 2022 Alexey Kutepov <reximkut@gmail.com> and MIT-licensed;
the full license text is in `LICENSE` and reproduced in the header of
`olive.c`. `triangle_demo.c` is derived from the same MIT-licensed upstream
demo and carries the same notice. The rest of this repository keeps its own
licensing; nothing here changes it.

## Run and verify

```sh
sh examples/modern_c_ports/olive_demo/run.sh
```

`run.sh` prefers the repository compiler `build/c+` (`CPLUS_BIN` overrides the
path) and falls back to plain clang. C+ builds the vendored plain-C source in
`--legacy-c --sanitize=none -std=c11` mode; the clang fallback uses
`-std=c11 -O2 -Wall -Wextra -Werror`. No warnings are suppressed and no C ABI
is changed. Both paths currently produce byte-identical frames.

The runner checks the P6 magic, the exact byte count, the SHA-256 of the PPM
file, and the program's own byte count and FNV-1a64 line. It exits non-zero on
any mismatch, and prints:

```
OLIVE_DEMO bytes=230415 fnv1a64=878c7e60fc33e272 ppm=out/frame.ppm
OLIVE_DEMO PASS compiler=c+ bytes=230415 sha256=8c7784763ad0558d1783830f7116e01ad7f2036fa394f11018069ad0d001977d
```

With `CPLUS_BIN=/nonexistent sh run.sh` the last line reports
`compiler=clang` with the same checksum.

Generated files go into `out/` (also ignored by the repository root
`.gitignore`); nothing generated is committed. This is an offline renderer
check only — it is not a build, runtime, or live-game claim for any other
project in this repository.
