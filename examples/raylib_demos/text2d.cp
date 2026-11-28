/* Modern C+ raylib text demo. Proof mode writes a real PNG screenshot. */
#include "cplus/cplus.h"
#include "../raylib/include/raylib.h"
#include <stdlib.h>

class TextScene {
    int frames;

    TextScene(void) { this->frames = 0; }

    void tick(void) { this->frames++; }

    int done(void) { return this->frames > 3; }

    void draw(void) {
        ClearBackground((Color){12, 16, 30, 255});
        DrawText("MODERN C+ / RAYLIB TEXT", 30, 40, 30, (Color){100, 220, 255, 255});
        DrawText("real GPU-backed GUI window", 30, 100, 20, (Color){130, 220, 200, 255});
        DrawText("class methods drive the frame", 30, 160, 20, (Color){160, 200, 230, 255});
        DrawRectangle(30, 300, 560, 80, (Color){30, 40, 60, 255});
        DrawText("C+ COMPILED RAYLIB APP", 46, 326, 22, (Color){255, 220, 100, 255});
    }
};

int main(void) {
    const int proof = getenv("RAYLIB_PROOF") != (char *)0;
    InitWindow(640, 480, "C+ raylib text");
    SetTargetFPS(60);
    TextScene scene = TextScene();
    while (!WindowShouldClose()) {
        if (proof && scene.done()) break;
        BeginDrawing();
        scene.draw();
        EndDrawing();
        scene.tick();
        if (proof && scene.frames == 3) TakeScreenshot("out/text2d.png");
    }
    print("RAYLIB_TEXT frames=", scene.frames);
    CloseWindow();
    return scene.frames >= 3 ? 0 : 1;
}
