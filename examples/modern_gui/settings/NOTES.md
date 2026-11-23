# Modern GUI settings panel — headless teaching app (not a proof)

This example drives a **real Nuklear settings form** (text edit field, wrapped
integer slider, two flag check boxes, Apply button) through the official SDL3
renderer backend with `SDL_VIDEODRIVER=dummy` and a hidden software-rendered
window. `argv[1]` selects a fixed scenario, so no interactive input, display or
game loop is required. Every input is checked with `stdlib/error.hp`
(`error_reset`, `error_fail_cat`, `error::failed_now/category/code`) plus
`stdlib/string.hp` (`string_view::is_valid_utf8`, `string::substr`) **before**
any state is committed; refusals leave the state at its `UNSET` sentinel.

> **Explicit scope statement:** this is a headless **teaching app** and runtime
> **refusal evidence**, not a proof of GUI correctness, visual fidelity, memory
> safety or absence of invalid states. The dummy driver produces no visible UI
> and the transcript is a deterministic observation of one process. The checked
> calls do not establish full pointer provenance or prevent a caller from
> bypassing them. The `OK`/`REFUSED` lines show that specific hostile inputs are
> rejected with stable categories and `cplus_exit_code` values; nothing more.

## Files

| File | Purpose |
| --- | --- |
| `settings_panel.cp` | Nuklear form, scenario table, checked validation, headless render, transcript |
| `run.sh` | builds with the exact flags and runs each scenario under `timeout 30`; prints `GUI_SETTINGS <scenarios> <passed> <failed> <refused>` |
| `settings_panel` | build artifact (binary) |

## Scenarios and expected outcomes

| Scenario | Input | Expected |
| --- | --- | --- |
| `apply` | text `guest`, slider `42`, flags `3` | `APPLIED code=0 cat=ok` plus 2 rendered frames and a 320x240 readback |
| `text_oversize` | 40-byte text into a 31-byte field + NUL | `REFUSED code=101 cat=bounds` |
| `slider_range` | slider `101` (max `100`) | `REFUSED code=101 cat=range` |
| `null_context` | `apply_settings((gui *)0, ...)` | `REFUSED code=102 cat=invalid` |
| `null_text` | `text == NULL` | `REFUSED code=102 cat=invalid` |
| `invalid_utf8` | overlong two-byte sequence `C0 80` | `REFUSED code=108 cat=utf8` |

Codes: `101 = CPLUS_EXIT_BOUNDS`, `102 = CPLUS_EXIT_NULL_POINTER`,
`108 = CPLUS_EXIT_UNSUPPORTED`. Categories are the stable `STDLIB_CATEGORY_*`
values rendered by `stdlib_category_name`.

The app exits `0` only when the observed outcome, code, category and
state-untouched check match the scenario's expectation; it exits `1` otherwise
(verified negative control: an unknown scenario prints
`SETTINGS unknown_scenario FAIL code=108 cat=invalid unknown_scenario` and
exits `1`).

## Exact commands (from repo root)

`build/ext/libnuklear.a` already exists here (compiled once from
`third_party/nuklear/nuklear_impl.c` by the repository's test runner), so no
Nuklear rebuild was needed. `run.sh` only compiles that source if the archive
is absent, using `pkg-config --cflags sdl3` and `-I third_party/nuklear`.

```sh
# Exact build command used (pkg-config values on this host:
#   cflags: -I/opt/homebrew/include
#   libs:   -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3)
./build/c+ build examples/modern_gui/settings/settings_panel.cp \
  --strict --sanitize=none -I. \
  -I/opt/homebrew/include -Ithird_party/nuklear \
  -Lbuild/ext -lnuklear \
  -L/opt/homebrew/lib -Wl,-rpath,/opt/homebrew/lib -lSDL3 \
  -o examples/modern_gui/settings/settings_panel

# One fixed scenario per process (run.sh performs exactly these six):
SDL_VIDEODRIVER=dummy timeout 30 ./examples/modern_gui/settings/settings_panel apply
SDL_VIDEODRIVER=dummy timeout 30 ./examples/modern_gui/settings/settings_panel text_oversize
SDL_VIDEODRIVER=dummy timeout 30 ./examples/modern_gui/settings/settings_panel slider_range
SDL_VIDEODRIVER=dummy timeout 30 ./examples/modern_gui/settings/settings_panel null_context
SDL_VIDEODRIVER=dummy timeout 30 ./examples/modern_gui/settings/settings_panel null_text
SDL_VIDEODRIVER=dummy timeout 30 ./examples/modern_gui/settings/settings_panel invalid_utf8

# All-in-one driver:
examples/modern_gui/settings/run.sh
```

The C+ driver emits five benign `clang: warning: ... unused ... argument`
lines from an internal compile-only pass (linker flags are not consumed in that
pass); the final link succeeds and the binary is produced (checked with
`file`: Mach-O 64-bit executable arm64).

## Exact output observed (not fabricated)

Build: no diagnostics other than the five benign warnings listed above;
`build_exit=0`.

`examples/modern_gui/settings/run.sh` output:

```text
SETTINGS apply APPLIED code=0 cat=ok field=text len=5 max=31 text=guest slider=42 flags=3 frames=2 windows=2 surface=320x240
SETTINGS text_oversize REFUSED code=101 cat=bounds field=text len=40 max=31
SETTINGS slider_range REFUSED code=101 cat=range field=slider value=101 min=0 max=100
SETTINGS null_context REFUSED code=102 cat=invalid field=context
SETTINGS null_text REFUSED code=102 cat=invalid field=text is_null=1
SETTINGS invalid_utf8 REFUSED code=108 cat=utf8 field=text len=2 max=31
GUI_SETTINGS 6 6 0 5
```

`run.sh` exit status: `0`. Two consecutive `run.sh` invocations produced
byte-identical transcripts (`diff` clean, `DETERMINISTIC_OK`).

Single-process form (same six scenario names as argv) was also run with
`SDL_VIDEODRIVER=dummy timeout 30` and produced the same six `SETTINGS` lines
and exit `0`.
