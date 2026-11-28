# games3d — headless 3D math demo (NOT a GPU/3D app)

This is a **deterministic headless algorithm test**, not an interactive 3D
application and not a GPU renderer. It builds a cube mesh, composes transforms
with math3d.hp and prints an ASCII transcript with a checksum. No window,
no GPU, no game assets.

Runner: `sh apps/games3d/run.sh` (prints `SCENE3D ...`).

For a **real GPU-rendered 3D app**, see `examples/raylib_demos/cube3d.cp`
(raylib `BeginMode3D`, `DrawCube`, orbital camera, real PNG framebuffer).
