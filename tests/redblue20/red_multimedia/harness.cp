/* RED20 / red_multimedia -- fork-per-case attack harness for the seven
 * multimedia/window headers.
 *
 * Scope (headers under attack):
 *   stdlib/graphics.hp, gui.hp, image.hp, video.hp, window.hp, input.hp,
 *   audio.hp
 *
 * Method: exactly N_CASES deterministic cases (fixed SplitMix64 seed + case
 * id).  EVERY case runs in a forked child.  A 2-second per-child alarm and
 * RLIMIT_CORE=0 keep a hang or crash from killing the harness.  A pipe carries
 * the child's classification back before _exit().  Canary guards bracket
 * every buffer an API writes into; a changed guard is CORRUPT.  A hostile
 * input that the API nonetheless reports as success is UNEXPECTED_OK.
 *
 * Outcomes (mutually exclusive, precedence CRASH > CORRUPT > UNEXPECTED_OK >
 * REFUSED):
 *   CRASH         - child terminated by a signal (SIGSEGV / SIGABRT /
 *                   SIGALRM / SIGBUS / ...), or exited without a report.
 *   CORRUPT       - child survived but a canary byte changed.
 *   UNEXPECTED_OK - child survived, canaries intact, but the API reported
 *                   success for an input the contract says must be refused.
 *   REFUSED       - hostile/edge input was refused, or produced a defined,
 *                   safe result, with all canaries intact.
 *
 * Prints exactly one authoritative line on stdout:
 *   RED20 multimedia <cases> <crash> <corrupt> <unexpected_ok> <refused>
 * Per-case detail goes to results/scenarios.log (argv[4]); finding lines go
 * to stderr.  Harness exit status is 0 when it completed (findings are
 * reported, never hidden).
 *
 * Build with the exact flags from method_tests/multimedia/run_all.sh:
 *   ./build/c+ build harness.cp --legacy-c --sanitize=none <sdl3> <stb> \
 *     <miniaudio+frameworks> <nuklear> <ffmpeg> -o harness
 * Run with:
 *   SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy timeout 60 ./harness \
 *     <clip.mp4> <truncated.mp4> <garbage.bin> <scenarios.log>
 */
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/wait.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"
#include "stdlib/image.hp"
#include "stdlib/audio.hp"
#include "stdlib/video.hp"
#include "stdlib/input.hp"

#define N_CASES 2520
#define N_KINDS 42
#define RED_SEED 0x9D2C5680A5B4F1E7ULL

/* ---- outcome codes ----------------------------------------------------- */
enum { OUT_REFUSED = 0, OUT_CORRUPT = 1, OUT_UNEXPECTED = 2 };

struct report {
    int outcome;
    int observed;
    int ms;
};

/* ---- guarded regions --------------------------------------------------- */
struct byte_guard {
    unsigned char pre[32];
    unsigned char buf[4096];
    unsigned char post[32];
};
struct float_guard {
    unsigned char pre[32];
    float buf[256];
    unsigned char post[32];
};
struct int_guard {
    unsigned char pre[32];
    int buf[128];
    unsigned char post[32];
};

static struct byte_guard G_byte;
static struct float_guard G_float;
static struct int_guard G_int;

static void arm_guards(void) {
    __builtin_memset(G_byte.pre, 0xA5, 32);
    __builtin_memset(G_byte.buf, 0x00, 4096);
    __builtin_memset(G_byte.post, 0x5A, 32);
    __builtin_memset(G_float.pre, 0x3C, 32);
    __builtin_memset(G_float.buf, 0, 256 * sizeof(float));
    __builtin_memset(G_float.post, 0xC3, 32);
    __builtin_memset(G_int.pre, 0x7E, 32);
    __builtin_memset(G_int.buf, 0x5A5A5A5A, 128 * sizeof(int));
    __builtin_memset(G_int.post, 0xE7, 32);
}

static int guard_byte_ok(const unsigned char *p, int n, unsigned char want) {
    int i;
    for (i = 0; i < n; i++)
        if (p[i] != want) return 0;
    return 1;
}

static int guards_ok(void) {
    /* Only the bracket regions are invariants.  The buffers themselves are
     * deliberately written by several cases, so their contents are not
     * checked here. */
    int ok = 1;
    if (!guard_byte_ok(G_byte.pre, 32, 0xA5)) ok = 0;
    if (!guard_byte_ok(G_byte.post, 32, 0x5A)) ok = 0;
    if (!guard_byte_ok(G_float.pre, 32, 0x3C)) ok = 0;
    if (!guard_byte_ok(G_float.post, 32, 0xC3)) ok = 0;
    if (!guard_byte_ok(G_int.pre, 32, 0x7E)) ok = 0;
    if (!guard_byte_ok(G_int.post, 32, 0xE7)) ok = 0;
    return ok;
}

/* ---- deterministic PRNG ------------------------------------------------ */
static void red_rnd(unsigned long long id, unsigned long long *out) {
    unsigned long long z = RED_SEED ^ (id * 0x9E3779B97F4A7C15ULL);
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

/* ---- minimal in-memory WAV (mono 8 kHz, 16-bit PCM) -------------------- */
static void make_wav(unsigned char *dst, int frames) {
    int bytes = frames * 2;
    int total = 44 + bytes;
    __builtin_memset(dst, 0, 44);
    dst[0] = 'R'; dst[1] = 'I'; dst[2] = 'F'; dst[3] = 'F';
    dst[4] = (unsigned char)(total - 8);
    dst[5] = (unsigned char)((total - 8) >> 8);
    dst[6] = (unsigned char)((total - 8) >> 16);
    dst[7] = (unsigned char)((total - 8) >> 24);
    dst[8] = 'W'; dst[9] = 'A'; dst[10] = 'V'; dst[11] = 'E';
    dst[12] = 'f'; dst[13] = 'm'; dst[14] = 't'; dst[15] = ' ';
    dst[16] = 16; dst[20] = 1; dst[22] = 1;
    dst[24] = 0x40; dst[25] = 0x1F; /* 8000 Hz */
    dst[28] = 0x80; dst[29] = 0x3E; /* byte rate */
    dst[32] = 2; dst[34] = 16;
    dst[36] = 'd'; dst[37] = 'a'; dst[38] = 't'; dst[39] = 'a';
    dst[40] = (unsigned char)bytes;
    dst[41] = (unsigned char)(bytes >> 8);
    dst[42] = (unsigned char)(bytes >> 16);
    dst[43] = (unsigned char)(bytes >> 24);
    {
        int i;
        for (i = 0; i < bytes; i += 2) {
            dst[44 + i] = (unsigned char)(i * 7);
            dst[45 + i] = (unsigned char)(i * 11);
        }
    }
}

/* ---- child-side case dispatch ------------------------------------------
 * `bad` is set to 1 when a hostile input was nonetheless accepted as success.
 * The final classification combines `bad`, guards_ok() and the signal state
 * (the latter is decided by the parent from waitpid()).
 */
static int run_case(unsigned long long id, int kind, unsigned long long *r,
                    const char *clip, const char *trunc, const char *garbage) {
    error err = error();
    int bad = 0;
    unsigned long long a = r[0], b = r[1], c = r[2];
    int dim = (int)(a % 9);
    int w = (int)(8 + (a % 57));
    int h = (int)(8 + (b % 41));
    char title[64];

    (void)clip; (void)trunc; (void)garbage; /* used in selected cases below */
    sprintf(title, "red%llu", id);
    arm_guards();

    SDL_Init((SDL_InitFlags)(SDL_INIT_VIDEO | SDL_INIT_EVENTS));

    switch (kind) {
    /* ---------------------------- window ------------------------------- */
    case 0: { /* invalid dimensions: 0 / negative / SIZE_MAX-as-int */
        int widths[6];
        int heights[6];
        int i;
        widths[0] = 0;            widths[1] = -1;
        widths[2] = INT_MIN;      widths[3] = (int)SIZE_MAX;
        widths[4] = 0;            widths[5] = -7;
        heights[0] = 8;           heights[1] = 8;
        heights[2] = 8;           heights[3] = 8;
        heights[4] = -1;          heights[5] = 0;
        for (i = 0; i < 6; i++) {
            window w0 = window(title, widths[i], heights[i],
                               (unsigned)SDL_WINDOW_HIDDEN, &err);
            if (w0.valid() || err.ok()) bad = 1;
            w0.drop();
        }
        break;
    }
    case 1: { /* null / invalid window handle, then all methods */
        window w1 = window(NULL, 16, 16, 0, &err);
        int ow = 1234, oh = 5678;
        if (w1.valid()) bad = 1;
        G_int.buf[0] = 9876; G_int.buf[1] = 5432;
        w1.size(&G_int.buf[0], &G_int.buf[1], &err);
        if (G_int.buf[0] != 9876 || G_int.buf[1] != 5432) bad = 1;
        w1.set_title(title, &err);
        w1.set_title(NULL, &err);
        w1.native();
        w1.drop(); w1.drop();
        (void)ow; (void)oh;
        break;
    }
    case 2: { /* valid window, double drop, post-drop operations */
        window w2 = window(title, 16, 12, (unsigned)SDL_WINDOW_HIDDEN, &err);
        if (!w2.valid()) break;
        w2.drop();
        w2.drop();
        if (w2.valid()) bad = 1;
        G_int.buf[2] = 111; G_int.buf[3] = 222;
        w2.size(&G_int.buf[2], &G_int.buf[3], &err);
        if (G_int.buf[2] != 111 || G_int.buf[3] != 222) bad = 1;
        w2.set_title(title, &err);
        break;
    }
    case 3: { /* null out-parameters on a live window */
        window w3 = window(title, 16, 12, (unsigned)SDL_WINDOW_HIDDEN, &err);
        if (!w3.valid()) break;
        w3.size(NULL, &G_int.buf[4], &err);
        w3.size(&G_int.buf[5], NULL, &err);
        w3.size(NULL, NULL, &err);
        w3.set_title(NULL, &err);
        w3.drop();
        break;
    }
    /* --------------------------- graphics ------------------------------ */
    case 4: { /* surface(NULL): every hostile access must refuse */
        surface s4 = surface(NULL);
        unsigned char out4[4];
        if (s4.valid()) bad = 1;
        s4.width(); s4.height();
        if (s4.pixel(0, 0, out4, &err) == STDLIB_OK) bad = 1;
        if (s4.pixel(0, 0, NULL, &err) == STDLIB_OK) bad = 1;
        if (s4.pixel(INT_MAX, INT_MAX, out4, &err) == STDLIB_OK) bad = 1;
        s4.drop(); s4.drop();
        break;
    }
    case 5: { /* out-of-range coordinates on a real 32bpp surface */
        SDL_Surface *raw5 = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_RGBA8888);
        surface s5 = surface(raw5);
        unsigned char *out5 = G_byte.buf;
        int xs[6]; int ys[6]; int i;
        xs[0] = -1;  xs[1] = w;      xs[2] = INT_MIN;
        xs[3] = INT_MAX; xs[4] = 0;  xs[5] = w - 1;
        ys[0] = 0;   ys[1] = 0;      ys[2] = 0;
        ys[3] = 0;   ys[4] = h;      ys[5] = h - 1;
        if (!s5.valid()) break;
        for (i = 0; i < 4; i++)
            if (s5.pixel(xs[i], ys[i], out5, &err) == STDLIB_OK) bad = 1;
        __builtin_memset(out5, 0xCC, 32);
        if (s5.pixel(w / 2, h / 2, out5, &err) != STDLIB_OK) bad = 1;
        if (!guard_byte_ok(out5 + 4, 28, 0xCC)) bad = 1; /* exactly 4 bytes */
        s5.drop(); s5.drop();
        break;
    }
    case 6: { /* invalid pixel formats: bpp < 32 must be refused */
        Uint32 fmts[5];
        SDL_Surface *raw6;
        surface s6;
        int i;
        fmts[0] = SDL_PIXELFORMAT_RGB24;   /* 24 bpp -> must refuse */
        fmts[1] = SDL_PIXELFORMAT_RGB565;  /* 16 bpp -> must refuse */
        fmts[2] = SDL_PIXELFORMAT_INDEX8;  /*  8 bpp -> must refuse */
        fmts[3] = SDL_PIXELFORMAT_UNKNOWN; /* no masks -> must refuse */
        fmts[4] = SDL_PIXELFORMAT_RGBA8888;/* 32 bpp -> defined success */
        for (i = 0; i < 5; i++) {
            raw6 = SDL_CreateSurface(8, 8, fmts[i]);
            if (raw6 == NULL) continue;
            s6 = surface(raw6);
            {
                int rc6 = s6.pixel(0, 0, G_byte.buf, &err);
                if (i < 4) {
                    if (rc6 == STDLIB_OK) bad = 1;
                } else if (rc6 != STDLIB_OK) {
                    bad = 1; /* valid 32bpp format unexpectedly refused */
                }
            }
            s6.drop();
        }
        break;
    }
    case 7: { /* crafted short pitch: row pitch guard must refuse */
        unsigned char *mem7 = (unsigned char *)malloc((size_t)w * h * 4 + 64);
        SDL_Surface *raw7;
        surface s7;
        if (mem7 == NULL) break;
        __builtin_memset(mem7, 0, (size_t)w * h * 4 + 64);
        raw7 = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA8888,
                                     mem7, w * 2);
        if (raw7 == NULL) { free(mem7); break; }
        s7 = surface(raw7);
        if (s7.pixel(w - 1, h - 1, G_byte.buf, &err) == STDLIB_OK) bad = 1;
        s7.drop(); /* SDL_DestroySurface does not free caller memory */
        free(mem7);
        break;
    }
    case 8: { /* renderer NULL handle: every method must refuse */
        renderer r8 = renderer(NULL, "software", &err);
        if (r8.valid() || r8.native() != NULL || err.ok()) bad = 1;
        r8.set_color(1, 2, 3, 4, &err);
        r8.clear(&err);
        r8.fill_rect(0, 0, 1, 1, &err);
        r8.line(0, 0, 1, 1, &err);
        r8.present(&err);
        r8.set_vsync(0, &err);
        {
            surface s8 = r8.read(&err);
            if (s8.valid()) bad = 1;
        }
        r8.drop(); r8.drop();
        break;
    }
    case 9: { /* live renderer: NaN/inf/huge rects, line, double drop */
        window w9 = window(title, 32, 24, (unsigned)SDL_WINDOW_HIDDEN, &err);
        renderer r9;
        if (!w9.valid()) break;
        r9 = renderer(w9.native(), "software", &err);
        if (!r9.valid()) { w9.drop(); break; }
        r9.set_color(255, 0, 0, 255, &err);
        r9.fill_rect(NAN, INFINITY, -INFINITY, HUGE_VAL, &err);
        r9.fill_rect(-1.0e30f, -1.0e30f, 1.0e30f, 1.0e30f, &err);
        r9.line(NAN, NAN, INFINITY, -INFINITY, &err);
        r9.present(&err);
        r9.drop(); r9.drop();
        r9.clear(&err); r9.read(&err);
        w9.drop(); w9.drop();
        break;
    }
    case 10: { /* renderer::read after drop / on NULL surface out */
        window w10 = window(title, 16, 16, (unsigned)SDL_WINDOW_HIDDEN, &err);
        renderer r10;
        if (!w10.valid()) break;
        r10 = renderer(w10.native(), "software", &err);
        if (r10.valid()) {
            surface s10 = r10.read(&err);
            if (s10.valid()) {
                s10.pixel(INT_MAX, 0, G_byte.buf, &err);
                s10.drop();
            }
            r10.drop();
            {
                surface s10b = r10.read(&err);
                if (s10b.valid()) bad = 1;
            }
        }
        w10.drop();
        break;
    }
    /* ----------------------------- input ------------------------------- */
    case 11: { /* event helpers with NULL and malformed events */
        SDL_Event ev11;
        if (event_is_quit(NULL) || event_is_key_down(NULL) ||
            event_is_key_up(NULL)) bad = 1;
        if (event_scancode(NULL) != SDL_SCANCODE_UNKNOWN) bad = 1;
        if (event_text(NULL) != '\0') bad = 1;
        __builtin_memset(&ev11, 0, sizeof(ev11));
        ev11.type = SDL_EVENT_KEY_DOWN;
        ev11.key.scancode = (SDL_Scancode)INT_MAX;
        event_scancode(&ev11);
        ev11.type = SDL_EVENT_TEXT_INPUT;
        ev11.text.text = NULL;
        if (event_text(&ev11) != '\0') bad = 1;
        ev11.text.text = (char *)G_byte.buf;
        event_text(&ev11);
        ev11.type = (Uint32)(a % 100000U);
        event_is_quit(&ev11);
        break;
    }
    case 12: { /* key_pressed OOB scancode + mouse_state canary */
        const bool *st12 = keyboard_state();
        key_pressed(NULL, SDL_SCANCODE_A);
        key_pressed(st12, (SDL_Scancode)INT_MIN);
        key_pressed(st12, (SDL_Scancode)INT_MAX);
        key_pressed(st12, (SDL_Scancode)SDL_SCANCODE_COUNT);
        G_float.buf[0] = 1234.5f;
        G_float.buf[1] = 6789.5f;
        mouse_state(&G_float.buf[0], &G_float.buf[1]);
        mouse_state(NULL, NULL);
        mouse_button_left((unsigned)a);
        (void)dim;
        break;
    }
    /* ----------------------------- image ------------------------------- */
    case 13: { /* image_from_pixels invalid dims / channels */
        int widths[6]; int heights[6]; int chans[6]; int i;
        widths[0] = 0;  widths[1] = -1; widths[2] = 4;
        widths[3] = 4;  widths[4] = 4;  widths[5] = INT_MIN;
        heights[0] = 3; heights[1] = 3; heights[2] = 0;
        heights[3] = -3; heights[4] = 3; heights[5] = 3;
        chans[0] = 3; chans[1] = 3; chans[2] = 3;
        chans[3] = 3; chans[4] = 0; chans[5] = -1;
        for (i = 0; i < 6; i++) {
            image im13 = image_from_pixels(G_byte.buf, widths[i], heights[i],
                                           chans[i], &err);
            if (im13.valid() || err.ok()) bad = 1;
            im13.drop();
        }
        {
            image im13n = image_from_pixels(NULL, 4, 3, 3, &err);
            if (im13n.valid() || err.ok()) bad = 1;
        }
        break;
    }
    case 14: { /* size_t overflow / absurd byte count.
                 * The huge allocation is expensive (~350 ms per child on
                 * macOS), so it is exercised on 1 in 8 ids; the rest use a
                 * cheaply rejected dimension to keep the suite under 60 s. */
        if ((a & 7ULL) == 0) {
            image im14 = image_from_pixels(G_byte.buf, INT_MAX, INT_MAX, 4, &err);
            if (im14.valid() || err.ok()) bad = 1;
            im14.drop();
            /* The byte count fits size_t but is absurd; malloc may hand back
             * an overcommitted pointer and the unchecked memcpy then reads
             * past the caller's small source buffer. */
            {
                image im14c = image_from_pixels(G_byte.buf, 2, INT_MAX, 4, &err);
                if (im14c.valid() || err.ok()) bad = 1;
                im14c.drop();
            }
        } else {
            image im14b = image_from_pixels(G_byte.buf, 2, 0, 4, &err);
            if (im14b.valid() || err.ok()) bad = 1;
            im14b.drop();
        }
        break;
    }
    case 15: { /* invalid pixel format channels > 4, then write */
        int ch15 = (int)(5 + (a % 2000));
        image im15 = image_from_pixels(G_byte.buf, 2, 2, ch15, &err);
        if (!im15.valid()) { /* allocation of 4*ch could fail; still safe */
            break;
        }
        __builtin_memset(G_byte.buf, 0x11, 4096);
        if (im15.write_png("results/red15.png", &err) == STDLIB_OK) bad = 1;
        if (im15.write_bmp("results/red15.bmp", &err) == STDLIB_OK) bad = 1;
        im15.drop();
        break;
    }
    case 16: { /* image ctor: invalid desired_channels / null path */
        int dc[5];
        int i;
        dc[0] = -1; dc[1] = 5; dc[2] = INT_MIN;
        dc[3] = INT_MAX; dc[4] = 4;
        for (i = 0; i < 4; i++) {
            image im16 = image("results/red_no_such_file.png", dc[i], &err);
            if (err.ok()) bad = 1;
            im16.drop();
        }
        {
            image im16n = image(NULL, 0, &err);
            if (im16n.valid()) bad = 1;
            im16n.drop();
        }
        break;
    }
    case 17: { /* image_from_memory truncated / garbage / bad channels */
        int n17 = (int)(1 + (a % 256));
        int dc17 = (int)(b % 7) - 1;
        image im17 = image_from_memory(G_byte.buf, n17, dc17, &err);
        if (dc17 < 0 || dc17 > 4) {
            if (err.ok() || im17.valid()) bad = 1;
        }
        im17.drop();
        {
            image im17n = image_from_memory(NULL, 16, 3, &err);
            if (im17n.valid() || err.ok()) bad = 1;
            im17n.drop();
        }
        {
            image im17z = image_from_memory(G_byte.buf, 0, 3, &err);
            if (im17z.valid() || err.ok()) bad = 1;
            im17z.drop();
        }
        break;
    }
    case 18: { /* image pixel out-of-range + double drop + write after */
        image im18 = image_from_pixels(G_byte.buf, 4, 3, 3, &err);
        if (!im18.valid()) break;
        if (im18.pixel(-1, 0) != NULL) bad = 1;
        if (im18.pixel(0, -1) != NULL) bad = 1;
        if (im18.pixel(4, 0) != NULL) bad = 1;
        if (im18.pixel(0, 3) != NULL) bad = 1;
        if (im18.pixel(INT_MAX, 0) != NULL) bad = 1;
        im18.drop(); im18.drop();
        if (im18.valid()) bad = 1;
        if (im18.write_png("results/red18.png", &err) == STDLIB_OK) bad = 1;
        break;
    }
    /* ----------------------------- audio ------------------------------- */
    case 19: { /* audio_sound invalid engine / path */
        audio_sound s19 = audio_sound(NULL, "results/red.wav", &err);
        if (s19.valid() || err.ok()) bad = 1;
        s19.play(&err); s19.stop(&err);
        s19.playing();
        s19.drop(); s19.drop();
        {
            audio_context ac19 = audio_context(1, &err);
            if (ac19.valid()) {
                audio_engine ae19 = audio_engine(ac19.native(), &err);
                if (ae19.valid()) {
                    audio_sound s19b = audio_sound(ae19.native(), NULL, &err);
                    if (s19b.valid() || err.ok()) bad = 1;
                    s19b.drop();
                    ae19.drop();
                }
                ac19.drop(); ac19.drop();
            }
        }
        break;
    }
    case 20: { /* engine invalid args + hostile volume values */
        audio_context ac20 = audio_context(1, &err);
        if (!ac20.valid()) break;
        /* NOTE: audio_engine(NULL) builds its own real-device context and can
         * block indefinitely on CoreAudio; intentionally not exercised here. */
        {
            audio_engine ae20b = audio_engine(ac20.native(), &err);
            if (ae20b.valid()) {
                if (ae20b.play(NULL, &err) == STDLIB_OK) bad = 1;
                {
                    audio_sound s20 = ae20b.load(NULL, &err);
                    if (s20.valid() || err.ok()) bad = 1;
                    s20.drop();
                }
                ae20b.set_volume(NAN, &err);
                ae20b.set_volume(INFINITY, &err);
                ae20b.set_volume(-1.0f, &err);
                ae20b.set_volume(1.0e30f, &err);
                ae20b.drop(); ae20b.drop();
                ae20b.set_volume(0.5f, &err);
            }
        }
        ac20.drop();
        break;
    }
    case 21: { /* audio_device invalid context / rates / channels */
        audio_device d21 = audio_device(NULL, 0, 44100, 2, NULL, NULL, &err);
        if (d21.valid() || err.ok()) bad = 1;
        d21.start(&err); d21.stop(&err); d21.running();
        d21.drop(); d21.drop();
        {
            audio_context ac21 = audio_context(1, &err);
            unsigned rates[4]; unsigned chans[4]; int i;
            rates[0] = 0; rates[1] = 1; rates[2] = UINT_MAX; rates[3] = 8000;
            chans[0] = 0; chans[1] = 1; chans[2] = UINT_MAX; chans[3] = 2;
            if (ac21.valid()) {
                for (i = 0; i < 4; i++) {
                    audio_device d21b = audio_device(ac21.native(), (int)(c & 1),
                                                     rates[i], chans[i],
                                                     NULL, NULL, &err);
                    if (d21b.valid()) {
                        d21b.start(&err); d21b.stop(&err);
                        d21b.drop();
                    }
                }
                ac21.drop();
            }
        }
        break;
    }
    case 22: { /* audio_decoder invalid path / double drop */
        audio_decoder ad22 = audio_decoder(NULL, &err);
        if (ad22.valid() || err.ok()) bad = 1;
        ad22.channels(); ad22.sample_rate(); ad22.frame_count();
        ad22.read(G_float.buf, 16, &err);
        ad22.drop(); ad22.drop();
        {
            audio_decoder ad22b = audio_decoder("results/red_no_such.wav", &err);
            if (ad22b.valid() || err.ok()) bad = 1;
            ad22b.drop();
        }
        break;
    }
    case 23: { /* decoder_from_memory: null / garbage / truncated */
        int n23 = (int)(1 + (b % 128));
        audio_decoder ad23 = decoder_from_memory(NULL, 16, &err);
        if (ad23.valid() || err.ok()) bad = 1;
        ad23.drop();
        {
            audio_decoder ad23z = decoder_from_memory(G_byte.buf, 0, &err);
            if (ad23z.valid() || err.ok()) bad = 1;
            ad23z.drop();
        }
        {
            audio_decoder ad23g = decoder_from_memory(G_byte.buf, (size_t)n23, &err);
            ad23g.drop();
        }
        break;
    }
    case 24: { /* audio_decoder::read with huge count into small guarded buf */
        unsigned char wav[2100];
        audio_decoder ad24;
        unsigned long long count24;
        unsigned long long got24;
        int frames24 = (int)(64 + (a % 512));
        make_wav(wav, frames24);
        ad24 = decoder_from_memory(wav, (size_t)(44 + frames24 * 2), &err);
        if (!ad24.valid()) break;
        __builtin_memset(G_float.pre, 0x3C, 32);
        __builtin_memset(G_float.buf, 0xCC, 256 * sizeof(float));
        __builtin_memset(G_float.post, 0xC3, 32);
        count24 = (unsigned long long)frames24;
        got24 = ad24.read(G_float.buf, count24, &err);
        (void)got24;
        if (!guard_byte_ok(G_float.pre, 32, 0x3C) ||
            !guard_byte_ok(G_float.post, 32, 0xC3)) {
            bad = 1; /* overwritten guard */
        }
        ad24.drop();
        break;
    }
    case 25: { /* audio_context double drop + native queries */
        audio_context ac25 = audio_context(1, &err);
        if (!ac25.valid()) break;
        ac25.native();
        ac25.drop(); ac25.drop();
        if (ac25.valid()) bad = 1;
        break;
    }
    /* ------------------------------ gui -------------------------------- */
    case 26: { /* gui NULL handles + all methods on invalid object */
        gui g26 = gui(NULL, NULL, &err);
        if (g26.valid() || err.ok()) bad = 1;
        g26.native();
        g26.handle_event(NULL);
        g26.input_begin(); g26.input_motion(0, 0);
        g26.input_button(1); g26.input_end();
        if (g26.window_begin(title, 0, 0, 10, 10, 0)) bad = 1;
        g26.window_end();
        g26.layout_row_dynamic(10, 1);
        g26.layout_row_static(10, 10, 1);
        if (g26.button("x")) bad = 1;
        g26.label("x");
        if (g26.slider(&G_int.buf[0], 0, 10)) bad = 1;
        if (g26.widget_bounds(&G_int.buf[0], &G_int.buf[1],
                              &G_int.buf[2], &G_int.buf[3])) bad = 1;
        g26.render(0);
        g26.style_text_color(1, 2, 3);
        g26.drop(); g26.drop();
        break;
    }
    case 27: { /* gui valid: extreme / NaN window rects */
        window w27 = window(title, 240, 160, (unsigned)SDL_WINDOW_HIDDEN, &err);
        renderer r27;
        gui g27;
        float fx, fy, fw, fh;
        int shown27;
        if (!w27.valid()) break;
        r27 = renderer(w27.native(), "software", &err);
        if (!r27.valid()) { w27.drop(); break; }
        g27 = gui(w27.native(), r27.native(), &err);
        if (!g27.valid()) { r27.drop(); w27.drop(); break; }
        fx = (a & 1) ? NAN : (float)(INT_MIN);
        fy = (b & 1) ? INFINITY : (float)(INT_MAX);
        fw = (c & 1) ? -1.0e30f : 1.0e30f;
        fh = (a & 2) ? 0.0f : 1.0e30f;
        g27.input_begin(); g27.input_end();
        shown27 = g27.window_begin(title, fx, fy, fw, fh, 0);
        if (shown27) {
            g27.layout_row_dynamic(20.0f, 1);
            g27.label(title);
            g27.widget_bounds(&G_int.buf[0], &G_int.buf[1],
                              &G_int.buf[2], &G_int.buf[3]);
            g27.window_end();
        }
        g27.drop(); r27.drop(); w27.drop();
        break;
    }
    case 28: { /* gui valid: bad slider args + extreme motion + events */
        window w28 = window(title, 200, 120, (unsigned)SDL_WINDOW_HIDDEN, &err);
        renderer r28;
        gui g28;
        SDL_Event ev28;
        if (!w28.valid()) break;
        r28 = renderer(w28.native(), "software", &err);
        if (!r28.valid()) { w28.drop(); break; }
        g28 = gui(w28.native(), r28.native(), &err);
        if (!g28.valid()) { r28.drop(); w28.drop(); break; }
        if (g28.slider(NULL, 0, 10) != 0) bad = 1;
        if (g28.slider(&G_int.buf[0], 10, 0) != 0) bad = 1;
        /* Calling a widget with no active window/layout makes Nuklear assert.
         * Exercise that abort path on 1 in 8 ids only (each abort costs
         * ~330 ms of OS crash-reporting time). */
        if ((a & 7ULL) == 0)
            g28.slider(&G_int.buf[0], INT_MIN, INT_MAX);
        g28.input_motion(INT_MIN, INT_MAX);
        g28.input_button(1);
        g28.input_button(0);
        __builtin_memset(&ev28, 0, sizeof(ev28));
        ev28.type = SDL_EVENT_TEXT_INPUT;
        ev28.text.text = (char *)G_byte.buf;
        g28.handle_event(&ev28);
        ev28.type = (Uint32)(a % 100000U);
        g28.handle_event(&ev28);
        g28.drop(); r28.drop(); w28.drop();
        break;
    }
    case 29: { /* gui valid: double drop + post-drop methods */
        window w29 = window(title, 160, 120, (unsigned)SDL_WINDOW_HIDDEN, &err);
        renderer r29;
        gui g29;
        if (!w29.valid()) break;
        r29 = renderer(w29.native(), "software", &err);
        if (!r29.valid()) { w29.drop(); break; }
        g29 = gui(w29.native(), r29.native(), &err);
        if (!g29.valid()) { r29.drop(); w29.drop(); break; }
        g29.drop(); g29.drop();
        if (g29.valid()) bad = 1;
        g29.handle_event(NULL);
        g29.input_begin(); g29.input_end();
        if (g29.window_begin(title, 0, 0, 10, 10, 0)) bad = 1;
        if (g29.button("x")) bad = 1;
        if (g29.widget_bounds(NULL, NULL, NULL, NULL)) bad = 1;
        g29.render(1);
        g29.style_text_color(9, 9, 9);
        r29.drop(); w29.drop();
        break;
    }
    /* ----------------------------- video ------------------------------- */
    case 30: { /* video helpers with NULL / boundary args */
        char why[128];
        if (video_stream_params(NULL, 0) != NULL) bad = 1;
        if (video_stream_params(NULL, -1) != NULL) bad = 1;
        if (video_stream_params(NULL, INT_MAX) != NULL) bad = 1;
        if (video_codec_id(NULL) != AV_CODEC_ID_NONE) bad = 1;
        video_error_text(-1, NULL, 0);
        video_error_text(AVERROR(EINVAL), why, 0);
        video_error_text(AVERROR(EINVAL), why, sizeof(why));
        break;
    }
    case 31: { /* decoder null / out-of-range stream */
        decoder d31 = decoder(NULL, -1, &err);
        if (d31.valid() || err.ok()) bad = 1;
        d31.width(); d31.height();
        d31.drop(); d31.drop();
        {
            decoder d31b = decoder(NULL, (int)a, &err);
            if (d31b.valid()) bad = 1;
            d31b.drop();
        }
        break;
    }
    case 32: { /* media(NULL) + methods on empty object */
        media m32 = media(NULL, &err);
        if (m32.valid() || err.ok()) bad = 1;
        m32.stream_count();
        m32.video_index();
        m32.video_width(); m32.video_height();
        m32.video_codec(); m32.duration_ms();
        {
            decoder d32 = m32.open_decoder(&err);
            if (d32.valid()) bad = 1;
            d32.drop();
        }
        m32.drop(); m32.drop();
        break;
    }
    case 33: { /* media nonexistent path */
        media m33 = media("results/red_no_such_clip.mp4", &err);
        if (m33.valid() || err.ok()) bad = 1;
        m33.stream_count(); m33.video_width();
        m33.drop();
        break;
    }
    case 34: { /* truncated / garbage media paths */
        media m34 = media(trunc, &err);
        if (m34.valid() && m34.video_index() >= 0 && m34.video_width() <= 0)
            bad = 1;
        if (m34.valid()) {
            decoder d34 = m34.open_decoder(&err);
            {
                frame f34 = frame(&err);
                if (d34.valid()) d34.next_frame(&f34, &err);
                f34.drop();
            }
            d34.drop();
        }
        m34.drop(); m34.drop();
        {
            media m34g = media(garbage, &err);
            m34g.drop();
        }
        break;
    }
    case 35: { /* valid media: metadata + double drop + decoder after */
        media m35 = media(clip, &err);
        if (!m35.valid()) break;
        m35.stream_count(); m35.video_index();
        m35.video_width(); m35.video_height();
        m35.video_codec(); m35.duration_ms();
        {
            decoder d35 = m35.open_decoder(&err);
            if (!d35.valid()) bad = 1;
            d35.drop();
        }
        m35.drop(); m35.drop();
        if (m35.valid()) bad = 1;
        {
            decoder d35b = m35.open_decoder(&err);
            if (d35b.valid()) bad = 1;
            d35b.drop();
        }
        break;
    }
    case 36: { /* frame ctor/drop twice + next_frame hostile args */
        frame f36 = frame(&err);
        if (!f36.valid() || !err.ok()) bad = 1;
        f36.drop(); f36.drop();
        if (f36.valid()) bad = 1;
        f36.width(); f36.height(); f36.format(); f36.pts();
        {
            media m36 = media(clip, &err);
            if (m36.valid()) {
                decoder d36 = m36.open_decoder(&err);
                if (d36.valid()) {
                    if (d36.next_frame(NULL, &err) == STDLIB_OK) bad = 1;
                    if (d36.next_frame(&f36, &err) == STDLIB_OK) bad = 1;
                    d36.drop();
                }
                m36.drop();
            }
        }
        break;
    }
    case 37: { /* decoder::next_frame after decoder drop */
        media m37 = media(clip, &err);
        if (!m37.valid()) break;
        {
            decoder d37 = m37.open_decoder(&err);
            frame f37 = frame(&err);
            if (d37.valid() && f37.valid()) {
                d37.drop(); d37.drop();
                if (d37.valid()) bad = 1;
                if (d37.next_frame(&f37, &err) == STDLIB_OK) bad = 1;
            }
            f37.drop();
        }
        m37.drop();
        break;
    }
    case 38: { /* lifetime: decoder outlives media (deliberate UAF probe) */
        media m38 = media(clip, &err);
        if (!m38.valid()) break;
        {
            decoder d38 = m38.open_decoder(&err);
            frame f38 = frame(&err);
            if (d38.valid() && f38.valid()) {
                /* Deliberate use-after-free; ~350 ms of OS crash reporting
                 * per child, so only 1 in 8 ids exercises it. */
                if ((a & 7ULL) == 0) {
                    m38.drop(); /* owner released before borrower */
                    d38.next_frame(&f38, &err);
                }
            }
            f38.drop();
            d38.drop();
        }
        break;
    }
    case 39: { /* surface::pixel must not write more than 4 bytes */
        SDL_Surface *raw39 = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_RGBA8888);
        surface s39 = surface(raw39);
        unsigned char *out39 = G_byte.buf;
        if (!s39.valid()) break;
        __builtin_memset(out39, 0xCC, 64);
        s39.pixel(4, 4, out39, &err);
        if (!guard_byte_ok(out39 + 4, 60, 0xCC)) bad = 1;
        s39.drop();
        break;
    }
    case 40: { /* SDL window extreme but positive dimensions (defined) */
        window w40 = window(title, INT_MAX, INT_MAX,
                            (unsigned)SDL_WINDOW_HIDDEN, &err);
        int ww = 0, hh = 0;
        w40.size(&ww, &hh, &err);
        w40.drop();
        break;
    }
    case 41: { /* image_from_pixels huge-but-valid allocation (defined) */
        image im41 = image_from_pixels(G_byte.buf, (int)(1 + (a % 64)),
                                       (int)(1 + (b % 64)), 4, &err);
        if (im41.valid()) {
            im41.width(); im41.height(); im41.channels();
            im41.size_bytes();
            im41.pixel(0, 0);
            im41.drop();
        }
        break;
    }
    default:
        break;
    }

    if (!guards_ok()) return OUT_CORRUPT;
    if (bad) return OUT_UNEXPECTED;
    return OUT_REFUSED;
}

/* ---- parent ------------------------------------------------------------- */
int main(int argc, char **argv) {
    const char *clip = "build/stdlib-tests/clip.mp4";
    const char *trunc = "results/trunc.mp4";
    const char *garbage = "results/garbage.bin";
    const char *logpath = "results/scenarios.log";
    int logfd;
    unsigned long long id;
    long long n_crash = 0, n_corrupt = 0, n_unexpected = 0, n_refused = 0;
    struct rlimit core0;
    char line[192];

    if (argc > 1) clip = argv[1];
    if (argc > 2) trunc = argv[2];
    if (argc > 3) garbage = argv[3];
    if (argc > 4) logpath = argv[4];

    logfd = open(logpath, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) { fprintf(stderr, "cannot open %s\n", logpath); return 70; }
    {
        const char *hdr = "# id kind outcome observed ms\n";
        ssize_t nw = write(logfd, hdr, strlen(hdr));
        (void)nw;
    }

    core0.rlim_cur = 0;
    core0.rlim_max = 0;
    (void)setrlimit(RLIMIT_CORE, &core0);

    for (id = 0; id < N_CASES; id++) {
        int kind = (int)(id % N_KINDS);
        unsigned long long r[3];
        struct report rep;
        int pfd[2];
        pid_t pid;
        int st = 0;
        ssize_t got;
        const char *name;
        long observed = -1;
        struct timespec t0, t1;
        int ms;

        red_rnd(id, r);
        __builtin_memset(&rep, 0, sizeof(rep));
        /* Flush inherited stdio buffers before forking: a child that dies by
         * signal must not re-flush a duplicate of the parent's pending data. */
        fflush(stdout);
        fflush(stderr);
        if (pipe(pfd) != 0) { perror("pipe"); close(logfd); return 70; }
        clock_gettime(CLOCK_MONOTONIC, &t0);
        pid = fork();
        if (pid < 0) { perror("fork"); close(logfd); return 70; }
        if (pid == 0) {
            struct rlimit c0;
            c0.rlim_cur = 0; c0.rlim_max = 0;
            (void)setrlimit(RLIMIT_CORE, &c0);
            alarm(2);
            close(pfd[0]);
            close(logfd); /* unbuffered fd; never written by a child */
            __builtin_memset(&rep, 0, sizeof(rep));
            rep.outcome = run_case(id, kind, r, clip, trunc, garbage);
            rep.observed = 0;
            {
                ssize_t w = write(pfd[1], &rep, sizeof(rep));
                (void)w;
            }
            close(pfd[1]);
            _exit(0);
        }
        close(pfd[1]);
        got = read(pfd[0], &rep, sizeof(rep));
        close(pfd[0]);
        waitpid(pid, &st, 0);
        clock_gettime(CLOCK_MONOTONIC, &t1);
        ms = (int)((t1.tv_sec - t0.tv_sec) * 1000 +
                   (t1.tv_nsec - t0.tv_nsec) / 1000000);

        if (WIFSIGNALED(st)) {
            name = "CRASH";
            observed = (long)WTERMSIG(st);
            n_crash++;
        } else if (got != (ssize_t)sizeof(rep)) {
            name = "CRASH"; /* child exited without a report */
            observed = (long)WEXITSTATUS(st);
            n_crash++;
        } else if (rep.outcome == OUT_CORRUPT) {
            name = "CORRUPT";
            observed = (long)rep.observed;
            n_corrupt++;
        } else if (rep.outcome == OUT_UNEXPECTED) {
            name = "UNEXPECTED_OK";
            observed = (long)rep.observed;
            n_unexpected++;
        } else {
            name = "REFUSED";
            observed = (long)rep.observed;
            n_refused++;
        }

        {
            int nline = snprintf(line, sizeof(line), "%llu %d %s %ld %d\n",
                                 (unsigned long long)id, kind, name,
                                 observed, ms);
            if (nline > 0) {
                ssize_t nw = write(logfd, line, (size_t)nline);
                (void)nw;
            }
        }
        if (strcmp(name, "REFUSED") != 0)
            fprintf(stderr, "FIND id=%llu kind=%d %s observed=%ld\n",
                    (unsigned long long)id, kind, name, observed);
    }
    close(logfd);

    printf("RED20 multimedia %llu %lld %lld %lld %lld\n",
           (unsigned long long)N_CASES, n_crash, n_corrupt, n_unexpected,
           n_refused);
    return 0;
}
