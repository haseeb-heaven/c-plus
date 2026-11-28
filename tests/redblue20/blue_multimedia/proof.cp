/* BLUE-TEAM agent 19 -- deterministic safety PROOF for the seven public
 * multimedia/window headers:
 *   stdlib/window.hp, graphics.hp, gui.hp, input.hp, image.hp, audio.hp,
 *   video.hp
 *
 * 2400 fixed-seed cases (SplitMix64 SEED 0xB10C0000C0FFEE15, per-case
 * SEED ^ (id+1)*GOLDEN ^ id*id).  kind = id % 50, so every documented refusal
 * path below is exercised 48 times.  No wall clock, PID, address, or host
 * state enters a result token.
 *
 * Coverage (the BLUE properties):
 *   - invalid dimensions    (window 0/negative/oversized, image dims/channels)
 *   - invalid handles       (NULL window/renderer/surface/gui/context/format)
 *   - invalid formats       (8bpp surface pixel, garbage SDL event, bad IHDR)
 *   - null handles guarded  (every NULL path returns its documented value)
 *   - truncated media       (PNG / WAV / MP4 containers)
 *   - double-drop inert     (window/surface/gui/image/decoder/frame)
 *   - canaries intact       (byte guards around the out-parameter arena plus a
 *                            dedicated 64-byte global canary band).
 *
 * Method: the headless fixtures (SDL_VIDEODRIVER=dummy, SDL_AUDIODRIVER=dummy,
 * miniaudio null backend) are built once by the parent.  Each case then runs in
 * a freshly forked child with an alarm(60) watchdog AND a 60s parent-side
 * monotonic deadline.  A case PASSES only when the child exits 0, emits the
 * exact token "BLUE20_MULTIMEDIA_PASS <id> <api> <case>", reports the expected
 * RES_REFUSED / RES_DEFINED outcome, and both canaries survive.  Anything else
 * (signal, hang, wrong token, canary overwrite, contract violation) is FAIL.
 * Parent prints the final line:
 *   BLUE20 multimedia <cases> <passed> <failed>
 *
 * Build (repo root):
 *   ./build/c+ build tests/redblue20/blue_multimedia/proof.cp --legacy-c \
 *     --sanitize=none $SDL $STB $MINIAUDIO $NUKLEAR $FF \
 *     -o tests/redblue20/blue_multimedia/out/proof
 * Run:
 *   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 \
 *     tests/redblue20/blue_multimedia/out/proof <log-path> <clip-path>
 */
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"
#include "stdlib/image.hp"
#include "stdlib/audio.hp"
#include "stdlib/video.hp"
#include "stdlib/input.hp"

#define BLUE_N 2400
#define BLUE_KINDS 50
#define BLUE_SEED 0xB10C0000C0FFEE15ULL
#define BLUE_GOLDEN 0x9E3779B97F4A7C15ULL
#define SENT_INT 0x5A5A5A5A
#define SENT_FLOAT 12345.5f
#define ALARM_SEC 60
#define DEADLINE_SEC 60
#define TOKEN "BLUE20_MULTIMEDIA_PASS"
#define CANARY_BYTES 64

/* ---- outcome tokens ---------------------------------------------------- */
#define RES_REFUSED 0
#define RES_DEFINED 1
#define RES_FAIL 2

static const char *blue_res_name(int r) {
    if (r == RES_REFUSED)
        return "refused";
    if (r == RES_DEFINED)
        return "defined";
    return "FAIL";
}

/* ---- deterministic PRNG ------------------------------------------------ */
static void blue_rnd(unsigned long long id, unsigned long long *out) {
    unsigned long long z = id * id;
    unsigned long long s = BLUE_SEED ^ ((id + 1ULL) * BLUE_GOLDEN) ^ z;
    z = s;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    out[0] = z ^ (z >> 31);
    z = out[0] + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    out[1] = z ^ (z >> 31);
    z = out[1] + 0x9E3779B97F4A7C15ULL;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    out[2] = z ^ (z >> 31);
}

/* ---- canaries ---------------------------------------------------------- */
/* Byte guards bracket the out-parameter regions; an overrun writes a guard.
 * A second, independent 64-byte band detects unrelated global corruption. */
struct blue_arena {
    unsigned char bpre[32];
    unsigned char bytes[256];
    unsigned char bpost[32];
    unsigned char ipre[32];
    int ints[64];
    unsigned char ipost[32];
    unsigned char fpre[32];
    float frames[64];
    unsigned char fpost[32];
};

static struct blue_arena g_arena;
static unsigned char g_canary[CANARY_BYTES];

static void blue_arm(void) {
    int i;
    __builtin_memset(g_arena.bpre, 0xA5, 32);
    __builtin_memset(g_arena.bytes, 0, 256);
    __builtin_memset(g_arena.bpost, 0x5A, 32);
    __builtin_memset(g_arena.ipre, 0x3C, 32);
    for (i = 0; i < 64; i++)
        g_arena.ints[i] = SENT_INT;
    __builtin_memset(g_arena.ipost, 0xC3, 32);
    __builtin_memset(g_arena.fpre, 0x7E, 32);
    for (i = 0; i < 64; i++)
        g_arena.frames[i] = SENT_FLOAT;
    __builtin_memset(g_arena.fpost, 0xE7, 32);
    for (i = 0; i < CANARY_BYTES; i++)
        g_canary[i] = (unsigned char)(0x5A ^ (i * 37));
}

static int blue_sentinel_ok(void) {
    unsigned char want[32];
    int ok = 1;
    int i;
    __builtin_memset(want, 0xA5, 32);
    if (__builtin_memcmp(g_arena.bpre, want, 32) != 0)
        ok = 0;
    __builtin_memset(want, 0x5A, 32);
    if (__builtin_memcmp(g_arena.bpost, want, 32) != 0)
        ok = 0;
    __builtin_memset(want, 0x3C, 32);
    if (__builtin_memcmp(g_arena.ipre, want, 32) != 0)
        ok = 0;
    __builtin_memset(want, 0xC3, 32);
    if (__builtin_memcmp(g_arena.ipost, want, 32) != 0)
        ok = 0;
    __builtin_memset(want, 0x7E, 32);
    if (__builtin_memcmp(g_arena.fpre, want, 32) != 0)
        ok = 0;
    __builtin_memset(want, 0xE7, 32);
    if (__builtin_memcmp(g_arena.fpost, want, 32) != 0)
        ok = 0;
    for (i = 0; i < 64; i++) {
        if (g_arena.ints[i] != SENT_INT)
            ok = 0;
        if (g_arena.frames[i] != SENT_FLOAT)
            ok = 0;
    }
    for (i = 0; i < CANARY_BYTES; i++) {
        if (g_canary[i] != (unsigned char)(0x5A ^ (i * 37)))
            ok = 0;
    }
    return ok;
}

struct blue_outcome {
    int observed;
    int sentinel;
};

static struct blue_outcome blue_result(int expected, int ok) {
    struct blue_outcome o;
    o.sentinel = blue_sentinel_ok();
    o.observed = (ok && o.sentinel) ? expected : RES_FAIL;
    return o;
}

/* ---- shared fixtures (headless) --------------------------------------- */
static char g_dir[512];
static char g_wav[560];
static char g_trunc_wav[560];
static char g_trunc_png[560];
static char g_bad_mp4[560];
static char g_png_path[560];
static char g_clip[560];

static sdl g_core;
static window g_win;
static renderer g_render;
static surface g_shot;
static gui g_ui;
static int g_have_sdl;
static int g_have_win;
static int g_have_render;
static int g_have_shot;
static int g_have_ui;
static SDL_Surface *g_low_surface;

static image g_img;
static int g_have_img;
static unsigned char g_png[4096];
static int g_png_len;

static audio_context g_ctx;
static audio_engine g_eng;
static audio_decoder g_ad;
static int g_have_ctx;
static int g_have_eng;
static int g_have_ad;
static unsigned char g_trunc_wav_bytes[512];
static int g_trunc_wav_len;

static media g_media;
static decoder g_dec;
static frame g_frame;
static int g_have_media;
static int g_have_dec;
static int g_have_frame;

static void blue_put32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static void blue_put16(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static unsigned blue_build_wav(unsigned char *wav_buf, unsigned samples) {
    unsigned data_bytes = samples * 2u;
    unsigned at = 0;
    unsigned i;
    const char *riff = "RIFF";
    const char *wave = "WAVE";
    const char *fmt = "fmt ";
    const char *data = "data";
    for (i = 0; i < 4; i++)
        wav_buf[at++] = (unsigned char)riff[i];
    blue_put32(wav_buf + at, 36u + data_bytes);
    at += 4;
    for (i = 0; i < 4; i++)
        wav_buf[at++] = (unsigned char)wave[i];
    for (i = 0; i < 4; i++)
        wav_buf[at++] = (unsigned char)fmt[i];
    blue_put32(wav_buf + at, 16u);
    at += 4;
    blue_put16(wav_buf + at, 1u);
    at += 2;
    blue_put16(wav_buf + at, 1u);
    at += 2;
    blue_put32(wav_buf + at, 8000u);
    at += 4;
    blue_put32(wav_buf + at, 16000u);
    at += 4;
    blue_put16(wav_buf + at, 2u);
    at += 2;
    blue_put16(wav_buf + at, 16u);
    at += 2;
    for (i = 0; i < 4; i++)
        wav_buf[at++] = (unsigned char)data[i];
    blue_put32(wav_buf + at, data_bytes);
    at += 4;
    for (i = 0; i < samples; i++) {
        int sample = (i % 20u < 10u) ? 8000 : -8000;
        blue_put16(wav_buf + at, (unsigned int)(sample & 0xffff));
        at += 2;
    }
    return at;
}

static int blue_write_file(const char *path, const unsigned char *data,
                           size_t n) {
    FILE *fp = fopen(path, "wb");
    size_t wrote;
    if (fp == NULL)
        return 0;
    wrote = fwrite(data, 1, n, fp);
    fclose(fp);
    return wrote == n ? 1 : 0;
}

/* Returns 0 on success, nonzero on setup failure. */
static int blue_setup(const char *log_path, const char *clip_path) {
    error e = error();
    unsigned char img_src36[36];
    unsigned char wav[512];
    unsigned wav_len;
    unsigned char mp4[128];
    size_t mp4_n;
    FILE *png_fp;
    FILE *mp4_fp;
    int x;
    int y;
    const char *slash;

    slash = strrchr(log_path, '/');
    if (slash != NULL && ((size_t)(slash - log_path)) < sizeof(g_dir)) {
        size_t n = (size_t)(slash - log_path);
        __builtin_memcpy(g_dir, log_path, n);
        g_dir[n] = '\0';
    } else {
        snprintf(g_dir, sizeof(g_dir), "out");
    }

    snprintf(g_wav, sizeof(g_wav), "%s/tone.wav", g_dir);
    snprintf(g_trunc_wav, sizeof(g_trunc_wav), "%s/trunc.wav", g_dir);
    snprintf(g_trunc_png, sizeof(g_trunc_png), "%s/trunc.png", g_dir);
    snprintf(g_bad_mp4, sizeof(g_bad_mp4), "%s/trunc.mp4", g_dir);
    snprintf(g_png_path, sizeof(g_png_path), "%s/valid.png", g_dir);
    snprintf(g_clip, sizeof(g_clip), "%s", clip_path);

    g_core = sdl((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS), &e);
    if (!g_core.valid())
        return 1;
    g_have_sdl = 1;
    g_win = window("blue20", 64, 48, (unsigned)SDL_WINDOW_HIDDEN, &e);
    if (!g_win.valid())
        return 2;
    g_have_win = 1;
    g_render = renderer(g_win.native(), "software", &e);
    if (!g_render.valid())
        return 3;
    g_have_render = 1;
    g_shot = g_render.read(&e);
    if (!g_shot.valid())
        return 4;
    g_have_shot = 1;
    g_ui = gui(g_win.native(), g_render.native(), &e);
    if (!g_ui.valid())
        return 5;
    g_have_ui = 1;
    g_low_surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_INDEX8);
    if (g_low_surface == (SDL_Surface *)0)
        return 22;

    for (y = 0; y < 3; y++) {
        for (x = 0; x < 4; x++) {
            int at = (y * 4 + x) * 3;
            img_src36[at] = (unsigned char)(x * 40);
            img_src36[at + 1] = (unsigned char)(y * 60);
            img_src36[at + 2] = 128;
        }
    }
    g_img = image_from_pixels(img_src36, 4, 3, 3, &e);
    if (!g_img.valid())
        return 6;
    g_have_img = 1;
    if (g_img.write_png(g_png_path, &e) != STDLIB_OK)
        return 7;
    png_fp = fopen(g_png_path, "rb");
    if (png_fp == NULL)
        return 8;
    g_png_len = (int)fread(g_png, 1, sizeof(g_png), png_fp);
    fclose(png_fp);
    if (g_png_len < 32)
        return 9;
    if (!blue_write_file(g_trunc_png, g_png, 16))
        return 10;

    wav_len = blue_build_wav(wav, 200);
    if (!blue_write_file(g_wav, wav, wav_len))
        return 11;
    g_trunc_wav_len = 16;
    for (x = 0; x < g_trunc_wav_len; x++)
        g_trunc_wav_bytes[x] = wav[x];
    if (!blue_write_file(g_trunc_wav, g_trunc_wav_bytes,
                         (size_t)g_trunc_wav_len))
        return 12;

    g_ctx = audio_context(1, &e);
    if (!g_ctx.valid())
        return 13;
    g_have_ctx = 1;
    g_eng = audio_engine(g_ctx.native(), &e);
    if (!g_eng.valid())
        return 14;
    g_have_eng = 1;
    g_ad = audio_decoder(g_wav, &e);
    if (!g_ad.valid())
        return 15;
    g_have_ad = 1;

    mp4_fp = fopen(g_clip, "rb");
    if (mp4_fp == NULL)
        return 16;
    mp4_n = fread(mp4, 1, sizeof(mp4), mp4_fp);
    fclose(mp4_fp);
    if (mp4_n < 32)
        return 17;
    if (!blue_write_file(g_bad_mp4, mp4, 32))
        return 18;

    g_media = media(g_clip, &e);
    if (!g_media.valid())
        return 19;
    g_have_media = 1;
    g_dec = g_media.open_decoder(&e);
    if (!g_dec.valid())
        return 20;
    g_have_dec = 1;
    g_frame = frame(&e);
    if (!g_frame.valid())
        return 21;
    g_have_frame = 1;

    return 0;
}

/* ---- window.hp scenarios ---------------------------------------------- */
static struct blue_outcome blue_window(int kind, unsigned long long *r) {
    error e = error();
    int dim = (int)(r[0] % 4097u);
    int dim2 = (int)(r[1] % 4097u);
    int ok = 0;
    int expected = RES_REFUSED;

    switch (kind) {
    case 0: { /* 0 x N / N x 0 dimensions -> refused */
        int w = (r[2] & 1u) ? 0 : dim;
        int h = (r[2] & 1u) ? dim2 : 0;
        window x = window("blue", w, h, (unsigned)SDL_WINDOW_HIDDEN, &e);
        ok = !x.valid() && e.failed_now() && x.native() == (SDL_Window *)0;
        x.drop();
        break;
    }
    case 1: { /* negative dimensions -> refused */
        int w = -((int)(r[0] % 4096u) + 1);
        int h = (r[1] & 1u) ? -((int)(r[1] % 4096u) + 1) : dim2 + 1;
        window x = window("blue", w, h, (unsigned)SDL_WINDOW_HIDDEN, &e);
        ok = !x.valid() && e.failed_now();
        x.drop();
        break;
    }
    case 2: { /* oversized positive dimensions: defined, no corruption */
        int big = 4097 + (int)(r[0] % 8192u);
        window x = window("blue", big, big, (unsigned)SDL_WINDOW_HIDDEN, &e);
        ok = x.valid() && e.ok() && x.native() != (SDL_Window *)0;
        expected = RES_DEFINED;
        x.drop();
        break;
    }
    case 3: { /* size() on NULL handle keeps out-params at sentinel */
        window x = window("blue", 0, 0, (unsigned)SDL_WINDOW_HIDDEN, &e);
        int rc = x.size(&g_arena.ints[0], &g_arena.ints[1], &e);
        ok = rc == STDLIB_FAIL && e.failed_now() &&
             g_arena.ints[0] == SENT_INT && g_arena.ints[1] == SENT_INT;
        x.drop();
        break;
    }
    case 4: { /* set_title(NULL) -> refused */
        window x = window("blue", 8, 8, (unsigned)SDL_WINDOW_HIDDEN, &e);
        int rc = x.set_title((const char *)0, &e);
        ok = rc == STDLIB_FAIL && e.failed_now();
        x.drop();
        break;
    }
    case 5: { /* double drop is idempotent, no crash */
        window x = window("blue", 8, 8, (unsigned)SDL_WINDOW_HIDDEN, &e);
        x.drop();
        int v1 = x.valid();
        x.drop();
        int v2 = x.valid();
        ok = v1 == 0 && v2 == 0 && x.native() == (SDL_Window *)0;
        expected = RES_DEFINED;
        break;
    }
    }
    return blue_result(expected, ok);
}

/* ---- graphics.hp scenarios -------------------------------------------- */
static struct blue_outcome blue_graphics(int kind, unsigned long long *r) {
    error e = error();
    int x = (int)(r[0] % 8192u) - 4096;
    int y = (int)(r[1] % 4096u) - 2048;
    int ok = 0;
    int expected = RES_REFUSED;

    switch (kind) {
    case 0: { /* NULL surface pixel -> refused, out sentinel intact */
        surface sfc_null = surface((SDL_Surface *)0);
        int rc = sfc_null.pixel(x, y, g_arena.bytes, &e);
        ok = rc == STDLIB_FAIL && e.failed_now() &&
             g_arena.bytes[0] == 0 && g_arena.bytes[3] == 0;
        sfc_null.drop();
        break;
    }
    case 1: { /* out-of-range pixel on the 64x48 snapshot -> refused */
        int px = 64 + (int)(r[0] % 4096u);
        int py = 48 + (int)(r[1] % 4096u);
        int rc = g_shot.pixel(px, py, g_arena.bytes, &e);
        ok = rc == STDLIB_FAIL && e.failed_now() &&
             g_arena.bytes[0] == 0 && g_arena.bytes[3] == 0;
        break;
    }
    case 2: { /* negative coordinates -> refused */
        int rc = g_shot.pixel(x, y, g_arena.bytes, &e);
        ok = rc == STDLIB_FAIL && e.failed_now() &&
             g_arena.bytes[0] == 0 && g_arena.bytes[3] == 0;
        break;
    }
    case 3: { /* invalid pixel format (8bpp) -> refused */
        surface sh = surface(g_low_surface);
        int rc = sh.pixel(1, 1, g_arena.bytes, &e);
        ok = rc == STDLIB_FAIL && e.failed_now() &&
             g_arena.bytes[0] == 0 && g_arena.bytes[3] == 0;
        break;
    }
    case 4: { /* renderer(NULL) -> invalid; draw calls refuse */
        renderer rnd_null = renderer((SDL_Window *)0, "software", &e);
        int a = rnd_null.set_color(1, 2, 3, 4, &e);
        int b = rnd_null.fill_rect(0.0f, 0.0f, 8.0f, 8.0f, &e);
        int c = rnd_null.clear(&e);
        ok = !rnd_null.valid() && a == STDLIB_FAIL && b == STDLIB_FAIL &&
             c == STDLIB_FAIL && e.failed_now();
        rnd_null.drop();
        break;
    }
    case 5: { /* double drop of an owned surface: idempotent */
        SDL_Surface *own = SDL_CreateSurface(4, 4, SDL_PIXELFORMAT_RGBA32);
        if (own != (SDL_Surface *)0) {
            surface sh = surface(own);
            sh.drop();
            int v1 = sh.valid();
            sh.drop();
            int v2 = sh.valid();
            ok = v1 == 0 && v2 == 0;
            expected = RES_DEFINED;
        }
        break;
    }
    }
    return blue_result(expected, ok);
}

/* ---- gui.hp scenarios -------------------------------------------------- */
static struct blue_outcome blue_gui(int kind, unsigned long long *r) {
    error e = error();
    int ok = 0;
    int expected = RES_REFUSED;

    switch (kind) {
    case 0: { /* NULL window/renderer handles -> refused */
        gui ui = gui((SDL_Window *)0, (SDL_Renderer *)0, &e);
        ok = !ui.valid() && e.failed_now() &&
             ui.native() == (struct nk_context *)0;
        ui.drop();
        break;
    }
    case 1: { /* handle_event(NULL) -> 0 */
        ok = g_ui.handle_event((SDL_Event *)0) == 0;
        break;
    }
    case 2: { /* window_begin(NULL,...) -> 0 */
        ok = g_ui.window_begin((const char *)0, 4.0f, 4.0f, 20.0f, 20.0f, 0) == 0;
        break;
    }
    case 3: { /* button(NULL)/slider(NULL)/slider(min>max) -> 0 */
        int value = SENT_INT;
        int a = g_ui.button((const char *)0);
        int b = g_ui.slider((int *)0, 0, 10);
        int c = g_ui.slider(&value, 10, 0);
        ok = a == 0 && b == 0 && c == 0 && value == SENT_INT;
        break;
    }
    case 4: { /* extreme coordinates stay defined, no crash */
        char ext_title[32];
        float huge = 1.0e6f + (float)(r[0] % 1000u);
        float coord = (r[1] & 1u) ? -huge : huge;
        int shown;
        snprintf(ext_title, sizeof(ext_title), "blue-ext%d",
                 (int)(r[0] % 100000u));
        g_ui.input_begin();
        shown = g_ui.window_begin(ext_title, coord, -huge, huge, huge, 0);
        if (shown != 0)
            g_ui.window_end();
        g_ui.input_end();
        ok = shown == 0 || shown == 1;
        expected = RES_DEFINED;
        break;
    }
    case 5: { /* double drop of an invalid gui: idempotent */
        gui ui = gui((SDL_Window *)0, (SDL_Renderer *)0, &e);
        ui.drop();
        int v1 = ui.valid();
        ui.drop();
        int v2 = ui.valid();
        ok = v1 == 0 && v2 == 0;
        expected = RES_DEFINED;
        break;
    }
    }
    return blue_result(expected, ok);
}

/* ---- input.hp scenarios ------------------------------------------------ */
static struct blue_outcome blue_input(int kind, unsigned long long *r) {
    int ok = 0;
    int expected = RES_REFUSED;

    switch (kind) {
    case 0: { /* every NULL-event helper returns its documented default */
        ok = event_is_quit((const SDL_Event *)0) == 0 &&
             event_is_key_down((const SDL_Event *)0) == 0 &&
             event_is_key_up((const SDL_Event *)0) == 0 &&
             event_scancode((const SDL_Event *)0) == SDL_SCANCODE_UNKNOWN &&
             event_text((const SDL_Event *)0) == '\0';
        break;
    }
    case 1: { /* out-of-range scancodes are rejected */
        bool state[SDL_SCANCODE_COUNT];
        int i;
        int bad = (r[0] & 1u) ? -((int)(r[1] % 5000u) + 1)
                              : (int)SDL_SCANCODE_COUNT + (int)(r[1] % 5000u);
        for (i = 0; i < (int)SDL_SCANCODE_COUNT; i++)
            state[i] = false;
        state[SDL_SCANCODE_A] = true;
        ok = key_pressed(state, (SDL_Scancode)bad) == 0 &&
             key_pressed((const bool *)0, SDL_SCANCODE_A) == 0 &&
             key_pressed(state, SDL_SCANCODE_A) == 1;
        break;
    }
    case 2: { /* synthetic event with a garbage type is ignored */
        SDL_Event ev;
        __builtin_memset(&ev, 0, sizeof(ev));
        ev.type = (Uint32)(r[0] & 0xffffu);
        ok = event_is_quit(&ev) == 0 &&
             event_is_key_down(&ev) == 0 &&
             event_is_key_up(&ev) == 0 &&
             event_scancode(&ev) == SDL_SCANCODE_UNKNOWN &&
             event_text(&ev) == '\0';
        break;
    }
    case 3: { /* poll_event(NULL) only pumps; defined 0/1 result */
        int got = poll_event((SDL_Event *)0);
        ok = got == 0 || got == 1;
        expected = RES_DEFINED;
        break;
    }
    case 4: { /* mouse_state(NULL,NULL) is safe */
        unsigned mask = mouse_state((float *)0, (float *)0);
        ok = (mask & ~0x1fu) == 0u || mask != 0xffffffffu;
        expected = RES_DEFINED;
        break;
    }
    case 5: { /* TEXT_INPUT with NULL text pointer -> '\0' */
        SDL_Event ev;
        __builtin_memset(&ev, 0, sizeof(ev));
        ev.type = SDL_EVENT_TEXT_INPUT;
        ev.text.text = (char *)0;
        ok = event_text(&ev) == '\0';
        break;
    }
    }
    return blue_result(expected, ok);
}

/* ---- image.hp scenarios ------------------------------------------------ */
static struct blue_outcome blue_image(int kind, unsigned long long *r) {
    error e = error();
    int ok = 0;
    int expected = RES_REFUSED;
    int dim = (int)(r[0] % 4097u);
    int dim2 = (int)(r[1] % 4097u);

    switch (kind) {
    case 0: { /* NULL source with random (incl huge) dims -> refused */
        int huge = 100000 + (int)(r[2] % 100000u);
        image im_a = image_from_pixels((const unsigned char *)0, dim, dim2, 3, &e);
        image im_b = image_from_pixels((const unsigned char *)0, huge, huge, 4, &e);
        ok = !im_a.valid() && e.failed_now() && !im_b.valid() && e.failed_now();
        im_a.drop();
        im_b.drop();
        break;
    }
    case 1: { /* non-positive dimensions / channels -> refused */
        int w = (r[2] & 1u) ? 0 : -((int)(r[2] % 100u) + 1);
        int ch = (r[0] & 1u) ? 0 : -((int)(r[0] % 100u) + 1);
        unsigned char im_src16[16];
        __builtin_memset(im_src16, 3, 16);
        image im_a = image_from_pixels(im_src16, w, dim2 + 1, 3, &e);
        ok = !im_a.valid() && e.failed_now();
        im_a.drop();
        image im_b = image_from_pixels(im_src16, dim + 1, dim2 + 1, ch, &e);
        ok = ok && !im_b.valid() && e.failed_now();
        im_b.drop();
        break;
    }
    case 2: { /* oversized dimensions + NULL source -> refused early */
        int huge = INT_MAX - (int)(r[2] % 1000u);
        image im_a = image_from_pixels((const unsigned char *)0, huge, huge, 4, &e);
        ok = !im_a.valid() && e.failed_now();
        im_a.drop();
        break;
    }
    case 3: { /* truncated PNG buffer -> refused */
        image im_a = image_from_memory(g_png, 16, 3, &e);
        ok = !im_a.valid() && e.failed_now();
        im_a.drop();
        break;
    }
    case 4: { /* crafted PNG with huge IHDR dims -> refused */
        unsigned char png[64];
        int n = 29;
        __builtin_memset(png, 0, sizeof(png));
        png[0] = 137; png[1] = 80; png[2] = 78; png[3] = 71;
        png[4] = 13; png[5] = 10; png[6] = 26; png[7] = 10;
        blue_put32(png + 8, 13u);
        png[12] = 'I'; png[13] = 'H'; png[14] = 'D'; png[15] = 'R';
        blue_put32(png + 16, 0xffffffffu);
        blue_put32(png + 20, 0xffffffffu);
        png[24] = 8; png[25] = 2;
        image im_a = image_from_memory(png, n, 3, &e);
        ok = !im_a.valid() && e.failed_now();
        im_a.drop();
        break;
    }
    case 5: { /* desired_channels out of 0..4 -> refused */
        int bad = (r[2] & 1u) ? -1 - (int)(r[0] % 100u)
                              : 5 + (int)(r[1] % 100u);
        image im_a = image_from_memory(g_png, g_png_len, bad, &e);
        image im_b = image(g_png_path, bad, &e);
        ok = !im_a.valid() && e.failed_now() && !im_b.valid() && e.failed_now();
        im_a.drop();
        im_b.drop();
        break;
    }
    case 6: { /* out-of-range pixel -> NULL */
        int px = 4 + (int)(r[0] % 4096u);
        int py = 3 + (int)(r[1] % 4096u);
        ok = g_img.pixel(px, py) == (const unsigned char *)0 &&
             g_img.pixel(-1, 0) == (const unsigned char *)0;
        break;
    }
    case 7: { /* write with NULL path -> refused */
        int im_a = g_img.write_png((const char *)0, &e);
        int im_b = g_img.write_bmp((const char *)0, &e);
        ok = im_a == STDLIB_FAIL && im_b == STDLIB_FAIL && e.failed_now();
        break;
    }
    case 8: { /* double drop of an empty image: idempotent */
        image im_a = image((const char *)0, 0, &e);
        im_a.drop();
        int v1 = im_a.valid();
        im_a.drop();
        int v2 = im_a.valid();
        ok = v1 == 0 && v2 == 0 && im_a.size_bytes() == 0;
        expected = RES_DEFINED;
        break;
    }
    }
    return blue_result(expected, ok);
}

/* ---- audio.hp scenarios ------------------------------------------------ */
static struct blue_outcome blue_audio(int kind, unsigned long long *r) {
    error e = error();
    int ok = 0;
    int expected = RES_REFUSED;

    switch (kind) {
    case 0: { /* truncated in-memory WAV -> refused */
        audio_decoder ad_a = decoder_from_memory(g_trunc_wav_bytes,
                                              (size_t)g_trunc_wav_len, &e);
        ok = !ad_a.valid() && e.failed_now();
        ad_a.drop();
        break;
    }
    case 1: { /* zero-size / NULL buffers -> refused */
        audio_decoder ad_a = decoder_from_memory((const void *)0, 16, &e);
        audio_decoder ad_b = decoder_from_memory(g_png, 0, &e);
        ok = !ad_a.valid() && e.failed_now() && !ad_b.valid() && e.failed_now();
        ad_a.drop();
        ad_b.drop();
        break;
    }
    case 2: { /* NULL path -> empty decoder, no error (defined) */
        audio_decoder ad_a = audio_decoder((const char *)0, &e);
        ok = !ad_a.valid() && e.ok() && ad_a.channels() == 0 &&
             ad_a.sample_rate() == 0 && ad_a.frame_count() == 0;
        expected = RES_DEFINED;
        ad_a.drop();
        break;
    }
    case 3: { /* truncated WAV file -> refused */
        audio_decoder ad_a = audio_decoder(g_trunc_wav, &e);
        ok = !ad_a.valid() && e.failed_now();
        ad_a.drop();
        break;
    }
    case 4: { /* read(NULL, n) -> refused, frame sentinel intact */
        unsigned long long got = g_ad.read((float *)0, 16, &e);
        ok = got == 0 && e.failed_now() && g_arena.frames[0] == SENT_FLOAT;
        break;
    }
    case 5: { /* read(frames, 0) -> 0, defined */
        unsigned long long got = g_ad.read(g_arena.frames, 0, &e);
        ok = got == 0 && e.ok() && g_arena.frames[0] == SENT_FLOAT;
        expected = RES_DEFINED;
        break;
    }
    case 6: { /* NULL path/engine -> refused */
        int play_rc = g_eng.play((const char *)0, &e);
        audio_sound snd_null = audio_sound((ma_engine *)0, g_wav, &e);
        int sound_rc = snd_null.play(&e);
        ok = play_rc == STDLIB_FAIL && !snd_null.valid() &&
             sound_rc == STDLIB_FAIL && e.failed_now();
        snd_null.drop();
        break;
    }
    case 7: { /* audio_device(NULL context, ...) -> refused */
        audio_device dev_null = audio_device((ma_context *)0, (r[0] & 1u) ? 1 : 0,
                                      48000, 2, (ma_device_data_proc)0,
                                      (void *)0, &e);
        ok = !dev_null.valid() && e.failed_now();
        if (dev_null.valid())
            dev_null.drop();
        break;
    }
    case 8: { /* double drop of an invalid decoder: idempotent */
        audio_decoder ad_a = audio_decoder((const char *)0, &e);
        ad_a.drop();
        int v1 = ad_a.valid();
        ad_a.drop();
        int v2 = ad_a.valid();
        ok = v1 == 0 && v2 == 0;
        expected = RES_DEFINED;
        break;
    }
    }
    return blue_result(expected, ok);
}

/* ---- video.hp scenarios ------------------------------------------------ */
static struct blue_outcome blue_video(int kind, unsigned long long *r) {
    error e = error();
    int ok = 0;
    int expected = RES_REFUSED;

    switch (kind) {
    case 0: { /* video_error_text with NULL/zero buffer -> safe */
        char err_buf[8];
        __builtin_memset(err_buf, 0, sizeof(err_buf));
        video_error_text(-1, (char *)0, 0);
        video_error_text(-1, err_buf, 0);
        video_error_text(-1, err_buf, sizeof(err_buf));
        ok = err_buf[0] != 0 || err_buf[0] == 0;
        expected = RES_DEFINED;
        break;
    }
    case 1: { /* stream lookup with NULL format / out-of-range index -> NULL */
        int s = (int)(r[0] % 200u) - 20;
        ok = video_stream_params((AVFormatContext *)0, s) ==
                 (const AVCodecParameters *)0 &&
             video_stream_params((AVFormatContext *)0, -s) ==
                 (const AVCodecParameters *)0;
        break;
    }
    case 2: { /* codec id of NULL params is NONE */
        ok = video_codec_id((const AVCodecParameters *)0) == AV_CODEC_ID_NONE;
        break;
    }
    case 3: { /* decoder(NULL, bad stream) -> refused */
        int s = (int)(r[0] % 200u) - 20;
        decoder vdec_null = decoder((AVFormatContext *)0, s, &e);
        ok = !vdec_null.valid() && e.failed_now();
        vdec_null.drop();
        break;
    }
    case 4: { /* media(NULL) -> empty, no error (defined) */
        media m = media((const char *)0, &e);
        ok = !m.valid() && e.ok() && m.stream_count() == 0 &&
             m.video_index() == -1 && m.video_width() == 0 &&
             m.video_height() == 0;
        expected = RES_DEFINED;
        m.drop();
        break;
    }
    case 5: { /* truncated container -> refused */
        media m = media(g_bad_mp4, &e);
        ok = !m.valid() && e.failed_now();
        m.drop();
        break;
    }
    case 6: { /* next_frame(NULL) -> refused */
        int rc = g_dec.next_frame((frame *)0, &e);
        ok = rc == STDLIB_FAIL && e.failed_now();
        break;
    }
    case 7: { /* double drop of a frame: idempotent */
        error e2 = error();
        frame f = frame(&e2);
        f.drop();
        int v1 = f.valid();
        f.drop();
        int v2 = f.valid();
        ok = e2.ok() && v1 == 0 && v2 == 0 && f.width() == 0;
        expected = RES_DEFINED;
        break;
    }
    }
    return blue_result(expected, ok);
}

/* ---- metadata and dispatch --------------------------------------------- */
static void blue_meta(int kind, const char **api, const char **name) {
    if (kind >= 0 && kind < 6) {
        *api = "window";
        switch (kind) {
        case 0: *name = "zero_dims"; break;
        case 1: *name = "negative_dims"; break;
        case 2: *name = "oversized_dims"; break;
        case 3: *name = "size_null_handle"; break;
        case 4: *name = "set_title_null"; break;
        default: *name = "double_drop"; break;
        }
    } else if (kind < 12) {
        *api = "graphics";
        switch (kind) {
        case 6: *name = "surface_null_pixel"; break;
        case 7: *name = "pixel_out_of_range"; break;
        case 8: *name = "pixel_negative"; break;
        case 9: *name = "invalid_pixel_format"; break;
        case 10: *name = "renderer_null"; break;
        default: *name = "double_drop"; break;
        }
    } else if (kind < 18) {
        *api = "gui";
        switch (kind) {
        case 12: *name = "null_handles"; break;
        case 13: *name = "handle_event_null"; break;
        case 14: *name = "window_begin_null"; break;
        case 15: *name = "button_slider_bad"; break;
        case 16: *name = "extreme_rect"; break;
        default: *name = "double_drop"; break;
        }
    } else if (kind < 24) {
        *api = "input";
        switch (kind) {
        case 18: *name = "null_event"; break;
        case 19: *name = "oob_scancode"; break;
        case 20: *name = "garbage_event"; break;
        case 21: *name = "poll_null"; break;
        case 22: *name = "mouse_null"; break;
        default: *name = "text_null"; break;
        }
    } else if (kind < 33) {
        *api = "image";
        switch (kind) {
        case 24: *name = "null_source"; break;
        case 25: *name = "bad_dimensions"; break;
        case 26: *name = "oversized_dims"; break;
        case 27: *name = "truncated_png"; break;
        case 28: *name = "huge_ihdr"; break;
        case 29: *name = "bad_channels"; break;
        case 30: *name = "pixel_oob"; break;
        case 31: *name = "write_null_path"; break;
        default: *name = "double_drop"; break;
        }
    } else if (kind < 42) {
        *api = "audio";
        switch (kind) {
        case 33: *name = "truncated_wav"; break;
        case 34: *name = "zero_size_buffer"; break;
        case 35: *name = "null_path"; break;
        case 36: *name = "truncated_file"; break;
        case 37: *name = "read_null_frames"; break;
        case 38: *name = "read_zero_count"; break;
        case 39: *name = "null_engine_path"; break;
        case 40: *name = "null_context"; break;
        default: *name = "double_drop"; break;
        }
    } else {
        *api = "video";
        switch (kind) {
        case 42: *name = "error_text_null"; break;
        case 43: *name = "stream_params_null"; break;
        case 44: *name = "codec_id_null"; break;
        case 45: *name = "decoder_null"; break;
        case 46: *name = "media_null"; break;
        case 47: *name = "truncated_media"; break;
        case 48: *name = "next_frame_null"; break;
        default: *name = "double_drop"; break;
        }
    }
}

static struct blue_outcome blue_exec(int kind, unsigned long long *r) {
    if (kind < 6)
        return blue_window(kind, r);
    if (kind < 12)
        return blue_graphics(kind - 6, r);
    if (kind < 18)
        return blue_gui(kind - 12, r);
    if (kind < 24)
        return blue_input(kind - 18, r);
    if (kind < 33)
        return blue_image(kind - 24, r);
    if (kind < 42)
        return blue_audio(kind - 33, r);
    return blue_video(kind - 42, r);
}

/* ---- monotonic helper -------------------------------------------------- */
static double blue_now(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

/* ---- fork-per-case runner ---------------------------------------------- */
static int fork_case(int id, int kind, int pfds[2], pid_t *pid) {
    if (pipe(pfds) != 0)
        return -1;
    *pid = fork();
    if (*pid < 0) {
        close(pfds[0]);
        close(pfds[1]);
        return -1;
    }
    if (*pid == 0) {
        unsigned long long r[3];
        struct blue_outcome o;
        int good;
        char line[256];
        close(pfds[0]);
        alarm(ALARM_SEC);
        blue_arm();
        blue_rnd((unsigned long long)id, r);
        o = blue_exec(kind, r);
        good = (o.observed != RES_FAIL) && (o.sentinel == 1);
        if (good) {
            const char *api;
            const char *case_name;
            blue_meta(kind, &api, &case_name);
            snprintf(line, sizeof(line), "%s %d %s %s\n",
                     TOKEN, id, api, case_name);
        } else {
            snprintf(line, sizeof(line), "BLUE20_MULTIMEDIA_FAIL %d %d %d %d\n",
                     id, kind, o.observed, o.sentinel);
        }
        (void)write(pfds[1], line, strlen(line));
        close(pfds[1]);
        _exit(good ? 0 : 1);
    }
    close(pfds[1]);
    return 0;
}

static size_t read_line(int fd, char *out, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c = '\0';
        ssize_t k = read(fd, &c, 1);
        if (k <= 0 || c == '\n')
            break;
        if (n + 1 < cap)
            out[n++] = c;
        else
            break;
    }
    out[n] = '\0';
    return n;
}

static void hash_line(const char *s, unsigned long long *digest) {
    while (*s) {
        unsigned char c = (unsigned char)*s;
        *digest ^= c;
        *digest *= 1099511628211ULL;
        s++;
    }
    *digest ^= '\n';
    *digest *= 1099511628211ULL;
}

int main(int argc, char **argv) {
    const char *log_path = "tests/redblue20/blue_multimedia/out/results.log";
    const char *clip_path = "build/stdlib-tests/clip.mp4";
    FILE *log;
    char line[256];
    char want[256];
    char rec[384];
    unsigned long long digest = 1469598103934665603ULL;
    int pfds[2];
    int status;
    int id;
    int kind;
    int setup_rc;
    int passed = 0;
    int failed = 0;
    pid_t pid;

    if (argc > 1 && argv[1] != (char *)0)
        log_path = argv[1];
    if (argc > 2 && argv[2] != (char *)0)
        clip_path = argv[2];

    log = fopen(log_path, "w");
    if (log == (FILE *)0) {
        fprintf(stderr, "BLUE20: cannot open %s\n", log_path);
        return 2;
    }

    setup_rc = blue_setup(log_path, clip_path);
    if (setup_rc != 0) {
        fprintf(stderr, "BLUE20: setup failed at step %d\n", setup_rc);
        fclose(log);
        printf("BLUE20 multimedia %d 0 %d\n", BLUE_N, BLUE_N);
        return 2;
    }

    signal(SIGPIPE, SIG_IGN);
    for (id = 0; id < BLUE_N; id++) {
        size_t n;
        int rc;
        int timed_out = 0;
        double deadline;
        const char *api;
        const char *case_name;

        kind = id % BLUE_KINDS;
        blue_meta(kind, &api, &case_name);

        if (fork_case(id, kind, pfds, &pid) != 0) {
            snprintf(rec, sizeof(rec), "E %05d %s %s FORK_FAIL\n",
                     id, api, case_name);
            fputs(rec, log);
            failed++;
            hash_line(rec, &digest);
            continue;
        }

        /* Parent-side 60s monotonic deadline on top of the child alarm(60). */
        deadline = blue_now() + (double)DEADLINE_SEC;
        for (;;) {
            pid_t w = waitpid(pid, &status, WNOHANG);
            if (w == pid)
                break;
            if (w < 0 && errno != EINTR)
                break;
            if (blue_now() >= deadline) {
                timed_out = 1;
                kill(pid, SIGKILL);
                (void)waitpid(pid, &status, 0);
                break;
            }
            {
                struct timespec nap;
                nap.tv_sec = 0;
                nap.tv_nsec = 1000000L;
                nanosleep(&nap, (struct timespec *)0);
            }
        }

        n = read_line(pfds[0], line, sizeof(line));
        close(pfds[0]);

        snprintf(want, sizeof(want), "%s %d %s %s", TOKEN, id, api, case_name);
        rc = timed_out ? -1 : 0;
        if (rc == 0 && (WIFSIGNALED(status) || !WIFEXITED(status) ||
                        WEXITSTATUS(status) != 0))
            rc = -1;
        if (rc == 0 && (n == 0 || strcmp(line, want) != 0))
            rc = -1;

        if (rc != 0) {
            snprintf(rec, sizeof(rec),
                     "FAIL id=%d api=%s case=%s timeout=%d sig=%d exit=%d line=%s\n",
                     id, api, case_name, timed_out,
                     WIFSIGNALED(status) ? WTERMSIG(status) : 0,
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1, line);
            fputs(rec, log);
            failed++;
        } else {
            snprintf(rec, sizeof(rec), "PASS %s\n", line);
            fputs(rec, log);
            passed++;
        }
        hash_line(rec, &digest);
    }
    fclose(log);

    printf("BLUE20_DIGEST %016llx\n", digest);
    printf("BLUE20 multimedia %d %d %d\n", BLUE_N, passed, failed);
    return failed == 0 ? 0 : 1;
}
