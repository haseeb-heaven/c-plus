# scene3d — stable-index scene graph

**Source:** `tests/stdlib/scene3d/test_scene3d.cp` (headers
`stdlib/scene3d.hp`, `stdlib/error.hp`, `stdlib/alloc_iface.hp`).
**APIs used:** scene init, node add/find, stable indices, depth and child
queries, traverse/subtree order, transform get/set, visibility, reparent,
remove, self-cycle and descendant-cycle refusal, duplicate and oversized
names, invalid parent, depth-limit chain, refusing/allocating-canary
allocators and `z_meta_balance` accounting.

| platform | binary | transcript | screenshot | exit | exact final marker line |
| --- | --- | --- | --- | ---: | --- |
| macOS arm64 | `binaries/macos/scene3d` | `transcripts/macos/scene3d.txt` | `screenshots/scene3d-macos.svg` | 0 | `SCENE3D 79 0` |
| Windows PE (Highball/Wine) | `binaries/windows/scene3d.exe` | `transcripts/windows/scene3d.txt` | `screenshots/scene3d-windows.svg` | 0 | `SCENE3D 79 0` |

The marker means 79 checks, 0 failures.  Canary lines (`i_canary_head 1`,
`i_canary_tail 1`, `i_canary_after_drop 1`) are part of that single final
marker's transcript; no guard-failure line appears on either platform.
