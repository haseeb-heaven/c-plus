# Modern GUI dashboard example — verification notes

## Scope and honesty statement

**This is a headless-rendered teaching application, not a production GUI.** It
exists to show the safe *shape* of a C+ immediate-mode GUI program:

- `stdlib/window.hp` — `sdl` process lifetime, `window`, `renderer`;
- `stdlib/gui.hp` — RAII-style `gui` wrapping a **real** Nuklear context and its
  official SDL3 renderer backend (label / layout / button / render calls);
- `stdlib/error.hp` — recoverable `error *` refusals for hostile inputs.

There is no event loop, no interactive input, no application logic, no
persistence, no networking and no wall clock. The "buttons" are emitted as real
Nuklear widgets for one frame, but the ON/OFF states in the transcript come from
a fixed scenario table, not from user clicks. Rendering happens through SDL's
`dummy` video driver, so **no physical display is required or used**.

Every `DASH_REFUSED ...` line is **runtime evidence**: it is produced by an
explicit check in this C+ source or by the checked stdlib wrapper, observed at
run time with `--sanitize=none`. It is not a static assertion, a sanitizer
report, or a claim of proven memory safety.

All inputs are fixed argv scenarios. Output is byte-identical across runs.

## Files

| file | purpose |
| --- | --- |
| `dashboard.cp` | the app: deterministic pure core (`describe`), one-frame headless render (`render`), and `error *` refusal cases (`refuse`) |
| `run.sh` | builds and runs 17 fixed scenarios (valid + hostile), checks exit codes, exact lines, empty stderr and run-to-run determinism; prints `GUI_DASHBOARD <scenarios> <passed> <failed> <refused>` |
| `NOTES.md` | this file |

Generated outputs and the binary land in `apps/legacy_examples/modern_gui/dashboard/results/`.

## Build

Verified from the repository root
`/Users/haseeb-mir/Documents/Code/Cpp/c-plus-stdlib` on branch
`agent/modern-stdlib`. The external Nuklear library already exists at
`build/ext/libnuklear.a`; `run.sh` refuses to proceed if it is missing rather
than fabricating a pass.

Exact build command (the one that works):

```sh
./build/c+ build apps/legacy_examples/modern_gui/dashboard/dashboard.cp --strict --sanitize=none -I. $(pkg-config --cflags --libs sdl3) -Ithird_party/nuklear -Lbuild/ext -lnuklear -o apps/legacy_examples/modern_gui/dashboard/dashboard
```

Result: exit 0, binary created. The only compiler diagnostics are benign clang
"argument unused during compilation" warnings for the link-only flags:
`-Wl,-rpath,/opt/homebrew/lib`, `-lSDL3`, `-lnuklear`, `-L/opt/homebrew/lib`,
`-Lbuild/ext`. They are passed to the linker by the same invocation.

Per `stdlib/gui.hp`, `third_party/nuklear/nuklear_impl.c` is compiled **once**
into the archive. If it ever needs rebuilding, the repository's canonical
command (from `tests/stdlib/run_stdlib_tests.sh`) is:

```sh
clang -c third_party/nuklear/nuklear_impl.c -o build/ext/nuklear.o -O2 \
  -Ithird_party/nuklear $(pkg-config --cflags sdl3)
ar rcs build/ext/libnuklear.a build/ext/nuklear.o
```

`third_party/` is not modified by this example.

## Command interface

| argv | meaning |
| --- | --- |
| `describe <id>` | pure core path: no SDL init, no display; prints the fixed `DASH_UI` description for scenario `id` (0, 1, 2) |
| `render <w> <h> <id>` | full path: SDL3 dummy window + renderer + real Nuklear context, one frame with the scenario's labels/buttons, render, then drop all handles in reverse order |
| `refuse <case>` | drives a specific `error *` refusal: `zero-width`, `negative-height`, `oversized`, `bad-scenario`, `null-context`, or an unknown case |

Every invocation is a fixed transcript and exits 0; refusals are expected,
demonstrated outcomes, not unhandled failures.

## Exact outputs (run from the repository root)

### Pure core, with no display environment at all

```sh
env -u SDL_VIDEODRIVER timeout 30 ./apps/legacy_examples/modern_gui/dashboard/dashboard describe 2
```

stdout:

```
DASH_UI id=2 title=diagnostics labels=TEMP,FAN,PWR buttons=ACK:ON,MUTE:ON,RESET:OFF
DASH_RESULT describe:2,0,0 OK 0
```

stderr empty, exit 0. This is the "testable without a real display" path: it
never calls `SDL_Init`.

### One real headless frame

```sh
SDL_VIDEODRIVER=dummy timeout 30 ./apps/legacy_examples/modern_gui/dashboard/dashboard render 640 480 0
```

stdout:

```
DASH_UI id=0 title=overview labels=CPU,MEM,NET buttons=RUN:ON,STOP:OFF,RESET:ON
DASH_RENDER 640 480 0 shown 1 widgets 6 frame 1
DASH_RESULT render:640,480,0 OK 0
```

`shown 1 widgets 6 frame 1` means `nk_begin` succeeded, three labels and three
buttons were emitted, and exactly one frame was rendered and presented before
the handles were dropped. stderr empty, exit 0.

### Refusals (runtime evidence)

```sh
SDL_VIDEODRIVER=dummy timeout 30 ./apps/legacy_examples/modern_gui/dashboard/dashboard refuse null-context
```

stdout:

```
DASH_REFUSED null-context category 1 code 108 message gui: invalid window or renderer
DASH_RESULT refuse null-context REFUSED 108
```

```sh
SDL_VIDEODRIVER=dummy timeout 30 ./apps/legacy_examples/modern_gui/dashboard/dashboard render 100000 100000 0
```

stdout:

```
DASH_UI id=0 title=overview labels=CPU,MEM,NET buttons=RUN:ON,STOP:OFF,RESET:ON
DASH_REFUSED render:100000,100000,0 category 2 code 101 message dashboard: window dimensions exceed DASH_MAX_DIM
DASH_RESULT render:100000,100000,0 REFUSED 101
```

The remaining refusals observed in the aggregate run:

```
DASH_REFUSED render:0,480,0 category 1 code 108 message dashboard: window dimensions must be positive
DASH_REFUSED render:640,-5,0 category 1 code 108 message dashboard: window dimensions must be positive
DASH_REFUSED describe:99,0,0 category 2 code 101 message scenario: id out of range
DASH_REFUSED zero-width category 1 code 108 message dashboard: window dimensions must be positive
DASH_REFUSED negative-height category 1 code 108 message dashboard: window dimensions must be positive
DASH_REFUSED oversized category 2 code 101 message dashboard: window dimensions exceed DASH_MAX_DIM
DASH_REFUSED bad-scenario category 2 code 101 message scenario: id out of range
DASH_REFUSED mystery category 1 code 108 message refuse: unknown case
DASH_REFUSED command category 1 code 108 message dashboard: unknown command
```

Dimension and scenario-id checks run before any SDL call. The null-context case
constructs `gui` with two NULL native handles; the wrapper refuses instead of
dereferencing them. All refusals come from `error_fail_cat` / the checked
wrapper, with `--sanitize=none`.

### Aggregate runner

```sh
sh apps/legacy_examples/modern_gui/dashboard/run.sh
```

It rebuilds the binary, runs 17 scenarios (5 success descriptions/renders, 11
hostile refusals, 1 no-display core check) twice each under `timeout 30`,
compares stdout byte-for-byte with `cmp`, requires empty stderr and exit 0, and
finishes with:

```
GUI_DASHBOARD 17 6 0 11
```

Exit 0. Full per-scenario stdout is in `apps/legacy_examples/modern_gui/dashboard/results/`.

## Refusal codes used

| code | name | meaning here |
| --- | --- | --- |
| 108 | `CPLUS_EXIT_UNSUPPORTED` | non-positive window dimension, NULL GUI context, unknown case/command (category `invalid` = 1) |
| 101 | `CPLUS_EXIT_BOUNDS` | dimension above `DASH_MAX_DIM` (4096), out-of-range scenario id (category `range` = 2) |

## Non-claims

- No production GUI, event loop, interactivity or application behaviour is
  implemented or implied.
- Matching output for these fixed inputs says nothing about other inputs or
  longer executions. Refusals are observed runtime outcomes for the listed
  cases only.
- No claim is made that C+ is "safer than Rust" or 100% memory-safe. This is
  one small, reproducible demonstration of checked wrappers and recoverable
  errors.
