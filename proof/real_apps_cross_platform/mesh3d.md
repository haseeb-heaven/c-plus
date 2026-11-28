# mesh3d — allocator-aware mesh construction

**Source:** `tests/stdlib/mesh3d/test_mesh3d.cp` (headers
`stdlib/mesh3d.hp`, `stdlib/error.hp`).
**APIs used:** cube build, vertex/triangle access, AABB and center, normal
generation (unit / outward / exact), validation (clean, degenerate, bad
index, restored), translate/scale, atomic refusal on NaN/Inf/overflow and
oversized vertex or triangle counts, `at()` bounds checks, `clear` and reuse,
custom allocator plus drop/double-drop.

| platform | binary | transcript | screenshot | exit | exact final marker line |
| --- | --- | --- | --- | ---: | --- |
| macOS arm64 | `binaries/macos/mesh3d` | `transcripts/macos/mesh3d.txt` | `screenshots/mesh3d-macos.svg` | 0 | `drop 1 1` |
| Windows PE (Highball/Wine) | `binaries/windows/mesh3d.exe` | `transcripts/windows/mesh3d.txt` | `screenshots/mesh3d-windows.svg` | 0 | `drop 1 1` |

This suite prints one tag per line with only `0`/`1` results, so its final
line is `drop 1 1` rather than a summary count; every emitted result is `1`
and stderr is empty.  The two transcripts are byte-identical on stdout.
