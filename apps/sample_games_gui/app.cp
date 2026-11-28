/* Real SDL3 GUI versions of the sample workloads: raycast room (doom),
 * block terrain (minecraft), interactive task list (todo). State is in fixed
 * slots; transient per-frame strings live on the C+ scoped data stack. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "cplus/cplus.h"
#include "stdlib/gfx_sdl3.hp"
#include "stdlib/scoped_mem.hp"

static const unsigned char MAP[8][8] = {
    {1,1,1,1,1,1,1,1},{1,0,0,0,0,0,0,1},{1,0,0,0,0,0,0,1},
    {1,0,0,1,1,0,0,1},{1,0,0,1,0,0,0,1},{1,0,0,0,0,0,0,1},
    {1,0,0,0,0,0,0,1},{1,1,1,1,1,1,1,1}
};

static void draw_doom(cplus_gfx_window *g, int angle) {
    int col;
    for (col = 0; col < 32; ++col) {
        double ray = (double)angle / 180.0 * 3.14159 + (col - 16) * 0.06;
        double dx = cos(ray), dy = sin(ray);
        double px = 3.5, py = 3.5, dist = 0;
        while (dist < 12.0) {
            int mx = (int)(px + dx * dist), my = (int)(py + dy * dist);
            if (mx < 0 || my < 0 || mx >= 8 || my >= 8 || MAP[my][mx]) break;
            dist += 0.1;
        }
        {
            int h = (int)(260.0 / (dist + 0.5));
            if (h > 260) h = 260;
            cplus_gfx_rect(g, 20 + col * 21, 210 - h, 21, h,
                           60 + col % 4 * 20, 110, 170);
        }
    }
}

static void draw_minecraft(cplus_gfx_window *g) {
    int x, y;
    for (x = 0; x < 24; ++x)
        for (y = 0; y < 8; ++y) {
            int ground = 5 + (x % 3);
            if (y >= ground)
                cplus_gfx_rect(g, 20 + x * 28, 90 + y * 22, 27, 21,
                               40 + (x % 4) * 25, 120 + (y % 3) * 20, 70);
        }
    cplus_gfx_rect(g, 148, 100, 22, 30, 250, 220, 60);
}

static void draw_todo(cplus_gfx_window *g, const int done[3]) {
    static const char *const tasks[] = {"raycast room", "build terrain", "toggle task"};
    int i;
    for (i = 0; i < 3; ++i) {
        cplus_gfx_rect(g, 20, 100 + i * 44, 560, 36, 22, 32, 48);
        cplus_gfx_rect(g, 30, 108 + i * 44, 20, 20,
                       done[i] ? 60 : 20, done[i] ? 200 : 40, done[i] ? 120 : 60);
        cplus_gfx_text(g, 64, 110 + i * 44, tasks[i]);
    }
}

int main(void) {
    cplus_gfx_window g;
    error e = error();
    int done[3] = {1, 0, 0};
    int mode = 0, frame = 0, angle = 0;
    SDL_Surface *shot;
    SDL_IOStream *io;
    SDL_Event ev;

    stdlib_set_error_callback((stdlib_error_callback)0);
    if (cplus_gfx_open(&g, "C+ Sample Games GUI", 720, 420) == 0) return 1;
    while (frame < 6) {
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_EVENT_QUIT) frame = 6;
        if (frame >= 3) mode = frame - 2;        /* demo cycle: 0,1,2 */
        angle += 12;
        cplus_gfx_clear(&g, 13, 17, 23);
        cplus_gfx_text(&g, 20, 14, "C+ MODERN LIBS - SAMPLE GAME WORKLOADS");
        cplus_gfx_button(&g, 20, 46, 170, mode == 0 ? "[1] raycast" : "1 raycast");
        cplus_gfx_button(&g, 205, 46, 170, mode == 1 ? "[2] blocks" : "2 blocks");
        cplus_gfx_button(&g, 390, 46, 190, mode == 2 ? "[3] todo" : "3 todo");
        if (mode == 0) draw_doom(&g, angle);
        else if (mode == 1) draw_minecraft(&g);
        else draw_todo(&g, done);
        {
            stdlib_scoped_scope scope = stdlib_scoped_scope(stdlib_scoped_stack_current(&e));
            char line[96];
            snprintf(line, sizeof(line), "mode=%d frame=%d (scoped bump, one-store free)", mode, frame);
            cplus_gfx_text(&g, 20, 380, line);
            scope.drop();
        }
        cplus_gfx_present(&g);
        if (frame == 0) {
            shot = SDL_RenderReadPixels(g.renderer, (SDL_Rect *)0);
            if (shot != (SDL_Surface *)0) {
                io = SDL_IOFromFile("proof/games_gui_nuklear/screenshots/sample_games.bmp", "wb");
                if (io != (SDL_IOStream *)0) (void)SDL_SaveBMP_IO(shot, io, 1);
                SDL_DestroySurface(shot);
            }
        }
        frame++;
    }
    cplus_gfx_close(&g);
    printf("SAMPLE_GAMES_GUI modes=3 frames=%d\n", frame);
    return 0;
}
