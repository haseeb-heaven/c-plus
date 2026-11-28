# olive.c demos (still frame + interactive SDL3)

A minimal, self-contained proof that the [olive.c](https://github.com/tsoding/olive.c)
single-header 2D software renderer builds and produces real framebuffer output
in this repository. Two demos are provided:

- `triangle_demo.c` - deterministic still frame written as a binary P6 PPM.
- `olive_sdl_demo.c` - interactive SDL3 window with mouse/keyboard controls;
  a fixed proof mode writes a real SDL framebuffer capture (BMP and PNG).

Neither needs assets, fonts or network access at run time.

## Provenance

Fetched 2026-11-28 with `gh api repos/tsoding/olive.c/contents/<path>` at
upstream commit `e2a3edb585ea643717f4f0d7f927ee83c9ec472c`; only these files
were used:

- `olive.c` - verbatim upstream single-header renderer.
- `LICENSE` - verbatim upstream license text.
- `triangle_demo.c` - a small adaptation of upstream `demos/triangle.c`.
- `olive_sdl_demo.c` - adaptation of the same shapes with an SDL3 platform layer.

Upstream `vc.c` (the interactive platform layer) was not used, nor were
`assets/`, `wasm/`, `docs/`, `fonts/` or the rest of the upstream repo.

## License

olive.c is Copyright 2022 Alexey Kutepov <reximkut@gmail.com> and MIT-licensed;
the full license text is in `LICENSE` and reproduced in the header of
`olive.c`. Both demo sources are derived from MIT-licensed upstream examples
and carry the same notice. The rest of this repository keeps its own licensing.

## Still-frame demo

```sh
sh examples/modern_c_ports/olive_demo/run.sh
```

Builds with the repository compiler (`CPLUS_BIN` overrides the path) and
falls back to plain clang. Prints:

```
OLIVE_DEMO PASS compiler=c+ bytes=230415 sha256=8c7784763ad0558d1783830f7116e01ad7f2036fa394f11018069ad0d001977d
```

## Interactive SDL3 demo

`olive_sdl_demo.c` renders the olive.c software framebuffer into an SDL3
texture and scales it to a 640x480 window. Controls:

| Input | Action |
| --- | --- |
| Mouse move | Move the circle |
| Left click or C | Cycle circle color |
| Space | Toggle the triangle |
| R | Reset color |
| Esc or close window | Quit |

Build and run interactively:

```sh
clang -std=c11 -O2 -Wall -Wextra -Werror -I. -I/opt/homebrew/include \
  olive_sdl_demo.c -o /tmp/olive_sdl -L/opt/homebrew/lib \
  -Wl,-rpath,/opt/homebrew/lib -lSDL3
/tmp/olive_sdl
```

`run_sdl.sh` verifies it without a display using SDL dummy video and a
fixed 4-frame proof sequence. It writes a real framebuffer capture
(`SDL_RenderReadPixels` to BMP, converted to PNG, 640x480) and checks the
exact markers, BMP size/magic and PNG SHA-256:

```
OLIVE_SDL_TEST 6 6 0
```

Both the repository compiler and the clang fallback produce identical
output. Generated files stay under `out/` and are not committed.
