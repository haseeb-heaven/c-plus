# widgets — headless widget toolkit core

**Source:** `tests/stdlib/widgets/test_widgets.cp` (headers
`stdlib/widgets.hp`, `stdlib/error.hp`, `stdlib/alloc_iface.hp`).
**APIs used:** widget tree construction, integer grid layout, hit testing,
tree serialization, canary-guarded windows (`pre[64]` / `post[64]`), null and
bad-geometry refusals, allocator lifecycle and drop behaviour.

| platform | binary | transcript | screenshot | exit | exact final marker line |
| --- | --- | --- | --- | ---: | --- |
| macOS arm64 | `binaries/macos/widgets` | `transcripts/macos/widgets.txt` | `screenshots/widgets-macos.svg` | 0 | `WIDGETS 144 0` |
| Windows PE (Highball/Wine) | `binaries/windows/widgets.exe` | `transcripts/windows/widgets.txt` | `screenshots/widgets-windows.svg` | 0 | `WIDGETS 144 0` |

The suite prints only on failure; a bare `WIDGETS 144 0` with empty stderr
means 144 checks and no failures.  No display is required — this exercises the
deterministic widget core, not a rendered GUI.
