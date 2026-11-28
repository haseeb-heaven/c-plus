/* Interactive olive.c + SDL3 demo.
 *
 * Renders the olive.c software framebuffer into an SDL3 texture, scales it to
 * the window, and accepts mouse/keyboard input. With OLIVE_SDL_PROOF=1 it
 * runs a fixed 4-frame sequence (no host input needed, works with the dummy
 * video driver), writes a real framebuffer BMP, and prints deterministic
 * markers so run.sh can verify it.
 *
 * Upstream: https://github.com/tsoding/olive.c (MIT -- see LICENSE).
 */
#define OLIVEC_IMPLEMENTATION
#include "olive.c"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 320
#define HEIGHT 240
#define SCALE 2
#define WIN_W (WIDTH * SCALE)
#define WIN_H (HEIGHT * SCALE)

static const uint32_t CIRCLE_COLORS[] = {
    0x99AA2020u, 0x9920AA20u, 0x992020AAu, 0x99AAAA20u
};
#define COLOR_COUNT (sizeof(CIRCLE_COLORS) / sizeof(CIRCLE_COLORS[0]))



static void render_frame(Olivec_Canvas oc, int cx, int cy, int color_idx,
                         int show_triangle)
{
    olivec_fill(oc, 0xFF181818u);
    if (show_triangle)
        olivec_triangle3c(oc, WIDTH / 2, HEIGHT / 8, WIDTH / 8, HEIGHT / 2,
                          WIDTH * 7 / 8, HEIGHT * 7 / 8,
                          0xFF2020FFu, 0xFF20FF20u, 0xFFFF2020u);
    olivec_circle(oc, cx, cy, 24, CIRCLE_COLORS[color_idx % COLOR_COUNT]);
}

static int capture_bmp(SDL_Renderer *renderer, const char *path)
{
    SDL_Surface *shot = SDL_RenderReadPixels(renderer, (SDL_Rect *)0);
    if (shot == (SDL_Surface *)0)
        return 0;
    SDL_IOStream *io = SDL_IOFromFile(path, "wb");
    if (io == (SDL_IOStream *)0) {
        SDL_DestroySurface(shot);
        return 0;
    }
    int ok = SDL_SaveBMP_IO(shot, io, 1);
    SDL_DestroySurface(shot);
    return ok;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    const int proof = getenv("OLIVE_SDL_PROOF") != (char *)0;

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "olive_sdl: SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }
    SDL_Window *win = SDL_CreateWindow("olive.c + SDL3", WIN_W, WIN_H, 0);
    SDL_Renderer *renderer = (win != (SDL_Window *)0)
        ? SDL_CreateRenderer(win, (char *)0) : (SDL_Renderer *)0;
    SDL_Texture *texture = (renderer != (SDL_Renderer *)0)
        ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ABGR8888,
                            SDL_TEXTUREACCESS_STREAMING, WIDTH, HEIGHT)
        : (SDL_Texture *)0;
    if (win == (SDL_Window *)0 || renderer == (SDL_Renderer *)0 ||
        texture == (SDL_Texture *)0) {
        fprintf(stderr, "olive_sdl: SDL setup failed: %s\n", SDL_GetError());
        return 1;
    }

    static uint32_t pixels[WIDTH * HEIGHT];
    Olivec_Canvas oc = olivec_canvas(pixels, WIDTH, HEIGHT, WIDTH);
    int cx = WIDTH / 2, cy = HEIGHT / 2, color_idx = 0, show_triangle = 1;
    int running = 1, frames = 0, mouse_moves = 0, key_events = 0, clicks = 0;

    /* Fixed proof sequence: no host input required, identical every run. */
    static const int px[4] = {160, 100, 220, 160};
    static const int py[4] = {120, 80, 160, 120};

    while (running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            if (ev.type == SDL_EVENT_QUIT) {
                running = 0;
            } else if (ev.type == SDL_EVENT_KEY_DOWN) {
                key_events++;
                if (ev.key.key == SDLK_ESCAPE)
                    running = 0;
                else if (ev.key.key == SDLK_SPACE)
                    show_triangle = !show_triangle;
                else if (ev.key.key == SDLK_R)
                    color_idx = 0;
                else if (ev.key.key == SDLK_C)
                    color_idx++;
            } else if (ev.type == SDL_EVENT_MOUSE_MOTION) {
                mouse_moves++;
                cx = (int)(ev.motion.x / (float)SCALE);
                cy = (int)(ev.motion.y / (float)SCALE);
            } else if (ev.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
                clicks++;
                color_idx++;
            }
        }
        if (proof) {
            if (frames >= 4) { running = 0; break; }
            cx = px[frames]; cy = py[frames];
            if (frames == 1) color_idx = 1;
            if (frames == 2) show_triangle = 0;
            if (frames == 3) { color_idx = 2; show_triangle = 1; }
        }

        render_frame(oc, cx, cy, color_idx, show_triangle);
        void *dst = (void *)0;
        int pitch = 0;
        if (SDL_LockTexture(texture, (SDL_Rect *)0, &dst, &pitch)) {
            memcpy(dst, pixels, sizeof(pixels));
            SDL_UnlockTexture(texture);
        }
        SDL_SetRenderDrawColor(renderer, 9, 12, 20, 255);
        SDL_RenderClear(renderer);
        SDL_FRect dstrect = {0.0f, 0.0f, (float)WIN_W, (float)WIN_H};
        SDL_RenderTexture(renderer, texture, (const SDL_FRect *)0, &dstrect);
        SDL_RenderPresent(renderer);
        frames++;

        if (proof && frames >= 4) {
            if (!capture_bmp(renderer, "out/olive_sdl_real.bmp")) {
                fprintf(stderr, "olive_sdl: framebuffer capture failed\n");
                running = 0;
            }
        }
        if (!proof && frames > 10000)
            running = 0;
    }

    printf("OLIVE_SDL frames=%d circle=%d,%d colors=%d triangle=%d "
           "mouse=%d keys=%d clicks=%d\n",
           frames, cx, cy, color_idx, show_triangle,
           mouse_moves, key_events, clicks);

    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return frames >= 4 ? 0 : 1;
}
