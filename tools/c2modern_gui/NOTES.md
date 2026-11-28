# c2modern_gui — headless Nuklear front-end for the C → modern-C converter

## What this is (and is not)

**This is a headless teaching GUI wrapping a conservative converter. It is NOT a
production IDE.** It demonstrates the C+ stdlib GUI/window/file stack
(`stdlib/window.hp`, `stdlib/gui.hp`, `stdlib/file.hp`) with a deterministic,
headless-testable core.

**The converter cannot guarantee semantics.** A textual C → modern-C rewrite
does not prove behaviour preservation. Even if the CLI converter were ready, a
"successful" report would mean only that a conservative transformation produced
output — not that the result is observationally equivalent to the input. Treat
every report as advisory and verify converted code independently (build it, run
its tests, inspect the diff).

All runs use `SDL_VIDEODRIVER=dummy timeout 30`, require no display, perform no
subprocess execution, and read no wall clock. There is no interactive input
requirement: `argv[1..]` select fixed scenarios.

## PENDING status (explicit declaration)

`tools/c2modern/convert.sh` is owned by a sibling component and is **not ready
yet**. Per the task's fallback, `gui_convert(input_path, output_path)` **models
its report deterministically** rather than invoking the CLI converter, and marks:

- `status=PENDING`
- `build_result=NOT_RUN`
- `run_result=NOT_RUN`

No `cplus_system()` call is made, so the program does not depend on the hardened
libc archive and the build needs only the exact flags below. The model is a
pure function of its two path arguments plus the committed fixture bytes:
`source_lines` / `source_bytes` are read from the input `.c` file via
`stdlib/file.hp::read_line` (sum of newline-stripped line lengths). Identical
fixture bytes ⇒ identical report. When the real CLI converter lands, the
`sibling-owner` can replace the body of `gui_convert()` with a `cplus_system()`
invocation (linking `build/libc/libcplus_hardened.a`); the validation, widget
layout, transcripts and refusal behaviour do not change.

## Files

- `converter_gui.cp` — the C+ program (window.hp + gui.hp + file.hp). Core:
  `gui_convert()`, `c2_describe()`, guarded refusals. Commands: `describe <id>`,
  `convert <in> <out>`, `render <w> <h> <id>`, `refuse <case>`.
- `run.sh` — builds, runs 23 fixed scenarios twice each (determinism + empty
  stderr + exact expected line), prints `C2GUI <scenarios> <passed> <failed>
  <refused>` as the last matching line, exits non-zero on any failure.
- `fixtures/{hello,util,main}.c` — committed fixed inputs (owned by this dir).
- `results/` — generated binaries, logs and per-case `.out`/`.err` transcripts.

## Exact commands and results

Build (repository root, `build/ext/libnuklear.a` already present):

```sh
./build/c+ build tools/c2modern_gui/converter_gui.cp --strict --sanitize=none \
  -I. $(pkg-config --cflags --libs sdl3) -Ithird_party/nuklear \
  -Lbuild/ext -lnuklear -o tools/c2modern_gui/results/converter_gui
```

Build result: **success**. The compiler emits only benign clang warnings that
the `-lSDL3`/`-lnuklear`/`-L...` linker inputs are unused during the
compile-only phase (the C+ driver links in a later phase) — no errors, binary
produced. Example runtime transcript (one run):

```sh
$ SDL_VIDEODRIVER=dummy timeout 30 tools/c2modern_gui/results/converter_gui describe 0
C2GUI_UI id=0 title=convert-hello widgets=input:field,convert:button,output:field,status:label,build:label,run:label input=tools/c2modern_gui/fixtures/hello.c output=tools/c2modern_gui/results/hello.cpp status=PENDING build=NOT_RUN run=NOT_RUN source_lines=2 source_bytes=61
C2GUI_RESULT describe:0 OK 0

$ SDL_VIDEODRIVER=dummy timeout 30 tools/c2modern_gui/results/converter_gui render 640 480 0
C2GUI_UI id=0 title=convert-hello widgets=input:field,convert:button,output:field,status:label,build:label,run:label input=tools/c2modern_gui/fixtures/hello.c output=tools/c2modern_gui/results/hello.cpp status=PENDING build=NOT_RUN run=NOT_RUN source_lines=2 source_bytes=61
C2GUI_RENDER 640 480 0 shown 1 widgets 8 frame 1
C2GUI_RESULT render:640,480,0 OK 0

$ SDL_VIDEODRIVER=dummy timeout 30 tools/c2modern_gui/results/converter_gui convert tools/c2modern_gui/fixtures/hello.c out.cpp
C2GUI_REPORT input=tools/c2modern_gui/fixtures/hello.c output=out.cpp status=PENDING build=NOT_RUN run=NOT_RUN source_lines=2 source_bytes=61
C2GUI_RESULT convert OK 0
```

Representative refusals (all print `REFUSED` with a stable category + code):

```sh
$ SDL_VIDEODRIVER=dummy timeout 30 tools/c2modern_gui/results/converter_gui refuse empty-input
C2GUI_REFUSED empty-input category 1 code 108 message c2gui: empty path
C2GUI_RESULT refuse empty-input REFUSED 108

$ SDL_VIDEODRIVER=dummy timeout 30 tools/c2modern_gui/results/converter_gui refuse oversized
C2GUI_REFUSED oversized category 5 code 101 message c2gui: path exceeds 256 bytes
C2GUI_RESULT refuse oversized REFUSED 101

$ SDL_VIDEODRIVER=dummy timeout 30 tools/c2modern_gui/results/converter_gui refuse non-c
C2GUI_REFUSED non-c category 1 code 108 message c2gui: input must be a .c file
C2GUI_RESULT refuse non-c REFUSED 108

$ SDL_VIDEODRIVER=dummy timeout 30 tools/c2modern_gui/results/converter_gui refuse null-context
C2GUI_REFUSED null-context category 1 code 102 message c2gui: null GUI context
C2GUI_RESULT refuse null-context REFUSED 102
```

Full harness:

```sh
$ sh tools/c2modern_gui/run.sh > /tmp/cgui_full.log 2>&1; echo "exit=$?"
exit=0
$ grep '^C2GUI ' /tmp/cgui_full.log
C2GUI 23 7 0 16
$ grep -c '^FAIL' /tmp/cgui_full.log
0
```

23 scenarios: 7 passed (pure-core descriptions, real headless Nuklear frames,
no-display core), 0 failed, 16 refused (empty/oversized/non-.c/null paths,
null context, out-of-range id, missing file, bad window dimensions, unknown
case/command). Every scenario was run twice with byte-identical stdout and empty
stderr. All reported passes are from actual executions above.

## Stdlib APIs exercised

`sdl`, `window`, `renderer`, `gui` (Nuklear `nk_begin`/`nk_edit_string_zero_terminated`/
`nk_button_label`/`nk_label`), `file` (checked `read_line`), `error`
(`error_fail_cat`, stable `STDLIB_CATEGORY_*` and `CPLUS_EXIT_*` codes).
Every hostile input is refused before any state write; refusals are expected
demonstrated outcomes (the program still exits 0 so the transcript is fixed).
