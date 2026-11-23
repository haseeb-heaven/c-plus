/* ADV1000 / multimedia -- 1000 deterministic adversarial scenarios.
 *
 * Scope: the seven public multimedia/window headers
 *   stdlib/graphics.hp, gui.hp, image.hp, video.hp, window.hp, input.hp,
 *   audio.hp
 *
 * Determinism: each scenario id 0..999 derives every parameter from one fixed
 * SplitMix64 seed plus its own id.  No wall-clock, PID, address, or host state
 * enters a result token.  Kind = id % 50 (50 kinds, 6-9 per header), so every
 * kind is exercised ~20 times and every scenario is distinct.
 *
 * Method: SDL3 runs on SDL_VIDEODRIVER=dummy / SDL_AUDIODRIVER=dummy and
 * miniaudio uses its explicit null backend.  Each case arms byte guards around
 * the work buffers (and pre-seeds out-parameters with a sentinel), invokes one
 * documented refusal/safe-result path, then checks both the expected result
 * and that every guard byte survived.  A single changed byte fails the case.
 *
 * Output: one canonical line "S <id> <api> <case> <result> <sentinel_ok>" per
 * scenario, then
 *   ADV1000 multimedia 1000 <refused> sentinel_ok <failures>
 * The same 1000 S-lines are written to <log-path> (argv[1], default
 * results/scenarios.log).  Exit status is nonzero on any failure.
 *
 * Build (repo root, same link flags as method_tests/multimedia/run_all.sh):
 *   ./build/c+ build tests/adversarial_1000/multimedia/adv1000_multimedia.cp \
 *     --strict --sanitize=none <sdl3> <stb> <miniaudio> <nuklear> <ffmpeg> \
 *     -o tests/adversarial_1000/multimedia/results/adv1000_multimedia
 * Run:  SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 120 <bin> <log>
 *
 * NOTE: this is evidence, not proof.  It exercises the listed contracts with
 * deterministic inputs; it cannot establish the absence of all defects.
 */
#include <limits.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"
#include "stdlib/image.hp"
#include "stdlib/audio.hp"
#include "stdlib/video.hp"
#include "stdlib/input.hp"

#define ADV_N 1000
#define ADV_KINDS 50
#define ADV_SEED 0xC0FFEE123456789ULL
#define SENT_INT 0x5A5A5A5A
#define SENT_FLOAT 12345.5f

/* ---- outcome tokens ---------------------------------------------------- */
#define RES_REFUSED 0
#define RES_DEFINED 1
#define RES_FAIL 2

static const char *adv_res_name(int r) {
    if (r == RES_REFUSED)
        return "refused";
    if (r == RES_DEFINED)
        return "defined";
    return "FAIL";
}

/* ---- deterministic PRNG ------------------------------------------------ */
/* SplitMix64-style avalanche; three independent words per scenario id. */
static void adv_rnd(unsigned long long id, unsigned long long *out) {
    unsigned long long z = ADV_SEED ^ (id * 0x9E3779B97F4A7C15ULL);
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

/* ---- guarded arena ----------------------------------------------------- */
/* Byte guards bracket each out-parameter region; an overrun writes into a
 * guard.  Struct layout keeps the regions adjacent in one allocation. */
struct adv_arena {
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

static struct adv_arena g_arena;

static void adv_arm(void) {
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
}

static int adv_sentinel_ok(void) {
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
    return ok;
}

struct adv_outcome {
    int observed;
    int sentinel;
};

static struct adv_outcome adv_result(int expected, int ok) {
    struct adv_outcome o;
    o.sentinel = adv_sentinel_ok();
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

static void adv_put32(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static void adv_put16(unsigned char *p, unsigned int v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static unsigned adv_build_wav(unsigned char *wav_buf, unsigned samples) {
    unsigned data_bytes = samples * 2u;
    unsigned at = 0;
    unsigned i;
    const char *riff = "RIFF";
    const char *wave = "WAVE";
    const char *fmt = "fmt ";
    const char *data = "data";
    for (i = 0; i < 4; i++)
        wav_buf[at++] = (unsigned char)riff[i];
    adv_put32(wav_buf + at, 36u + data_bytes);
    at += 4;
    for (i = 0; i < 4; i++)
        wav_buf[at++] = (unsigned char)wave[i];
    for (i = 0; i < 4; i++)
        wav_buf[at++] = (unsigned char)fmt[i];
    adv_put32(wav_buf + at, 16u);
    at += 4;
    adv_put16(wav_buf + at, 1u);
    at += 2;
    adv_put16(wav_buf + at, 1u);
    at += 2;
    adv_put32(wav_buf + at, 8000u);
    at += 4;
    adv_put32(wav_buf + at, 16000u);
    at += 4;
    adv_put16(wav_buf + at, 2u);
    at += 2;
    adv_put16(wav_buf + at, 16u);
    at += 2;
    for (i = 0; i < 4; i++)
        wav_buf[at++] = (unsigned char)data[i];
    adv_put32(wav_buf + at, data_bytes);
    at += 4;
    for (i = 0; i < samples; i++) {
        int sample = (i % 20u < 10u) ? 8000 : -8000;
        adv_put16(wav_buf + at, (unsigned int)(sample & 0xffff));
        at += 2;
    }
    return at;
}

static int adv_write_file(const char *path, const unsigned char *data,
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
static int adv_setup(const char *log_path, const char *clip_path) {
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

    /* Derive the results directory from the log path so every generated
     * fixture stays under tests/adversarial_1000/multimedia/results/. */
    slash = strrchr(log_path, '/');
    if (slash != NULL && ((size_t)(slash - log_path)) < sizeof(g_dir)) {
        size_t n = (size_t)(slash - log_path);
        __builtin_memcpy(g_dir, log_path, n);
        g_dir[n] = '\0';
    } else {
        snprintf(g_dir, sizeof(g_dir), "results");
    }

    snprintf(g_wav, sizeof(g_wav), "%s/tone.wav", g_dir);
    snprintf(g_trunc_wav, sizeof(g_trunc_wav), "%s/trunc.wav", g_dir);
    snprintf(g_trunc_png, sizeof(g_trunc_png), "%s/trunc.png", g_dir);
    snprintf(g_bad_mp4, sizeof(g_bad_mp4), "%s/trunc.mp4", g_dir);
    snprintf(g_png_path, sizeof(g_png_path), "%s/valid.png", g_dir);
    snprintf(g_clip, sizeof(g_clip), "%s", clip_path);

    /* SDL headless core + one small hidden window + software renderer. */
    g_core = sdl((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS), &e);
    if (!g_core.valid())
        return 1;
    g_have_sdl = 1;
    g_win = window("adv1000", 64, 48, (unsigned)SDL_WINDOW_HIDDEN, &e);
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

    /* Valid image, PNG bytes, and a truncated PNG fixture. */
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
    if (!adv_write_file(g_trunc_png, g_png, 16))
        return 10;

    /* Valid WAV, truncated WAV buffer + file. */
    wav_len = adv_build_wav(wav, 200);
    if (!adv_write_file(g_wav, wav, wav_len))
        return 11;
    g_trunc_wav_len = 16;
    for (x = 0; x < g_trunc_wav_len; x++)
        g_trunc_wav_bytes[x] = wav[x];
    if (!adv_write_file(g_trunc_wav, g_trunc_wav_bytes,
                        (size_t)g_trunc_wav_len))
        return 12;

    /* Audio null backend: context, engine, decoder. */
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

    /* Truncated video container. */
    mp4_fp = fopen(g_clip, "rb");
    if (mp4_fp == NULL)
        return 16;
    mp4_n = fread(mp4, 1, sizeof(mp4), mp4_fp);
    fclose(mp4_fp);
    if (mp4_n < 32)
        return 17;
    if (!adv_write_file(g_bad_mp4, mp4, 32))
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

static void adv_teardown(void) {
    if (g_have_frame)
        g_frame.drop();
    if (g_have_dec)
        g_dec.drop();
    if (g_have_media)
        g_media.drop();
    if (g_have_ad)
        g_ad.drop();
    if (g_have_eng)
        g_eng.drop();
    if (g_have_ctx)
        g_ctx.drop();
    if (g_have_ui)
        g_ui.drop();
    if (g_have_shot)
        g_shot.drop();
    if (g_have_render)
        g_render.drop();
    if (g_have_win)
        g_win.drop();
    if (g_have_sdl)
        g_core.drop();
}

/* ---- window.hp scenarios ---------------------------------------------- */
static struct adv_outcome adv_window(int kind, unsigned long long *r) {
    error e = error();
    int dim = (int)(r[0] % 4097u);
    int dim2 = (int)(r[1] % 4097u);
    int ok = 0;
    int expected = RES_REFUSED;

    switch (kind) {
    case 0: { /* 0 x N / N x 0 dimensions -> refused */
        int w = (r[2] & 1u) ? 0 : dim;
        int h = (r[2] & 1u) ? dim2 : 0;
        window x = window("adv", w, h, (unsigned)SDL_WINDOW_HIDDEN, &e);
        ok = !x.valid() && e.failed_now() && x.native() == (SDL_Window *)0;
        x.drop();
        break;
    }
    case 1: { /* negative dimensions -> refused */
        int w = -((int)(r[0] % 4096u) + 1);
        int h = (r[1] & 1u) ? -((int)(r[1] % 4096u) + 1) : dim2 + 1;
        window x = window("adv", w, h, (unsigned)SDL_WINDOW_HIDDEN, &e);
        ok = !x.valid() && e.failed_now();
        x.drop();
        break;
    }
    case 2: { /* oversized positive dimensions: defined, no corruption */
        int big = 4097 + (int)(r[0] % 200000u);
        window x = window("adv", big, big, (unsigned)SDL_WINDOW_HIDDEN, &e);
        ok = x.valid() && e.ok() && x.native() != (SDL_Window *)0;
        expected = RES_DEFINED;
        x.drop();
        break;
    }
    case 3: { /* size() on NULL handle keeps out-params at sentinel */
        window x = window("adv", 0, 0, (unsigned)SDL_WINDOW_HIDDEN, &e);
        int rc = x.size(&g_arena.ints[0], &g_arena.ints[1], &e);
        ok = rc == STDLIB_FAIL && e.failed_now() &&
             g_arena.ints[0] == SENT_INT && g_arena.ints[1] == SENT_INT;
        x.drop();
        break;
    }
    case 4: { /* set_title(NULL) -> refused */
        window x = window("adv", 8, 8, (unsigned)SDL_WINDOW_HIDDEN, &e);
        int rc = x.set_title((const char *)0, &e);
        ok = rc == STDLIB_FAIL && e.failed_now();
        x.drop();
        break;
    }
    case 5: { /* double drop is idempotent, no crash */
        window x = window("adv", 8, 8, (unsigned)SDL_WINDOW_HIDDEN, &e);
        x.drop();
        int v1 = x.valid();
        x.drop();
        int v2 = x.valid();
        ok = v1 == 0 && v2 == 0 && x.native() == (SDL_Window *)0;
        expected = RES_DEFINED;
        break;
    }
    }
    return adv_result(expected, ok);
}

/* ---- graphics.hp scenarios -------------------------------------------- */
static struct adv_outcome adv_graphics(int kind, unsigned long long *r) {
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
        SDL_Surface *low = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_INDEX8);
        if (low != (SDL_Surface *)0) {
            surface sh = surface(low);
            int rc = sh.pixel(1, 1, g_arena.bytes, &e);
            ok = rc == STDLIB_FAIL && e.failed_now() &&
                 g_arena.bytes[0] == 0 && g_arena.bytes[3] == 0;
            sh.drop();
        }
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
    return adv_result(expected, ok);
}

/* ---- gui.hp scenarios -------------------------------------------------- */
static struct adv_outcome adv_gui(int kind, unsigned long long *r) {
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
        float huge = 1.0e6f + (float)(r[0] % 1000u);
        float coord = (r[1] & 1u) ? -huge : huge;
        int shown;
        g_ui.input_begin();
        shown = g_ui.window_begin("adv-extreme", coord, -huge, huge, huge, 0);
        /* nk_begin ends a clipped window itself; ending it again corrupts
         * Nuklear's frame sequence and trips NK_ASSERT on the next frame. */
        if (shown != 0)
            g_ui.window_end();
        g_ui.input_end();
        /* nk_sdl_render invokes nk_clear, advancing the frame sequence so the
         * next scenario can re-open a window with the same name. */
        g_ui.render(0);
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
    return adv_result(expected, ok);
}

/* ---- input.hp scenarios ------------------------------------------------ */
static struct adv_outcome adv_input(int kind, unsigned long long *r) {
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
    return adv_result(expected, ok);
}

/* ---- image.hp scenarios ------------------------------------------------ */
static struct adv_outcome adv_image(int kind, unsigned long long *r) {
    error e = error();
    int ok = 0;
    int expected = RES_REFUSED;
    int dim = (int)(r[0] % 4097u);
    int dim2 = (int)(r[1] % 4097u);

    switch (kind) {
    case 0: { /* NULL source with random (incl huge) dims -> refused */
        unsigned char tiny[4];
        int huge = 100000 + (int)(r[2] % 100000u);
        __builtin_memset(tiny, 7, 4);
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
        adv_put32(png + 8, 13u);
        png[12] = 'I'; png[13] = 'H'; png[14] = 'D'; png[15] = 'R';
        adv_put32(png + 16, 0xffffffffu);
        adv_put32(png + 20, 0xffffffffu);
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
    return adv_result(expected, ok);
}

/* ---- audio.hp scenarios ------------------------------------------------ */
static struct adv_outcome adv_audio(int kind, unsigned long long *r) {
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
    return adv_result(expected, ok);
}

/* ---- video.hp scenarios ------------------------------------------------ */
static struct adv_outcome adv_video(int kind, unsigned long long *r) {
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
    return adv_result(expected, ok);
}

/* ---- metadata and dispatch --------------------------------------------- */
static void adv_meta(int kind, const char **api, const char **name) {
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

static struct adv_outcome adv_exec(int kind, unsigned long long *r) {
    if (kind < 6)
        return adv_window(kind, r);
    if (kind < 12)
        return adv_graphics(kind - 6, r);
    if (kind < 18)
        return adv_gui(kind - 12, r);
    if (kind < 24)
        return adv_input(kind - 18, r);
    if (kind < 33)
        return adv_image(kind - 24, r);
    if (kind < 42)
        return adv_audio(kind - 33, r);
    return adv_video(kind - 42, r);
}

int main(int argc, char **argv) {
    const char *log_path = "results/scenarios.log";
    const char *clip_path = "build/stdlib-tests/clip.mp4";
    FILE *log;
    int setup_rc;
    int id;
    int kind;
    int refused = 0;
    int sentinel_ok = 0;
    int failures = 0;
    int emitted = 0;
    int log_failures = 0;
    unsigned long long r[3];
    const char *arg;
    char line[192];

    if (argc > 1) {
        __builtin_memcpy(&arg, argv + 1, sizeof(arg));
        if (arg != (const char *)0)
            log_path = arg;
    }
    if (argc > 2) {
        __builtin_memcpy(&arg, argv + 2, sizeof(arg));
        if (arg != (const char *)0)
            clip_path = arg;
    }

    log = fopen(log_path, "w");
    if (log == NULL) {
        fprintf(stderr, "ADV1000: cannot open log path %s\n", log_path);
        return 2;
    }

    setup_rc = adv_setup(log_path, clip_path);
    if (setup_rc != 0)
        fprintf(stderr, "ADV1000: setup failed at step %d\n", setup_rc);

    if (setup_rc == 0) {
        for (id = 0; id < ADV_N; id++) {
            struct adv_outcome o;
            fprintf(stderr, "ADVDBG id=%d\n", id);
            const char *api;
            const char *case_name;
            int n;

            adv_rnd((unsigned long long)id, r);
            kind = (int)((unsigned long long)id % (unsigned long long)ADV_KINDS);
            adv_meta(kind, &api, &case_name);
            adv_arm();
            o = adv_exec(kind, r);

            fprintf(stdout, "S %d %s %s %s %d\n", id, api, case_name,
                    adv_res_name(o.observed), o.sentinel);
            emitted++;

            if (o.observed == RES_REFUSED)
                refused++;
            if (o.sentinel == 1)
                sentinel_ok++;
            if (o.observed == RES_FAIL || o.sentinel != 1)
                failures++;

            n = snprintf(line, sizeof(line), "S %d %s %s %s %d", id, api,
                         case_name, adv_res_name(o.observed), o.sentinel);
            if (n <= 0 || (size_t)n >= sizeof(line)) {
                log_failures++;
            } else if (fprintf(log, "%s\n", line) < 0) {
                log_failures++;
            }
        }
    }

    fclose(log);
    adv_teardown();

    if (setup_rc != 0)
        return 2;
    if (emitted != ADV_N)
        failures++;
    if (log_failures != 0)
        failures++;

    fprintf(stdout, "ADV1000 multimedia 1000 %d sentinel_ok %d\n", refused,
            failures);
    return failures == 0 ? 0 : 1;
}
