# gui_app — headless widget app (SDL3 dummy driver)

**Source:** `examples/gui_app/gui_app.cp` (headers `stdlib/widgets.hp`,
`stdlib/error.hp`; SDL3 only for the hidden render frame).
**APIs used:** widget tree build, integer grid layout, two fixed hit tests,
tree serialization with CRC32, an optional real SDL3 frame under
`SDL_VIDEODRIVER=dummy`, and `error *` fault injection for null buffers, bad
geometry, duplicate ids, oversized/control-character labels and bad indices.

| platform | binary | transcript(s) | screenshot(s) | exit |
| --- | --- | --- | --- | ---: |
| macOS arm64 | `binaries/macos/gui_app` | `transcripts/macos/gui_app.txt` | `screenshots/gui_app-macos.svg` | 0 |
| Windows PE (Highball/Wine) | `binaries/windows/gui_app.exe` | `transcripts/windows/gui_app.txt`, `transcripts/windows/gui_app_describe0.txt` | `screenshots/gui_app-windows.svg` | 0 |

**Exact marker lines — like-for-like `describe 0` invocation:**
- macOS (`./gui_app describe 0`): `GUI_RESULT describe:0,0,0 OK`
- Windows (`highball run -v ./gui_app.exe describe 0`, in
  `transcripts/windows/gui_app_describe0.txt`): `GUI_RESULT describe:0,0,0 OK`

The default Windows transcript `transcripts/windows/gui_app.txt` runs with no
arguments and therefore records a defined, clean refusal (still exit 0):

```
GUI_REFUSED tag=describe:0,0,0 category=1 code=108 message=describe: missing or invalid arguments
GUI_RESULT describe:0,0,0 REFUSED 108
```

This is an invocation difference, not a platform difference: the matching
`describe 0` run produces the same `OK` marker on both.  The screenshot
`screenshots/gui_app-windows.svg` reflects the default no-arg transcript;
read the transcript header for its exact command line.  Highball/Wine stderr
here can contain transient bottle noise (msync bootstrap,
`winemenubuilder.exe`, driver-load errors); those lines are not app failures
and are stripped from the marker transcript.
