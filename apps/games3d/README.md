# games3d — real 3D scene app (verified, not a demo)

This is a real, verified C+ application (not a demo). Runner: `sh apps/games3d/run.sh`.

Verified verdict: `SCENE3D 1 4 0 13` — 4 program checks passed, 0 failed,
13 defined refusals.

Screenshot generated from an actual run:

![games3d](../../proof/all_apps_games_screenshots/screenshots/games3d.svg)

Transcript + verifier: [`proof/all_apps_games_screenshots/games3d.md`](../../proof/all_apps_games_screenshots/games3d.md).

A small, GPU-free "game engine" app built entirely on the new additive
standard-library headers:

| header | role in this app |
| --- | --- |
| `stdlib/math3d.hp` | `vec3`, `quat`, `mat4`: axis-angle → quaternion → rotation matrix, look-at / perspective cameras, and finite-input refusals |
| `stdlib/mesh3d.hp` | an 8-vertex / 12-triangle cube mesh, normals, validation, world-space AABBs and bad-index / degenerate-face refusals |
| `stdlib/scene3d.hp` | stable-index scene graph (root → platform → three cubes, plus a lights group), transforms, preorder traversal and cycle refusal |

## Files

- `scene3d_app.cp` — the whole app (built-in scene + hostile CLI).
- `expected/transcript.expected` — byte-exact golden stdout for the no-argv run.
- `run.sh` — builds with the supported driver, checks exact output,
  determinism, checksum and every refusal case. Final line:
  `SCENE3D 1 <passed> <failed> <refused>`.

## What it prints

The no-argv run renders a fixed transcript:

1. cube mesh stats, base AABB and validation report;
2. camera parameters and the perspective matrix's key entries;
3. a preorder node table with depth, child counts, world translations,
   world AABBs, centers and NDC screen projections;
4. a 32×16 top-down ASCII XZ occupancy grid (`A`/`B`/`C` for the cubes);
5. `CHECKSUM fnv1a32=<hex> bytes=<n> lines=<n>` — FNV-1a over every emitted
   transcript line — followed by `SCENE_DONE status=OK`.

All floats are printed at fixed precision with negative zero normalized, so
the transcript and its checksum are byte-for-byte reproducible.

## Hostile inputs

Every hostile command exits 2 and prints both a `REFUSE code=…` line and
`SCENE_DONE status=REFUSED code=…`:

| command | refused because | code |
| --- | --- | --- |
| `NAN` | NaN position fed to `vec3_add` | `E_NONFINITE` |
| `INF` | infinite rotation axis fed to `quat_from_axis_angle` | `E_NONFINITE` |
| `INDEX <n>` | `mesh3d_vertex_at` with an out-of-range vertex | `E_INDEX` |
| `NODE <n>` | `scene3d_node_child_at` with an out-of-range child | `E_BOUNDS` |
| `DEGENERATE` | `mesh3d_add_triangle` with a repeated index | `E_INDEX` |
| `CYCLE` | `scene3d_node_reparent` under a descendant | `E_CYCLE` |
| `PARSE <token>` | malformed numeric token | `E_PARSE` |

## Build / run

From the repository root:

```sh
apps/games3d/run.sh
```

`run.sh` uses exactly:

```
./build/c+ build apps/games3d/scene3d_app.cp \
    --default --sanitize=none -I. -o apps/games3d/bin/scene3d_app \
    -- build/libc/libcplus_hardened.a
```

Every run is bounded by `timeout 30`.

## C+ lowering notes

Two small workarounds for current C+ lowering quirks are intentional:

- arrays of struct types and the 2D grid use literal extents rather than
  macro expressions (macro extents get wrapped in an invalid
  `CPLUS_NONNULL(...)` declaration / a bogus runtime bounds check);
- NaN/Inf are produced by floating division rather than union type-punning,
  which the lifetime checker rejects.

`--default` mode reports these as warnings but does not fail the build.

## Cross-platform screenshots (captured transcripts)

- macOS arm64: [games3d-macos.svg](../../proof/real_apps_cross_platform/screenshots/games3d-macos.svg)
- Windows PE32+ (Highball/Wine): [games3d-windows.svg](../../proof/real_apps_cross_platform/screenshots/games3d-windows.svg)

Full cross-platform proof:
[`proof/real_apps_cross_platform/`](../../proof/real_apps_cross_platform/README.md)
— `CROSS_PROOF 58 58 0`.