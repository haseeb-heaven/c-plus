# Modern Nuklear + SDL3 breakout UI (`breakout_ui`)

## Explicit scope statement

`breakout_ui.cp` is a **deterministic teaching game with a headless-rendered
HUD, not a full interactive game**. There is no keyboard/event loop, no wall
clock, no network, and no randomness beyond a fixed seed (`0xC0FFEE11`) used
only for the initial ball direction. Under `SDL_VIDEODRIVER=dummy` it renders
real frames through the SDL3 software renderer and draws a real Nuklear overlay
through the official Nuklear SDL3 renderer backend, then writes a byte-identical
transcript of score / remaining bricks / paddle state. The HUD text lines are
fed to Nuklear with `gui::label` and echoed to stdout as fixed `HUD ...` lines;
they are text evidence of the overlay, not an interactive UI.

The **refusals are runtime evidence**: each hostile invocation really calls
`error_fail` (an `error*` API) on an out-of-range paddle command, an oversized
brick-grid request, or a deliberately null `surface` handle / null Nuklear
context, prints a `REFUSED` line, and exits exactly 2. They are not simulated
or pre-baked output.

It combines:
- `stdlib/graphics.hp` — SDL3 `sdl`, `window`, `renderer`, `surface` (software
  renderer, frame readback, `surface::pixel` probe),
- `stdlib/gui.hp` — `gui` around Nuklear with the SDL3 renderer backend,
- `stdlib/vector.hp` / `stdlib/string.hp` — command and option parsing,
- `stdlib/error.hp` — `error` value plus `error_fail` for clean refusals,
- a deterministic breakout core: dynamic brick grid, paddle, ball, walls,
  brick destruction, life loss/reset, win/game-over detection.

## Input contract

```sh
bin/breakout_ui                       # built-in fixed sequence (31 commands)
bin/breakout_ui RLSRRLSS...           # one compact token
bin/breakout_ui R L S R ...           # one command per argv token
printf 'RLSR\n' | bin/breakout_ui -   # one command line from stdin
bin/breakout_ui --grid 12x4 RLSR      # custom brick grid (4<=W<=32, 1<=H<=8, W*H<=256)
bin/breakout_ui --probe-null          # null-handle guard probe (refuses)
bin/breakout_ui --help                # usage
```

Commands: `L`/`R` move the paddle, `S`/`.` stay still. Token length <= 64,
at most 128 ticks. Exit **0** on success, **2** on a clean refusal, **3** on an
unexpected SDL/Nuklear setup failure.

Transcript shape: `GFXBREAKOUT ...` header, `INIT ...`, then one `FRAME nnn
draw=OK hud=OK shot=OK painted=1` block plus four `HUD ...` lines per frame, a
`TICK ...` line per command, `FINAL ...`, and `GFXBREAKOUT-END frames=N
hud_lines=4*N`.

## Exact build commands (from the repository root)

Nuklear is built once into this example's own `bin/` directory, so no files
outside `examples/modern_gui/games/breakout_ui/` are written by this example.

```sh
clang -c third_party/nuklear/nuklear_impl.c \
  -o examples/modern_gui/games/breakout_ui/bin/nuklear.o -O2 \
  -Ithird_party/nuklear $(pkg-config --cflags sdl3)
ar rcs examples/modern_gui/games/breakout_ui/bin/libnuklear.a \
  examples/modern_gui/games/breakout_ui/bin/nuklear.o

./build/c+ build examples/modern_gui/games/breakout_ui/breakout_ui.cp \
  --strict --sanitize=none -I. \
  $(pkg-config --cflags --libs sdl3) \
  -Ithird_party/nuklear \
  -Lexamples/modern_gui/games/breakout_ui/bin -lnuklear \
  -o examples/modern_gui/games/breakout_ui/bin/breakout_ui
```

On this host `pkg-config --cflags --libs sdl3` expands to
`-I/opt/homebrew/include -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3`.
The C+ driver prints harmless clang warnings of the form
`clang: warning: ... '-lSDL3': 'linker' input unused` during the lowering
compile step; the final link succeeds and the build command exits 0.

## Exact runner command and output

```sh
$ examples/modern_gui/games/breakout_ui/run.sh
BUILD ok breakout_ui
RUN ok valid/default rc=0 frames=32
RUN ok determinism/default byte-identical
RUN ok valid/compact rc=0 frames=32
RUN ok valid/argv identical-to-compact
RUN ok valid/grid rc=0 frames=5
REFUSE ok hostile/badcmd rc=2
REFUSE ok hostile/oversized rc=2
REFUSE ok hostile/nullhandles rc=2
GFX_BREAKOUT 8 8 0 3
```

`run.sh` exit status: **0**. Every program run is wrapped in `timeout 30` with
`SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy`.

Fields of the final line:
- `8` scenarios total (5 valid + 3 hostile; the second default run is counted
  separately as the determinism scenario),
- `8` passed (all scenarios behaved as specified),
- `0` failed,
- `3` hostile scenarios correctly refused.

A valid run must also pass transcript checks: `GFXBREAKOUT`/`INIT`/`FRAME
000 draw=OK hud=OK shot=OK painted=1`/`FINAL`/`GFXBREAKOUT-END` present, no
`=FAIL` token, and `HUD` line count exactly `4 * frames`. The default transcript
is compared byte-for-byte across two runs and against the compact-token and
per-character-argv runs.

## Exact fixed-input run

```sh
$ SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 30 \
    examples/modern_gui/games/breakout_ui/bin/breakout_ui \
    'RLSRRLSSLLRRRLSRSSLLRRRLLSRRRLS'
```

Verbatim first 22 lines:

```
GFXBREAKOUT seed=0xC0FFEE11 grid=16x4 bricks=64 field=16x12 paddle_w=3 lives=3 cmds=31
INIT ball=(8,9) v=(+1,-1) paddle=[0,2] score=0 bricks=64
FRAME 000 draw=OK hud=OK shot=OK painted=1
HUD SCORE 000
HUD LIVES 3
HUD BRICKS 064
HUD PADDLE [0,2]
TICK 1 cmd=R ball=(9,8) v=(+1,-1) paddle=[1,3] event=none score=0 bricks=64 lives=3
FRAME 001 draw=OK hud=OK shot=OK painted=1
HUD SCORE 000
HUD LIVES 3
HUD BRICKS 064
HUD PADDLE [1,3]
TICK 2 cmd=L ball=(10,7) v=(+1,-1) paddle=[0,2] event=none score=0 bricks=64 lives=3
FRAME 002 draw=OK hud=OK shot=OK painted=1
HUD SCORE 000
HUD LIVES 3
HUD BRICKS 064
HUD PADDLE [0,2]
TICK 3 cmd=S ball=(11,6) v=(+1,-1) paddle=[0,2] event=none score=0 bricks=64 lives=3
FRAME 003 draw=OK hud=OK shot=OK painted=1
HUD SCORE 000
```

Verbatim final lines:

```
HUD SCORE 020
HUD LIVES 2
HUD BRICKS 062
HUD PADDLE [4,6]
FINAL outcome=incomplete ticks=31 score=20 bricks=62 paddle_hits=1 wall_hits=2 brick_hits=2 lives=2
GFXBREAKOUT-END frames=32 hud_lines=128
```

The complete byte-exact 195-line transcript is regenerated by `run.sh` at
`examples/modern_gui/games/breakout_ui/bin/valid.compact.out`.

Custom grid example (also a runner scenario):

```sh
$ SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 30 \
    examples/modern_gui/games/breakout_ui/bin/breakout_ui --grid 12x4 RLSR
GFXBREAKOUT seed=0xC0FFEE11 grid=12x4 bricks=48 field=12x12 paddle_w=3 lives=3 cmds=4
INIT ball=(6,9) v=(+1,-1) paddle=[0,2] score=0 bricks=48
...
FINAL outcome=incomplete ticks=4 score=0 bricks=48 paddle_hits=0 wall_hits=0 brick_hits=0 lives=3
GFXBREAKOUT-END frames=5 hud_lines=20
```

## Safety-refusal evidence

All three hostile runs exited **2** (not 124/134/139) under `timeout 30`, with
empty stdout and the exact stderr below — clean refusals, never timeouts or
crashes. The null-handle case additionally prints its guard summary to stdout.

| scenario | exact command | rc | stderr (verbatim) |
| --- | --- | --- | --- |
| out-of-range paddle command | `bin/breakout_ui X` | 2 | `REFUSED commands: out-of-range paddle command (expected L/R/S)` |
| oversized brick dimensions | `bin/breakout_ui --grid 9999x9999` | 2 | `REFUSED grid: oversized brick dimensions` |
| null surface / null Nuklear context | `bin/breakout_ui --probe-null` | 2 | `REFUSED null-surface: null surface handle` then `REFUSED null-context: null nuklear context` |

`--probe-null` stdout (verbatim):

```
GUARD surface=REFUSED context=REFUSED
GUARD-END refused=2
```

The guards call `error_fail` on a genuine `surface((SDL_Surface *)0)` and on a
genuine `gui((SDL_Window *)0, (SDL_Renderer *)0, ...)` whose `valid()` is false
and whose `native()` context is NULL. Additional refusals implemented and
checked manually: `bin/breakout_ui` with 129 one-character tokens exits 2 with
`REFUSED commands: too many commands (max 128 ticks)`, and an oversized command
token exits 2 with `REFUSED commands: token is oversized (max 64 characters)`.

## Verification summary

- [x] `--strict --sanitize=none -I.` C+ build with `pkg-config sdl3` and
      vendored Nuklear includes/library: exit 0, binary produced.
- [x] `run.sh`: `BUILD ok`, 5 valid scenarios exit 0 under `timeout 30`.
- [x] Transcript invariants: real frames (`draw=OK hud=OK shot=OK painted=1`),
      HUD line count `4 * frames`, no `=FAIL`, FINAL + END markers.
- [x] Determinism: default run byte-identical across two runs and against
      compact-token and per-character-argv runs (`cmp -s`).
- [x] 3 hostile scenarios: exit 2 with `REFUSED` lines, no crash/timeout.
- [x] `run.sh` final line `GFX_BREAKOUT 8 8 0 3`, exit status 0.
- [x] Stdin (`-`), custom grid (`--grid 12x4` / boundary `4x1`) and the
      128-tick limit exercised manually as documented above.
