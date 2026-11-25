# games3d — deterministic 3D scene demo

**Source:** `examples/games3d/scene3d_demo.cp` (headers `stdlib/math3d.hp`,
`stdlib/mesh3d.hp`, `stdlib/scene3d.hp`, `stdlib/error.hp`,
`stdlib/alloc_iface.hp`).
**APIs used:** cube mesh build and AABB, a fixed scene graph, axis-angle to
quaternion to `mat4` conversion, composed world transforms, camera
projection, top-down ASCII occupancy grid, and an FNV-1a 32-bit checksum over
every emitted byte.  Hostile argv cases (`NAN`, `INF`, `INDEX`, `NODE`,
`CYCLE`, `DEGENERATE`, `PARSE`) produce defined refusals.  This is not an
interactive game and it does not touch a GPU.

| platform | binary | transcript | screenshot | exit | exact final marker line |
| --- | --- | --- | --- | ---: | --- |
| macOS arm64 | `binaries/macos/games3d` | `transcripts/macos/games3d.txt` | `screenshots/games3d-macos.svg` | 0 | `SCENE_DONE status=OK nodes=7 meshes=3` |
| Windows PE (Highball/Wine) | `binaries/windows/games3d.exe` | `transcripts/windows/games3d.txt` | `screenshots/games3d-windows.svg` | 0 | `SCENE_DONE status=OK nodes=7 meshes=3` |

Both transcripts also contain the identical self-check line
`CHECKSUM fnv1a32=b61bdf2b bytes=1779 lines=40`.  macOS sets
`SDL_VIDEODRIVER=dummy`; Windows uses `highball run -v` with the two MinGW
DLLs beside the exe.  `run.sh` re-runs this demo on both platforms and checks
both marker lines plus exit 0.
