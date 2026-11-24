# 3D game demo — `binaries/scene3d_demo`

Source: `examples/games3d/scene3d_demo.cp`
Libraries used: `stdlib/math3d.hp`, `stdlib/scene3d.hp`, `stdlib/mesh3d.hp`

## What it proves

A deterministic 3D scene is built from an 8-vertex/12-triangle mesh, placed in
a 7-node scene graph, transformed with quaternion/matrix math, and rendered as
an ASCII transcript with world-space AABBs and a projected occupancy grid.

## Evidence

- Binary: `binaries/scene3d_demo` (Mach-O arm64, SHA-256 in `SHA256SUMS.txt`)
- Build: `transcripts/games3d.build.log`
- Run: `transcripts/games3d.run.txt`

Exact final lines:

```
CHECKSUM fnv1a32=b61bdf2b bytes=1779 lines=40
SCENE_DONE status=OK nodes=7 meshes=3
EXIT=0
```

The full transcript is byte-identical to
`examples/games3d/expected/transcript.expected`.

Refusal cases exercised by the owning suite (`examples/games3d/run.sh`, 13
cases): `NAN`/`INF` → `E_NONFINITE`, bad indices → `E_INDEX`, `NODE 9` →
`E_BOUNDS`, `CYCLE` → `E_CYCLE`, malformed tokens → `E_PARSE`.

## Reproduce

```sh
sh examples/games3d/run.sh
sh proof/games_apps/run.sh
```

No GPU is required; this is a deterministic scene/transcript proof.
