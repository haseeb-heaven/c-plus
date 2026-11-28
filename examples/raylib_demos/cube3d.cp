/* Modern C+ raylib 3D demo: real GPU cube with an orbital camera. */
#include "cplus/cplus.h"
#include "../raylib/include/raylib.h"
#include <stdlib.h>

class Scene3D {
    Camera3D camera;
    int frames;

    Scene3D(void) {
        this->camera.position = (Vector3){6.0f, 5.0f, 6.0f};
        this->camera.target = (Vector3){0.0f, 0.0f, 0.0f};
        this->camera.up = (Vector3){0.0f, 1.0f, 0.0f};
        this->camera.fovy = 60.0f;
        this->camera.projection = CAMERA_PERSPECTIVE;
        this->frames = 0;
    }

    void tick(int proof) {
        if (!proof) UpdateCamera(&this->camera, CAMERA_ORBITAL);
        else this->camera.position.x = 6.0f + (float)this->frames * 0.5f;
        this->frames++;
    }

    int done(void) { return this->frames > 3; }

    void draw(void) {
        ClearBackground((Color){14, 18, 30, 255});
        BeginMode3D(this->camera);
        DrawCube((Vector3){0, 0, 0}, 2, 2, 2, (Color){80, 160, 240, 255});
        DrawCubeWires((Vector3){0, 0, 0}, 2, 2, 2, (Color){255, 255, 255, 255});
        DrawGrid(10, 1.0f);
        EndMode3D();
        DrawText("REAL 3D - GPU RASTERIZED", 12, 12, 18, (Color){240, 240, 160, 255});
    }
};

int main(void) {
    const int proof = getenv("RAYLIB_PROOF") != (char *)0;
    InitWindow(640, 480, "C+ raylib real 3D");
    SetTargetFPS(60);
    Scene3D scene = Scene3D();
    while (!WindowShouldClose()) {
        if (proof && scene.done()) break;
        scene.tick(proof);
        BeginDrawing();
        scene.draw();
        EndDrawing();
        if (proof && scene.frames == 3) TakeScreenshot("out/cube3d.png");
    }
    print("RAYLIB_3D frames=", scene.frames);
    CloseWindow();
    return scene.frames >= 3 ? 0 : 1;
}
