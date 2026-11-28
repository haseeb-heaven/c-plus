/* Modern C+ raylib 2D GUI demo. Proof mode writes a real PNG screenshot. */
#include "cplus/cplus.h"
#include "../raylib/include/raylib.h"
#include <stdlib.h>

class Box2D {
    int x;
    int y;
    int clicks;

    Box2D(int px, int py) {
        this->x = px;
        this->y = py;
        this->clicks = 0;
    }

    void move(int dx, int dy) {
        this->x += dx;
        this->y += dy;
    }

    void set(int px, int py) {
        this->x = px;
        this->y = py;
    }

    void draw(void) {
        DrawRectangle(this->x, this->y, 90, 60, (Color){220, 70, 60, 255});
    }
};

int main(void) {
    const int proof = getenv("RAYLIB_PROOF") != (char *)0;
    InitWindow(640, 480, "C+ raylib 2D shapes");
    SetTargetFPS(60);
    Box2D box = Box2D(100, 100);
    int frames = 0;

    while (!WindowShouldClose()) {
        if (IsKeyDown(KEY_RIGHT)) box.move(3, 0);
        if (IsKeyDown(KEY_LEFT)) box.move(-3, 0);
        if (IsKeyDown(KEY_DOWN)) box.move(0, 3);
        if (IsKeyDown(KEY_UP)) box.move(0, -3);
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)) box.clicks++;
        if (proof) { if (frames >= 3) break; box.set(120 + frames * 100, 140); }

        BeginDrawing();
        ClearBackground((Color){16, 20, 28, 255});
        DrawText("MODERN C+ / RAYLIB 2D", 16, 16, 20, (Color){120, 220, 255, 255});
        box.draw();
        DrawCircle(320, 300, 48, (Color){60, 180, 120, 255});
        DrawCircleLines(320, 300, 70, (Color){240, 220, 80, 255});
        EndDrawing();
        frames++;
        if (proof && frames == 3) TakeScreenshot("out/shapes2d.png");
    }
    print("RAYLIB_2D frames=", frames, " box=", box.x, ",", box.y);
    CloseWindow();
    return frames >= 3 ? 0 : 1;
}
