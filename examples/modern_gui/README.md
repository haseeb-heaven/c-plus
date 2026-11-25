# Modern Nuklear / gfx GUI examples

Four small apps that exercise the modern C+ GUI/graphics stdlib adapters
(`stdlib/window.hp`, `stdlib/graphics.hp`, `stdlib/gui.hp`) with a **real**
Nuklear context and the official SDL3 renderer backend.  Every app is built
with the supported `./build/c+` driver and the vendored
`third_party/nuklear/` headers plus the prebuilt `build/ext/libnuklear.a`.

> **These are headless teaching apps, not production GUI applications.** Each
> one is driven entirely by fixed `argv`/`stdin` scenarios and is run with
> `SDL_VIDEODRIVER=dummy`, so a hidden software-rendered window produces a
> byte-identical transcript.  There is no interactive event loop, no wall
> clock, no network and no real user input.  A group named "games" describes
> the *model* it demonstrates, not a claim to be a playable game.  The apps
> are runtime **refusal evidence** for checked stdlib usage — not a proof of
> memory safety, GUI correctness, or freedom from all invalid states.  See
> [Non-claims](#non-claims).

## Run all groups

```sh
sh examples/modern_gui/run_all.sh
```

The aggregate runner discovers `<group>/run.sh` at depth 2 and
`<group>/<subgroup>/run.sh` at depth 3 below this directory, runs each from the
repository root under `timeout -k 10 300` with a restricted `PATH`,
`CPLUS_CLANG=/usr/bin/clang` and `SDL_VIDEODRIVER=dummy`, captures the **last**
final-report line from each group, prints a table, and exits non-zero on any
real failure.  It continues after failures and never masks a failure with a
success override.  Per-group logs land in `build/modern_gui/<runner>.log`; the
verbatim final lines are saved to `build/modern_gui/final_lines.txt`.

### Final-line contract (for group authors)

Each `run.sh` must print, as its last matching line, exactly one line beginning
with its token:

| Runner | Token |
| --- | --- |
| `dashboard/run.sh` | `GUI_DASHBOARD` |
| `settings/run.sh` | `GUI_SETTINGS` |
| `games/breakout_ui/run.sh` | `GFX_BREAKOUT` |
| `games/invaders_ui/run.sh` | `GFX_INVADERS` |

Recommended shape (the positional form these runners emit):

```
TOKEN <scenarios> <passed> <failed> <refused>
```

The aggregate parser also accepts `TOKEN total=<n> pass=<n> fail=<n>
[blocked=<n>]` and a bare status word.  A third positional count greater than
zero, `fail=<n>` with `n > 0`, the words `FAIL`, `MISMATCH`, `ERROR`,
`BUILD_FAIL`, and a non-zero exit status are real failures.  `BLOCKED` /
`blocked=<n>` is reported but is not a hard failure, matching the repository
evidence convention.  Group-specific detail belongs in that group's
`NOTES.md`.

## Common build convention

Build from the repository root, after `build/c+` and `build/ext/libnuklear.a`
exist.  The backend is pinned by the aggregate runner to `/usr/bin/clang`;
individual build commands may obtain SDL3 flags from `pkg-config sdl3`.

| Item | Value on the verification host |
| --- | --- |
| SDL3 | 3.4.18 (`/opt/homebrew/lib/libSDL3.dylib`) |
| Nuklear headers | `third_party/nuklear/nuklear.h`, `nuklear_sdl3_renderer.h` |
| Nuklear backend archive | `build/ext/libnuklear.a` (from `third_party/nuklear/nuklear_impl.c`) |

## Catalogue

### Dashboard — `GUI_DASHBOARD` (`dashboard/run.sh`)

| | |
| --- | --- |
| Source | `examples/modern_gui/dashboard/dashboard.cp` |
| One-line purpose | Render a fixed three-label / three-button overview, operations or diagnostics panel through a real Nuklear context and emit one headless SDL3 frame. |
| APIs | `stdlib/window.hp` (`sdl`/`window`/`renderer` ctor, `*_native`, `*_drop`); `stdlib/graphics.hp`; `stdlib/gui.hp` (`gui_ctor`, `gui_label`, `window_begin`/`window_end`, `gui_render`); `stdlib/error.hp` (`error_fail_cat`, `error_reset`, `stdlib_set_error_callback`); `NK_EDIT`/Nuklear labels and buttons via `gui::native()`. |
| Safety refusals | Zero or negative window dimensions, dimensions above `DASH_MAX_DIM` (4096), out-of-range scenario id, null GUI context, unknown `refuse` case, unknown command. Each prints a stable `DASH_REFUSED ... category <n> code <n> message ...` line. |
| Build | `./build/c+ build examples/modern_gui/dashboard/dashboard.cp --strict --sanitize=none -I. $(pkg-config --cflags --libs sdl3) -Ithird_party/nuklear -Lbuild/ext -lnuklear -o examples/modern_gui/dashboard/results/dashboard` |

### Settings panel — `GUI_SETTINGS` (`settings/run.sh`)

| | |
| --- | --- |
| Source | `examples/modern_gui/settings/settings_panel.cp` |
| One-line purpose | Drive a real Nuklear settings panel (text field, check boxes, slider) headlessly; validate every input before any state is written. |
| APIs | `stdlib/window.hp`, `stdlib/graphics.hp`, `stdlib/gui.hp` (`gui::slider`, borrowed Nuklear context from `gui::native()`, `nk_edit_string_zero_terminated`); `stdlib/string.hp` (`string_view`, checked length/copy); `stdlib/error.hp` (`error_fail_cat` with `STDLIB_CATEGORY_BOUNDS/IO/MEMORY/UTF8/INVALID/RANGE`, `error_reset`). |
| Safety refusals | Oversized/unterminated text, out-of-range slider value, invalid flags bitmask, bad UTF-8, null window or renderer context — each leaves state at its sentinel and returns a typed `error *` with a stable category and `cplus_exit_code`. |
| Build | `./build/c+ build examples/modern_gui/settings/settings_panel.cp --strict --sanitize=none -I. $(pkg-config --cflags sdl3) -Ithird_party/nuklear -Lbuild/ext -lnuklear $(pkg-config --libs sdl3) -o examples/modern_gui/settings/settings_panel` |

### Breakout HUD — `GFX_BREAKOUT` (`games/breakout_ui/run.sh`)

| | |
| --- | --- |
| Source | `examples/modern_gui/games/breakout_ui/breakout_ui.cp` |
| One-line purpose | Step a fixed-seed Breakout model with a bounded brick pool, render a real SDL3 playfield plus a Nuklear HUD, and echo the same score/bricks/paddle transcript. |
| APIs | `stdlib/window.hp`, `stdlib/graphics.hp` (`renderer_set_color`, `renderer_clear`, `renderer_present`, `renderer_read` pixel probe), `stdlib/gui.hp` (`gui_label`, `window_begin`/`window_end`), `stdlib/vector.hp` (bounded bricks), `stdlib/string.hp` (checked token parsing), `stdlib/error.hp`. |
| Safety refusals | `--probe-null` guards a null surface and null Nuklear context; bad grid dimensions (`4<=W<=32`, `1<=H<=8`, `W*H<=256`), oversized command token (>64 chars), too many ticks (>128), unknown command. Each is a clean exit 2 with a `REFUSED ... code ... category ...` line. |
| Build | The group `run.sh` first builds the backend locally when absent: `/usr/bin/clang -c third_party/nuklear/nuklear_impl.c -o examples/modern_gui/games/breakout_ui/bin/nuklear.o -O2 -Ithird_party/nuklear $(pkg-config --cflags sdl3)` then `ar rcs examples/modern_gui/games/breakout_ui/bin/libnuklear.a examples/modern_gui/games/breakout_ui/bin/nuklear.o`; then `./build/c+ build examples/modern_gui/games/breakout_ui/breakout_ui.cp --strict --sanitize=none -I. $(pkg-config --cflags sdl3) $(pkg-config --libs sdl3) -Ithird_party/nuklear -Lexamples/modern_gui/games/breakout_ui/bin -lnuklear -o examples/modern_gui/games/breakout_ui/bin/breakout_ui` |

### Invaders HUD — `GFX_INVADERS` (`games/invaders_ui/run.sh`)

| | |
| --- | --- |
| Source | `examples/modern_gui/games/invaders_ui/invaders_ui.cp` |
| One-line purpose | Step a fixed-seed space-invaders model with a bounded bullet pool, sample two real SDL3 surface pixels, and reproduce the same fixed HUD lines in the transcript. |
| APIs | `stdlib/window.hp` (`sdl_ctor`, `window_ctor`, `renderer_ctor`, `window_native`, `renderer_native`, `*_drop`), `stdlib/graphics.hp` (`renderer_set_color`, `renderer_clear`, `renderer_read`, `renderer_present`), `stdlib/gui.hp` (`gui_ctor`, `gui_label`, `gui_input_begin`/`gui_input_end`, `gui_render`, `gui_drop`), `stdlib/error.hp` (`error_ctor`, `error_fail`, `error_code`/`error_category`/`error_what`). |
| Safety refusals | `--null-surface` and `--null-context` fault injection, oversized token, unknown command, invalid `--bullets` capacity (1..16); each is a clean exit 2 with a `REFUSED ... code ... category ...` line. |
| Build | `./build/c+ build examples/modern_gui/games/invaders_ui/invaders_ui.cp --sanitize=none $(pkg-config --cflags --libs sdl3) -I. -Ithird_party/nuklear -Lbuild/ext -lnuklear -o examples/modern_gui/games/invaders_ui/bin/invaders_ui` (the group builds a local `bin/libnuklear_local.a` and uses that `-L` path instead only if `build/ext/libnuklear.a` is absent) |

## Non-claims

- This directory does **not** claim Rust equivalence, a 100%-safe application,
  or a production GUI toolkit. The apps are deterministic models whose value is
  the observable, reproducible refusal transcript.
- Results are recorded for **macOS arm64 / SDL3 3.4.18 only** (Apple clang
  16.0.0, `/usr/bin/clang`). No other platform, SDL3 version, video driver or
  renderer backend is claimed to behave identically.
- A clean `build/` or emulator-style check is not live-game or live-display
  success; all frames here are produced against `SDL_VIDEODRIVER=dummy`.
- See [RESULTS.md](RESULTS.md) for the exact final table, toolchain, totals and
  reproduction commands from a real run.
