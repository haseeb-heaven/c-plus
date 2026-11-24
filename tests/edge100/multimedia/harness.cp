/* EDGE100 multimedia edge-case harness.
 *
 * Covers EVERY public API of stdlib/{graphics,gui,image,video,window,input,
 * audio}.hp with valid / NULL / zero-or-oversized dimension / invalid format /
 * truncated-media / double-drop / out-of-range-coordinate cases.
 *
 * Fork-per-case: the parent forks a child for each case, bounds the child to
 * 60 seconds (alarm + SIGKILL), and reaps it.  Each child runs exactly one
 * case, prints one line, and exits.  A crash or timeout is therefore isolated
 * to a single case and reported as FAIL by the parent.
 *
 * Modes:
 *   harness list          -> "ID HEADER METHOD TAG" per case
 *   harness apis          -> number of distinct public APIs covered
 *   harness <id>          -> run one case (forked, 60s bound)
 *   harness all           -> run every case, print EDGE100 summary line
 *
 * Build/run recipe: see run.sh in this directory.
 */
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/input.hp"
#include "stdlib/image.hp"
#include "stdlib/audio.hp"
#include "stdlib/gui.hp"
#include "stdlib/video.hp"

#define VFLAGS ((unsigned)(SDL_INIT_VIDEO | SDL_INIT_EVENTS))
#define WFLAGS ((unsigned)SDL_WINDOW_HIDDEN)

/* ---- result reporting --------------------------------------------------- */
#define REPORT(H, M, T, OK)                                                    \
    do {                                                                       \
        int _rok = (int)(OK);                                                  \
        printf("%s %s %s %s\n", (H), (M), (T), _rok ? "PASS" : "FAIL");        \
        fflush(stdout);                                                        \
        return _rok ? 0 : 1;                                                   \
    } while (0)

/* ---- file fixtures (per-process paths avoid cross-agent collisions) ----- */
static void put_u16(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
}

static void put_u32(unsigned char *p, unsigned v) {
    p[0] = (unsigned char)(v & 0xffu);
    p[1] = (unsigned char)((v >> 8) & 0xffu);
    p[2] = (unsigned char)((v >> 16) & 0xffu);
    p[3] = (unsigned char)((v >> 24) & 0xffu);
}

static const char *wav_path(void) {
    static char p[128];
    snprintf(p, sizeof(p), "/tmp/edge100_mm_%d.wav", (int)getpid());
    return p;
}

static const char *bad_wav_path(void) {
    static char p[128];
    snprintf(p, sizeof(p), "/tmp/edge100_mm_%d_bad.wav", (int)getpid());
    return p;
}

static const char *png_path(void) {
    static char p[128];
    snprintf(p, sizeof(p), "/tmp/edge100_mm_%d.png", (int)getpid());
    return p;
}

static const char *bad_png_path(void) {
    static char p[128];
    snprintf(p, sizeof(p), "/tmp/edge100_mm_%d_bad.png", (int)getpid());
    return p;
}

static const char *media_path(void) {
    static char p[128];
    snprintf(p, sizeof(p), "/tmp/edge100_mm_%d.mp4", (int)getpid());
    return p;
}

static const char *bad_media_path(void) {
    static char p[128];
    snprintf(p, sizeof(p), "/tmp/edge100_mm_%d_bad.mp4", (int)getpid());
    return p;
}

static void write_file(const char *path, const void *data, size_t n) {
    FILE *f = fopen(path, "wb");
    if (f != NULL) {
        if (n > 0)
            (void)fwrite(data, 1, n, f);
        fclose(f);
    }
}

/* 8 kHz, 16-bit mono square wave. */
static unsigned build_wav(unsigned char *buf, unsigned samples) {
    unsigned data_bytes = samples * 2u;
    unsigned at = 0;
    unsigned i;
    const char *riff = "RIFF";
    const char *wave = "WAVE";
    const char *fmt = "fmt ";
    const char *data = "data";
    for (i = 0; i < 4; i++) buf[at++] = (unsigned char)riff[i];
    put_u32(buf + at, 36u + data_bytes); at += 4;
    for (i = 0; i < 4; i++) buf[at++] = (unsigned char)wave[i];
    for (i = 0; i < 4; i++) buf[at++] = (unsigned char)fmt[i];
    put_u32(buf + at, 16); at += 4;
    put_u16(buf + at, 1); at += 2;
    put_u16(buf + at, 1); at += 2;
    put_u32(buf + at, 8000); at += 4;
    put_u32(buf + at, 16000); at += 4;
    put_u16(buf + at, 2); at += 2;
    put_u16(buf + at, 16); at += 2;
    for (i = 0; i < 4; i++) buf[at++] = (unsigned char)data[i];
    put_u32(buf + at, data_bytes); at += 4;
    for (i = 0; i < samples; i++) {
        int sample = (i % 20u < 10u) ? 8000 : -8000;
        put_u16(buf + at, (unsigned)(sample & 0xffff));
        at += 2;
    }
    return at;
}

static void ensure_wav(void) {
    unsigned char wav[512];
    unsigned n = build_wav(wav, 200);
    write_file(wav_path(), wav, n);
    write_file(bad_wav_path(), wav, 20); /* truncated header only */
}

static size_t build_png_bytes(unsigned char *buf, size_t cap) {
    error e = error();
    unsigned char src[36];
    int x, y;
    FILE *f;
    size_t n = 0;
    for (y = 0; y < 3; y++) {
        for (x = 0; x < 4; x++) {
            int at = (y * 4 + x) * 3;
            src[at] = (unsigned char)(x * 40);
            src[at + 1] = (unsigned char)(y * 60);
            src[at + 2] = 128;
        }
    }
    image im = image_from_pixels(src, 4, 3, 3, &e);
    if (im.valid() && im.write_png(png_path(), &e) == STDLIB_OK) {
        f = fopen(png_path(), "rb");
        if (f != NULL) {
            n = fread(buf, 1, cap, f);
            fclose(f);
        }
    }
    im.drop();
    return n;
}

static void ensure_png(void) {
    unsigned char buf[2048];
    size_t n = build_png_bytes(buf, sizeof(buf));
    if (n > 24)
        write_file(bad_png_path(), buf, 24); /* truncated PNG */
    write_file(bad_wav_path(), "this is not a decodable image", 30);
}

static void ensure_media(void) {
    unsigned char buf[4096];
    size_t n = 0;
    FILE *f = fopen("build/stdlib-tests/clip.mp4", "rb");
    if (f != NULL) {
        n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
    }
    write_file(media_path(), buf, n);
    if (n > 12)
        write_file(bad_media_path(), buf, 12); /* truncated container header */
    else
        write_file(bad_media_path(), "not a media file", 16);
}

/* Common object setup used by many cases.  Each is in a forked child. */
#define GFX_DECLS                                                              \
    error e = error();                                                         \
    sdl core = sdl(VFLAGS, &e);                                                \
    window win = window("edge100g", 64, 48, WFLAGS, &e);                       \
    renderer draw = renderer(win.native(), "software", &e)

#define GFX_DROP                                                               \
    do {                                                                       \
        draw.drop();                                                           \
        win.drop();                                                            \
        core.drop();                                                           \
    } while (0)

#define GUI_DECLS                                                              \
    error e = error();                                                         \
    sdl core = sdl(VFLAGS, &e);                                                \
    window win = window("edge100ui", 240, 160, WFLAGS, &e);                    \
    renderer draw = renderer(win.native(), "software", &e);                    \
    gui ui = gui(win.native(), draw.native(), &e)

#define GUI_DROP                                                               \
    do {                                                                       \
        ui.drop();                                                             \
        draw.drop();                                                           \
        win.drop();                                                            \
        core.drop();                                                           \
    } while (0)

/* =========================================================================
 * window.hp
 * ========================================================================= */
static int c_window_sdl_ctor_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    int ok = core.valid() == 1 && e.ok();
    core.drop();
    REPORT("window", "sdl::ctor", "valid", ok);
}

static int c_window_sdl_ctor_null(void) {
    /* NULL error is legal for a constructor (error_reset tolerates NULL). */
    sdl core = sdl(0u, NULL);
    int ok = core.valid() == 1;
    core.drop();
    REPORT("window", "sdl::ctor", "null", ok);
}

static int c_window_sdl_ctor_flags(void) {
    error e = error();
    /* Zero and oversized flags: valid must equal err.ok() by construction. */
    sdl sc_a = sdl(0u, &e);
    int ok1 = sc_a.valid() == (e.ok() ? 1 : 0);
    sc_a.drop();
    sdl sc_b = sdl((unsigned)0xffffffffu, &e);
    int ok2 = sc_b.valid() == (e.ok() ? 1 : 0);
    sc_b.drop();
    REPORT("window", "sdl::ctor", "dim", ok1 && ok2);
}

static int c_window_sdl_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    int ok = core.valid() == 1;
    core.drop();
    if (core.valid() != 0) ok = 0;
    REPORT("window", "sdl::valid", "valid", ok);
}

static int c_window_sdl_flags_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    int ok = core.subsystem_flags() == VFLAGS;
    core.drop();
    REPORT("window", "sdl::subsystem_flags", "valid", ok);
}

static int c_window_sdl_flags_zero(void) {
    error e = error();
    sdl core = sdl(0u, &e);
    int ok = core.subsystem_flags() == 0u;
    core.drop();
    REPORT("window", "sdl::subsystem_flags", "dim", ok);
}

static int c_window_backend_error(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    int ok = sdl_backend_error() != NULL;
    core.drop();
    REPORT("window", "sdl_backend_error", "valid", ok);
}

static int c_window_sdl_drop2(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    core.drop();
    core.drop(); /* double-drop must be sc_a no-op */
    REPORT("window", "sdl::drop", "drop2", core.valid() == 0);
}

static int c_window_ctor_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100", 64, 48, WFLAGS, &e);
    int ok = e.ok() && w.valid() == 1 && w.native() != NULL;
    w.drop();
    core.drop();
    REPORT("window", "window::ctor", "valid", ok);
}

static int c_window_ctor_null(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window wn_a = window(NULL, 64, 48, WFLAGS, &e);
    int ok1 = wn_a.valid() == (e.ok() ? 1 : 0);
    wn_a.drop();
    window wn_b = window("edge100", 64, 48, WFLAGS, NULL);
    int ok2 = wn_b.valid() == 1;
    wn_b.drop();
    core.drop();
    REPORT("window", "window::ctor", "null", ok1 && ok2);
}

static int c_window_ctor_dim(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window wn_a = window("edge100", 0, 48, WFLAGS, &e);
    int ok1 = !wn_a.valid() && e.failed_now() == 1;
    wn_a.drop();
    window wn_b = window("edge100", -4, 48, WFLAGS, &e);
    int ok2 = !wn_b.valid() && e.failed_now() == 1;
    wn_b.drop();
    core.drop();
    REPORT("window", "window::ctor", "dim", ok1 && ok2);
}

static int c_window_ctor_dimbig(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100", INT_MAX, INT_MAX, WFLAGS, &e);
    int ok = w.valid() == (e.ok() ? 1 : 0);
    w.drop();
    core.drop();
    REPORT("window", "window::ctor", "dimbig", ok);
}

static int c_window_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window wn_a = window("edge100", 64, 48, WFLAGS, &e);
    window wn_b = window("edge100", 0, 48, WFLAGS, &e);
    int ok = wn_a.valid() == 1 && wn_b.valid() == 0;
    wn_a.drop();
    wn_b.drop();
    core.drop();
    REPORT("window", "window::valid", "valid", ok);
}

static int c_window_native(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window wn_a = window("edge100", 64, 48, WFLAGS, &e);
    window wn_b = window("edge100", 0, 48, WFLAGS, &e);
    int ok = wn_a.native() != NULL && wn_b.native() == NULL;
    wn_a.drop();
    wn_b.drop();
    core.drop();
    REPORT("window", "window::native", "valid", ok);
}

static int c_window_size_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100", 64, 48, WFLAGS, &e);
    int width = 0, height = 0;
    int ok = w.size(&width, &height, &e) == STDLIB_OK &&
             width == 64 && height == 48;
    w.drop();
    core.drop();
    REPORT("window", "window::size", "valid", ok);
}

static int c_window_size_null(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100", 64, 48, WFLAGS, &e);
    int width = 0, height = 0;
    int ok1 = w.size(NULL, &height, &e) == STDLIB_FAIL && e.failed_now() == 1;
    int ok2 = w.size(&width, NULL, &e) == STDLIB_FAIL && e.failed_now() == 1;
    window wn_bad = window("edge100", 0, 0, WFLAGS, &e);
    int ok3 = wn_bad.size(&width, &height, &e) == STDLIB_FAIL;
    w.drop();
    wn_bad.drop();
    core.drop();
    REPORT("window", "window::size", "null", ok1 && ok2 && ok3);
}

static int c_window_set_title_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100", 64, 48, WFLAGS, &e);
    int ok = w.set_title("renamed", &e) == STDLIB_OK;
    w.drop();
    core.drop();
    REPORT("window", "window::set_title", "valid", ok);
}

static int c_window_set_title_null(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100", 64, 48, WFLAGS, &e);
    int ok1 = w.set_title(NULL, &e) == STDLIB_FAIL && e.failed_now() == 1;
    window wn_bad = window("edge100", 0, 0, WFLAGS, &e);
    int ok2 = wn_bad.set_title("x", &e) == STDLIB_FAIL;
    w.drop();
    wn_bad.drop();
    core.drop();
    REPORT("window", "window::set_title", "null", ok1 && ok2);
}

static int c_window_drop(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100", 64, 48, WFLAGS, &e);
    w.drop();
    int ok = w.valid() == 0 && w.native() == NULL;
    core.drop();
    REPORT("window", "window::drop", "valid", ok);
}

static int c_window_drop2(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100", 64, 48, WFLAGS, &e);
    w.drop();
    w.drop();
    int ok = w.valid() == 0 && w.native() == NULL;
    core.drop();
    REPORT("window", "window::drop", "drop2", ok);
}

/* =========================================================================
 * graphics.hp
 * ========================================================================= */
static int c_gfx_renderer_ctor_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100r", 64, 48, WFLAGS, &e);
    renderer r = renderer(w.native(), "software", &e);
    int ok = e.ok() && r.valid() == 1 && r.native() != NULL;
    r.drop();
    w.drop();
    core.drop();
    REPORT("graphics", "renderer::ctor", "valid", ok);
}

static int c_gfx_renderer_ctor_null(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100r", 64, 48, WFLAGS, &e);
    renderer rn_a = renderer(NULL, "software", &e);
    int ok1 = rn_a.valid() == 0 && e.failed_now() == 1;
    rn_a.drop();
    renderer rn_b = renderer(w.native(), "software", NULL);
    int ok2 = rn_b.valid() == 1;
    rn_b.drop();
    w.drop();
    core.drop();
    REPORT("graphics", "renderer::ctor", "null", ok1 && ok2);
}

static int c_gfx_renderer_ctor_fmt(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100r", 64, 48, WFLAGS, &e);
    renderer r = renderer(w.native(), "no_such_renderer_driver", &e);
    int ok = r.valid() == 0 && e.failed_now() == 1;
    r.drop();
    w.drop();
    core.drop();
    REPORT("graphics", "renderer::ctor", "fmt", ok);
}

static int c_gfx_renderer_valid(void) {
    GFX_DECLS;
    int ok = draw.valid() == 1;
    draw.drop();
    if (draw.valid() != 0) ok = 0;
    GFX_DROP;
    REPORT("graphics", "renderer::valid", "valid", ok);
}

static int c_gfx_renderer_native(void) {
    GFX_DECLS;
    int ok = draw.native() != NULL;
    draw.drop();
    if (draw.native() != NULL) ok = 0;
    GFX_DROP;
    REPORT("graphics", "renderer::native", "valid", ok);
}

static int c_gfx_renderer_set_color(void) {
    GFX_DECLS;
    int ok = draw.set_color(255, 0, 0, 255, &e) == STDLIB_OK;
    if (draw.set_color(0, 0, 0, 255, NULL) != STDLIB_OK) ok = 0;
    draw.drop();
    if (draw.set_color(1, 2, 3, 4, &e) != STDLIB_FAIL) ok = 0;
    GFX_DROP;
    REPORT("graphics", "renderer::set_color", "valid", ok);
}

static int c_gfx_renderer_clear(void) {
    GFX_DECLS;
    int ok = draw.clear(&e) == STDLIB_OK;
    draw.drop();
    if (draw.clear(&e) != STDLIB_FAIL) ok = 0;
    GFX_DROP;
    REPORT("graphics", "renderer::clear", "valid", ok);
}

static int c_gfx_renderer_fill_rect(void) {
    GFX_DECLS;
    int ok = draw.fill_rect(8.0f, 8.0f, 16.0f, 16.0f, &e) == STDLIB_OK;
    if (draw.fill_rect(-100.0f, -100.0f, 1000.0f, 1000.0f, &e) != STDLIB_OK)
        ok = 0; /* out-of-range coords clip, no error */
    draw.drop();
    if (draw.fill_rect(0, 0, 1, 1, &e) != STDLIB_FAIL) ok = 0;
    GFX_DROP;
    REPORT("graphics", "renderer::fill_rect", "oob", ok);
}

static int c_gfx_renderer_line(void) {
    GFX_DECLS;
    int ok = draw.line(0.0f, 0.0f, 63.0f, 47.0f, &e) == STDLIB_OK;
    if (draw.line(-1000.0f, -1000.0f, 2000.0f, 2000.0f, &e) != STDLIB_OK)
        ok = 0;
    draw.drop();
    if (draw.line(0, 0, 1, 1, &e) != STDLIB_FAIL) ok = 0;
    GFX_DROP;
    REPORT("graphics", "renderer::line", "oob", ok);
}

static int c_gfx_renderer_set_vsync(void) {
    GFX_DECLS;
    int rc = draw.set_vsync(0, &e);
    int ok = (rc == STDLIB_OK) ? e.ok() : e.failed_now();
    rc = draw.set_vsync(-1, &e);
    if ((rc == STDLIB_OK) ? !e.ok() : !e.failed_now()) ok = 0;
    draw.drop();
    if (draw.set_vsync(0, &e) != STDLIB_FAIL) ok = 0;
    GFX_DROP;
    REPORT("graphics", "renderer::set_vsync", "valid", ok);
}

static int c_gfx_renderer_present(void) {
    GFX_DECLS;
    int ok = draw.present(&e) == STDLIB_OK;
    draw.drop();
    if (draw.present(&e) != STDLIB_FAIL) ok = 0;
    GFX_DROP;
    REPORT("graphics", "renderer::present", "valid", ok);
}

static int c_gfx_renderer_read(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    int ok = e.ok() && shot.valid() == 1 && shot.width() == 64 &&
             shot.height() == 48;
    shot.drop();
    draw.drop();
    surface sf_none = draw.read(&e);
    if (sf_none.valid() != 0 || e.failed_now() != 1) ok = 0;
    sf_none.drop();
    GFX_DROP;
    REPORT("graphics", "renderer::read", "valid", ok);
}

static int c_gfx_renderer_drop(void) {
    GFX_DECLS;
    draw.drop();
    int ok = draw.valid() == 0 && draw.native() == NULL;
    GFX_DROP;
    REPORT("graphics", "renderer::drop", "valid", ok);
}

static int c_gfx_renderer_drop2(void) {
    GFX_DECLS;
    draw.drop();
    draw.drop();
    int ok = draw.valid() == 0 && draw.native() == NULL;
    GFX_DROP;
    REPORT("graphics", "renderer::drop", "drop2", ok);
}

static int c_gfx_surface_ctor(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    int ok = shot.valid() == 1;
    surface sf_empty = surface(NULL);
    if (sf_empty.valid() != 0 || sf_empty.width() != 0) ok = 0;
    sf_empty.drop();
    shot.drop();
    GFX_DROP;
    REPORT("graphics", "surface::ctor", "valid", ok);
}

static int c_gfx_surface_valid(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    int ok = shot.valid() == 1;
    shot.drop();
    if (shot.valid() != 0) ok = 0;
    GFX_DROP;
    REPORT("graphics", "surface::valid", "valid", ok);
}

static int c_gfx_surface_width(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    surface sf_empty = surface(NULL);
    int ok = shot.width() == 64 && sf_empty.width() == 0;
    sf_empty.drop();
    shot.drop();
    GFX_DROP;
    REPORT("graphics", "surface::width", "valid", ok);
}

static int c_gfx_surface_height(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    surface sf_empty = surface(NULL);
    int ok = shot.height() == 48 && sf_empty.height() == 0;
    sf_empty.drop();
    shot.drop();
    GFX_DROP;
    REPORT("graphics", "surface::height", "valid", ok);
}

static int c_gfx_surface_pixel(void) {
    GFX_DECLS;
    draw.set_color(255, 0, 0, 255, &e);
    draw.clear(&e);
    draw.fill_rect(8.0f, 8.0f, 16.0f, 16.0f, &e);
    surface shot = draw.read(&e);
    unsigned char px[4] = {0, 0, 0, 0};
    int ok = shot.pixel(12, 12, px, &e) == STDLIB_OK &&
             px[2] > 200 && px[0] < 50 && px[1] < 50;
    shot.drop();
    GFX_DROP;
    REPORT("graphics", "surface::pixel", "valid", ok);
}

static int c_gfx_surface_pixel_oob(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    unsigned char px[4] = {0, 0, 0, 0};
    int ok = 1;
    if (shot.pixel(-1, 0, px, &e) != STDLIB_FAIL || e.failed_now() != 1) ok = 0;
    if (shot.pixel(0, -1, px, &e) != STDLIB_FAIL) ok = 0;
    if (shot.pixel(64, 0, px, &e) != STDLIB_FAIL) ok = 0;
    if (shot.pixel(0, 48, px, &e) != STDLIB_FAIL) ok = 0;
    if (shot.pixel(INT_MAX, INT_MAX, px, &e) != STDLIB_FAIL) ok = 0;
    shot.drop();
    GFX_DROP;
    REPORT("graphics", "surface::pixel", "oob", ok);
}

static int c_gfx_surface_pixel_null(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    int ok = shot.pixel(1, 1, NULL, &e) == STDLIB_FAIL && e.failed_now() == 1;
    shot.drop();
    GFX_DROP;
    REPORT("graphics", "surface::pixel", "null", ok);
}

static int c_gfx_surface_drop(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    shot.drop();
    int ok = shot.valid() == 0 && shot.width() == 0;
    GFX_DROP;
    REPORT("graphics", "surface::drop", "valid", ok);
}

static int c_gfx_surface_drop2(void) {
    GFX_DECLS;
    surface shot = draw.read(&e);
    shot.drop();
    shot.drop();
    int ok = shot.valid() == 0;
    GFX_DROP;
    REPORT("graphics", "surface::drop", "drop2", ok);
}

/* =========================================================================
 * input.hp
 * ========================================================================= */
static int c_input_poll_valid(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    SDL_Event pushed, got;
    memset(&pushed, 0, sizeof(pushed));
    memset(&got, 0, sizeof(got));
    pushed.type = SDL_EVENT_QUIT;
    (void)SDL_PushEvent(&pushed);
    int got_rc = poll_event(&got);
    int ok = got_rc == 1 && got.type == SDL_EVENT_QUIT;
    core.drop();
    REPORT("input", "poll_event", "valid", ok);
}

static int c_input_poll_null(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    SDL_Event leftover;
    while (poll_event(&leftover) != 0) { /* drain */ }
    int ok = poll_event(NULL) == 0; /* NULL only pumps */
    core.drop();
    REPORT("input", "poll_event", "null", ok);
}

static int c_input_is_quit(void) {
    SDL_Event ev, other;
    memset(&ev, 0, sizeof(ev));
    memset(&other, 0, sizeof(other));
    ev.type = SDL_EVENT_QUIT;
    other.type = SDL_EVENT_KEY_DOWN;
    int ok = event_is_quit(&ev) == 1 && event_is_quit(&other) == 0 &&
             event_is_quit(NULL) == 0;
    REPORT("input", "event_is_quit", "null", ok);
}

static int c_input_is_key_down(void) {
    SDL_Event ev, other;
    memset(&ev, 0, sizeof(ev));
    memset(&other, 0, sizeof(other));
    ev.type = SDL_EVENT_KEY_DOWN;
    other.type = SDL_EVENT_KEY_UP;
    int ok = event_is_key_down(&ev) == 1 && event_is_key_down(&other) == 0 &&
             event_is_key_down(NULL) == 0;
    REPORT("input", "event_is_key_down", "null", ok);
}

static int c_input_is_key_up(void) {
    SDL_Event ev, other;
    memset(&ev, 0, sizeof(ev));
    memset(&other, 0, sizeof(other));
    ev.type = SDL_EVENT_KEY_UP;
    other.type = SDL_EVENT_KEY_DOWN;
    int ok = event_is_key_up(&ev) == 1 && event_is_key_up(&other) == 0 &&
             event_is_key_up(NULL) == 0;
    REPORT("input", "event_is_key_up", "null", ok);
}

static int c_input_scancode(void) {
    SDL_Event ev, other;
    memset(&ev, 0, sizeof(ev));
    memset(&other, 0, sizeof(other));
    ev.type = SDL_EVENT_KEY_DOWN;
    ev.key.scancode = SDL_SCANCODE_A;
    other.type = SDL_EVENT_QUIT;
    int ok = event_scancode(&ev) == SDL_SCANCODE_A &&
             event_scancode(&other) == SDL_SCANCODE_UNKNOWN &&
             event_scancode(NULL) == SDL_SCANCODE_UNKNOWN;
    REPORT("input", "event_scancode", "null", ok);
}

static int c_input_text(void) {
    SDL_Event ev, other;
    memset(&ev, 0, sizeof(ev));
    memset(&other, 0, sizeof(other));
    ev.type = SDL_EVENT_TEXT_INPUT;
    ev.text.text = "Z";
    other.type = SDL_EVENT_TEXT_INPUT;
    other.text.text = NULL;
    int ok = event_text(&ev) == 'Z' && event_text(&other) == '\0' &&
             event_text(NULL) == '\0';
    REPORT("input", "event_text", "null", ok);
}

static int c_input_keyboard_state(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    int ok = keyboard_state() != NULL;
    core.drop();
    REPORT("input", "keyboard_state", "valid", ok);
}

static int c_input_key_pressed(void) {
    _Bool keys[SDL_SCANCODE_COUNT];
    int i;
    for (i = 0; i < (int)SDL_SCANCODE_COUNT; i++) keys[i] = false;
    keys[SDL_SCANCODE_A] = true;
    int ok = key_pressed(keys, SDL_SCANCODE_A) == 1 &&
             key_pressed(keys, SDL_SCANCODE_B) == 0 &&
             key_pressed(NULL, SDL_SCANCODE_A) == 0 &&
             key_pressed(keys, (SDL_Scancode)-1) == 0 &&
             key_pressed(keys, SDL_SCANCODE_COUNT) == 0 &&
             key_pressed(keys, (SDL_Scancode)INT_MAX) == 0;
    REPORT("input", "key_pressed", "oob", ok);
}

static int c_input_mouse_state(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    float x = -1.0f, y = -1.0f;
    unsigned mask = mouse_state(&x, &y);
    int ok = x >= 0.0f && y >= 0.0f && (mask & ~0x1fu) == 0u;
    (void)mouse_state(NULL, NULL); /* NULL out-pointers must not crash */
    core.drop();
    REPORT("input", "mouse_state", "valid", ok);
}

static int c_input_mouse_button_left(void) {
    int ok = mouse_button_left((unsigned)SDL_BUTTON_LMASK) == 1 &&
             mouse_button_left(0u) == 0 &&
             mouse_button_left(0xffffffffu) == 1;
    REPORT("input", "mouse_button_left", "valid", ok);
}

/* =========================================================================
 * image.hp
 * ========================================================================= */
static int c_image_from_pixels_valid(void) {
    error e = error();
    unsigned char src[36];
    int x, y;
    for (y = 0; y < 3; y++)
        for (x = 0; x < 4; x++) {
            int at = (y * 4 + x) * 3;
            src[at] = (unsigned char)(x * 40);
            src[at + 1] = (unsigned char)(y * 60);
            src[at + 2] = 128;
        }
    image im = image_from_pixels(src, 4, 3, 3, &e);
    int ok = e.ok() && im.valid() == 1 && im.width() == 4 && im.height() == 3 &&
             im.channels() == 3 && im.size_bytes() == 36 &&
             im.pixel(3, 2) != NULL;
    im.drop();
    REPORT("image", "image_from_pixels", "valid", ok);
}

static int c_image_from_pixels_null(void) {
    error e = error();
    unsigned char src[4] = {1, 2, 3, 4};
    image im_a = image_from_pixels(NULL, 4, 3, 3, &e);
    int ok1 = !im_a.valid() && e.failed_now() == 1;
    im_a.drop();
    image im_b = image_from_pixels(src, 4, 3, 3, NULL);
    int ok2 = im_b.valid() == 1;
    im_b.drop();
    REPORT("image", "image_from_pixels", "null", ok1 && ok2);
}

static int c_image_from_pixels_dim(void) {
    error e = error();
    unsigned char src[16] = {0};
    image im_a = image_from_pixels(src, 0, 3, 3, &e);
    int ok1 = !im_a.valid() && e.failed_now() == 1;
    im_a.drop();
    image im_b = image_from_pixels(src, 4, 0, 3, &e);
    int ok2 = !im_b.valid() && e.failed_now() == 1;
    im_b.drop();
    image im_c = image_from_pixels(src, 4, 3, 0, &e);
    int ok3 = !im_c.valid() && e.failed_now() == 1;
    im_c.drop();
    REPORT("image", "image_from_pixels", "dim", ok1 && ok2 && ok3);
}

static int c_image_from_pixels_dimbig(void) {
    error e = error();
    unsigned char src[4] = {1, 2, 3, 4};
    /* INT_MAX x INT_MAX x 4 overflows size_t: the checked guard in
       image_from_pixels must reject it before any allocation/memcpy. */
    image im = image_from_pixels(src, INT_MAX, INT_MAX, 4, &e);
    int ok = !im.valid() && e.failed_now() == 1;
    im.drop();
    REPORT("image", "image_from_pixels", "dimbig", ok);
}

static int c_image_ctor_valid(void) {
    error e = error();
    ensure_png();
    image im = image(png_path(), 0, &e);
    int ok = e.ok() && im.valid() == 1 && im.width() == 4 && im.height() == 3;
    im.drop();
    REPORT("image", "image::ctor", "valid", ok);
}

static int c_image_ctor_null(void) {
    error e = error();
    image im = image(NULL, 0, &e);
    int ok = !im.valid() && e.ok(); /* NULL path is documented as silent */
    im.drop();
    REPORT("image", "image::ctor", "null", ok);
}

static int c_image_ctor_fmt(void) {
    error e = error();
    ensure_png();
    image im = image(bad_wav_path(), 0, &e);
    int ok = !im.valid() && e.failed_now() == 1 && im.width() == 0;
    im.drop();
    REPORT("image", "image::ctor", "fmt", ok);
}

static int c_image_ctor_trunc(void) {
    error e = error();
    ensure_png();
    image im = image(bad_png_path(), 0, &e);
    int ok = !im.valid() && e.failed_now() == 1;
    im.drop();
    REPORT("image", "image::ctor", "trunc", ok);
}

static int c_image_ctor_channels(void) {
    error e = error();
    ensure_png();
    image im_a = image(png_path(), -1, &e);
    int ok1 = !im_a.valid() && e.failed_now() == 1;
    im_a.drop();
    image im_b = image(png_path(), 5, &e);
    int ok2 = !im_b.valid() && e.failed_now() == 1;
    im_b.drop();
    REPORT("image", "image::ctor", "dim", ok1 && ok2);
}

static int c_image_valid(void) {
    error e = error();
    ensure_png();
    image im_a = image(png_path(), 0, &e);
    image im_b = image(NULL, 0, &e);
    int ok = im_a.valid() == 1 && im_b.valid() == 0;
    im_a.drop();
    if (im_a.valid() != 0) ok = 0;
    im_b.drop();
    REPORT("image", "image::valid", "valid", ok);
}

static int c_image_width(void) {
    error e = error();
    ensure_png();
    image im_a = image(png_path(), 0, &e);
    image im_b = image(NULL, 0, &e);
    int ok = im_a.width() == 4 && im_b.width() == 0;
    im_a.drop();
    im_b.drop();
    REPORT("image", "image::width", "valid", ok);
}

static int c_image_height(void) {
    error e = error();
    ensure_png();
    image im_a = image(png_path(), 0, &e);
    image im_b = image(NULL, 0, &e);
    int ok = im_a.height() == 3 && im_b.height() == 0;
    im_a.drop();
    im_b.drop();
    REPORT("image", "image::height", "valid", ok);
}

static int c_image_channels(void) {
    error e = error();
    ensure_png();
    image im_a = image(png_path(), 1, &e);
    image im_b = image(NULL, 0, &e);
    int ok = im_a.channels() == 1 && im_b.channels() == 0;
    im_a.drop();
    im_b.drop();
    REPORT("image", "image::channels", "valid", ok);
}

static int c_image_size_bytes(void) {
    error e = error();
    ensure_png();
    image im_a = image(png_path(), 3, &e);
    image im_b = image(NULL, 0, &e);
    int ok = im_a.size_bytes() == 36 && im_b.size_bytes() == 0;
    im_a.drop();
    im_b.drop();
    REPORT("image", "image::size_bytes", "valid", ok);
}

static int c_image_pixel(void) {
    error e = error();
    ensure_png();
    image im = image(png_path(), 3, &e);
    const unsigned char *px = im.pixel(3, 2);
    int ok = px != NULL && px[0] == 120 && px[1] == 120 && px[2] == 128;
    im.drop();
    REPORT("image", "image::pixel", "valid", ok);
}

static int c_image_pixel_oob(void) {
    error e = error();
    ensure_png();
    image im_a = image(png_path(), 3, &e);
    image im_empty = image(NULL, 0, &e);
    int ok = im_a.pixel(-1, 0) == NULL && im_a.pixel(0, -1) == NULL &&
             im_a.pixel(4, 0) == NULL && im_a.pixel(0, 3) == NULL &&
             im_a.pixel(INT_MAX, INT_MAX) == NULL && im_empty.pixel(0, 0) == NULL;
    im_a.drop();
    im_empty.drop();
    REPORT("image", "image::pixel", "oob", ok);
}

static int c_image_write_png(void) {
    error e = error();
    unsigned char src[12] = {0};
    image im = image_from_pixels(src, 2, 2, 3, &e);
    int ok = im.write_png(png_path(), &e) == STDLIB_OK;
    if (im.write_png(NULL, &e) != STDLIB_FAIL || e.failed_now() != 1) ok = 0;
    im.drop();
    if (im.write_png(png_path(), &e) != STDLIB_FAIL) ok = 0; /* im_empty image */
    REPORT("image", "image::write_png", "null", ok);
}

static int c_image_write_bmp(void) {
    error e = error();
    unsigned char src[12] = {0};
    image im = image_from_pixels(src, 2, 2, 3, &e);
    int ok = im.write_bmp(png_path(), &e) == STDLIB_OK;
    if (im.write_bmp(NULL, &e) != STDLIB_FAIL || e.failed_now() != 1) ok = 0;
    im.drop();
    if (im.write_bmp(png_path(), &e) != STDLIB_FAIL) ok = 0;
    REPORT("image", "image::write_bmp", "null", ok);
}

static int c_image_drop(void) {
    error e = error();
    ensure_png();
    image im = image(png_path(), 0, &e);
    im.drop();
    int ok = im.valid() == 0 && im.width() == 0 && im.size_bytes() == 0;
    REPORT("image", "image::drop", "valid", ok);
}

static int c_image_drop2(void) {
    error e = error();
    ensure_png();
    image im = image(png_path(), 0, &e);
    im.drop();
    im.drop();
    int ok = im.valid() == 0;
    REPORT("image", "image::drop", "drop2", ok);
}

static int c_image_from_memory_valid(void) {
    error e = error();
    unsigned char buf[2048];
    size_t n = build_png_bytes(buf, sizeof(buf));
    image im = image_from_memory(buf, (int)n, 3, &e);
    int ok = e.ok() && im.valid() == 1 && im.width() == 4 && im.channels() == 3;
    im.drop();
    REPORT("image", "image_from_memory", "valid", ok);
}

static int c_image_from_memory_null(void) {
    error e = error();
    unsigned char buf[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    image im_a = image_from_memory(NULL, 8, 0, &e);
    int ok1 = !im_a.valid() && e.failed_now() == 1;
    im_a.drop();
    image im_b = image_from_memory(buf, 0, 0, &e);
    int ok2 = !im_b.valid() && e.failed_now() == 1;
    im_b.drop();
    REPORT("image", "image_from_memory", "null", ok1 && ok2);
}

static int c_image_from_memory_trunc(void) {
    error e = error();
    unsigned char buf[2048];
    size_t n = build_png_bytes(buf, sizeof(buf));
    image im = image_from_memory(buf, (int)(n > 12 ? 12 : n), 0, &e);
    int ok = !im.valid() && e.failed_now() == 1;
    im.drop();
    REPORT("image", "image_from_memory", "trunc", ok);
}

static int c_image_from_memory_fmt(void) {
    error e = error();
    unsigned char buf[2048];
    size_t n = build_png_bytes(buf, sizeof(buf));
    image im_a = image_from_memory(buf, (int)n, -1, &e);
    int ok1 = !im_a.valid() && e.failed_now() == 1;
    im_a.drop();
    image im_b = image_from_memory(buf, (int)n, 5, &e);
    int ok2 = !im_b.valid() && e.failed_now() == 1;
    im_b.drop();
    REPORT("image", "image_from_memory", "fmt", ok1 && ok2);
}

/* =========================================================================
 * audio.hp
 * ========================================================================= */
static void audio_cb(ma_device *device, void *output, const void *input,
                     ma_uint32 frame_count) {
    (void)device; (void)output; (void)input; (void)frame_count;
}

static int c_audio_sleep(void) {
    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    audio_sleep_ms(2);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    int ok = t1.tv_sec > t0.tv_sec ||
             (t1.tv_sec == t0.tv_sec && t1.tv_nsec >= t0.tv_nsec);
    t0 = t1;
    audio_sleep_ms(0); /* zero duration */
    clock_gettime(CLOCK_MONOTONIC, &t1);
    if (t1.tv_sec < t0.tv_sec ||
        (t1.tv_sec == t0.tv_sec && t1.tv_nsec < t0.tv_nsec)) ok = 0;
    REPORT("audio", "audio_sleep_ms", "valid", ok);
}

static int c_audio_decoder_from_memory_valid(void) {
    error e = error();
    unsigned char wav[512];
    unsigned n = build_wav(wav, 200);
    audio_decoder ax_d = decoder_from_memory(wav, n, &e);
    int ok = e.ok() && ax_d.valid() == 1 && ax_d.channels() == 1 &&
             ax_d.sample_rate() == 8000 && ax_d.frame_count() == 200;
    ax_d.drop();
    REPORT("audio", "decoder_from_memory", "valid", ok);
}

static int c_audio_decoder_from_memory_null(void) {
    error e = error();
    unsigned char wav[32] = {0};
    audio_decoder ax_a = decoder_from_memory(NULL, 32, &e);
    int ok1 = !ax_a.valid() && e.failed_now() == 1;
    ax_a.drop();
    audio_decoder ax_b = decoder_from_memory(wav, 0, &e);
    int ok2 = !ax_b.valid() && e.failed_now() == 1;
    ax_b.drop();
    REPORT("audio", "decoder_from_memory", "null", ok1 && ok2);
}

static int c_audio_decoder_from_memory_trunc(void) {
    error e = error();
    unsigned char wav[512];
    unsigned n = build_wav(wav, 200);
    audio_decoder ax_d = decoder_from_memory(wav, n > 20 ? 20 : n, &e);
    int ok = !ax_d.valid() && e.failed_now() == 1;
    ax_d.drop();
    REPORT("audio", "decoder_from_memory", "trunc", ok);
}

static int c_audio_decoder_ctor(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_d = audio_decoder(wav_path(), &e);
    int ok = e.ok() && ax_d.valid() == 1 && ax_d.channels() == 1 &&
             ax_d.sample_rate() == 8000 && ax_d.frame_count() == 200;
    ax_d.drop();
    REPORT("audio", "audio_decoder::ctor", "valid", ok);
}

static int c_audio_decoder_ctor_null(void) {
    error e = error();
    audio_decoder ax_d = audio_decoder(NULL, &e);
    int ok = !ax_d.valid() && e.ok(); /* NULL path: no error recorded */
    ax_d.drop();
    REPORT("audio", "audio_decoder::ctor", "null", ok);
}

static int c_audio_decoder_ctor_trunc(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_d = audio_decoder(bad_wav_path(), &e);
    int ok = !ax_d.valid() && e.failed_now() == 1;
    ax_d.drop();
    REPORT("audio", "audio_decoder::ctor", "trunc", ok);
}

static int c_audio_decoder_ctor_fmt(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_d = audio_decoder(bad_media_path(), &e);
    int ok = !ax_d.valid() && e.failed_now() == 1;
    ax_d.drop();
    REPORT("audio", "audio_decoder::ctor", "fmt", ok);
}

static int c_audio_decoder_valid(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_d = audio_decoder(wav_path(), &e);
    int ok = ax_d.valid() == 1;
    ax_d.drop();
    if (ax_d.valid() != 0) ok = 0;
    REPORT("audio", "audio_decoder::valid", "valid", ok);
}

static int c_audio_decoder_channels(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_a = audio_decoder(wav_path(), &e);
    audio_decoder ax_b = audio_decoder(NULL, &e);
    int ok = ax_a.channels() == 1 && ax_b.channels() == 0;
    ax_a.drop();
    ax_b.drop();
    REPORT("audio", "audio_decoder::channels", "valid", ok);
}

static int c_audio_decoder_sample_rate(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_a = audio_decoder(wav_path(), &e);
    audio_decoder ax_b = audio_decoder(NULL, &e);
    int ok = ax_a.sample_rate() == 8000 && ax_b.sample_rate() == 0;
    ax_a.drop();
    ax_b.drop();
    REPORT("audio", "audio_decoder::sample_rate", "valid", ok);
}

static int c_audio_decoder_frame_count(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_a = audio_decoder(wav_path(), &e);
    audio_decoder ax_b = audio_decoder(NULL, &e);
    int ok = ax_a.frame_count() == 200 && ax_b.frame_count() == 0;
    ax_a.drop();
    ax_b.drop();
    REPORT("audio", "audio_decoder::frame_count", "valid", ok);
}

static int c_audio_decoder_read(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_d = audio_decoder(wav_path(), &e);
    float frames[64];
    unsigned long long got = ax_d.read(frames, 32, &e);
    int ok = got == 32 && frames[0] > 0.0f;
    if (ax_d.read(NULL, 32, &e) != 0 || e.failed_now() != 1) ok = 0;
    if (ax_d.read(frames, 0, &e) != 0) ok = 0; /* zero frames: no-op */
    ax_d.drop();
    if (ax_d.read(frames, 32, &e) != 0) ok = 0;
    REPORT("audio", "audio_decoder::read", "null", ok);
}

static int c_audio_decoder_drop(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_d = audio_decoder(wav_path(), &e);
    ax_d.drop();
    REPORT("audio", "audio_decoder::drop", "valid", ax_d.valid() == 0);
}

static int c_audio_decoder_drop2(void) {
    error e = error();
    ensure_wav();
    audio_decoder ax_d = audio_decoder(wav_path(), &e);
    ax_d.drop();
    ax_d.drop();
    REPORT("audio", "audio_decoder::drop", "drop2", ax_d.valid() == 0);
}

static int c_audio_context_ctor(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    int ok = e.ok() && ctx.valid() == 1 && ctx.native() != NULL;
    ctx.drop();
    audio_context real = audio_context(0, &e);
    if (real.valid() != (e.ok() ? 1 : 0)) ok = 0;
    real.drop();
    REPORT("audio", "audio_context::ctor", "valid", ok);
}

static int c_audio_context_ctor_null(void) {
    audio_context ctx = audio_context(1, NULL);
    int ok = ctx.valid() == 1;
    ctx.drop();
    REPORT("audio", "audio_context::ctor", "null", ok);
}

static int c_audio_context_valid(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    int ok = ctx.valid() == 1;
    ctx.drop();
    if (ctx.valid() != 0) ok = 0;
    REPORT("audio", "audio_context::valid", "valid", ok);
}

static int c_audio_context_native(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    int ok = ctx.native() != NULL;
    ctx.drop();
    REPORT("audio", "audio_context::native", "valid", ok);
}

static int c_audio_context_drop(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    ctx.drop();
    REPORT("audio", "audio_context::drop", "valid", ctx.valid() == 0);
}

static int c_audio_context_drop2(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    ctx.drop();
    ctx.drop();
    REPORT("audio", "audio_context::drop", "drop2", ctx.valid() == 0);
}

static int c_audio_engine_ctor(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    int ok = e.ok() && eng.valid() == 1 && eng.native() != NULL;
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_engine::ctor", "valid", ok);
}

static int c_audio_engine_ctor_null(void) {
    error e = error();
    audio_engine eng = audio_engine(NULL, &e);
    int ok = eng.valid() == (e.ok() ? 1 : 0);
    eng.drop();
    audio_context ctx = audio_context(1, NULL);
    audio_engine eng2 = audio_engine(ctx.native(), NULL);
    if (eng2.valid() != 1) ok = 0;
    eng2.drop();
    ctx.drop();
    REPORT("audio", "audio_engine::ctor", "null", ok);
}

static int c_audio_engine_valid(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    int ok = eng.valid() == 1;
    eng.drop();
    if (eng.valid() != 0) ok = 0;
    ctx.drop();
    REPORT("audio", "audio_engine::valid", "valid", ok);
}

static int c_audio_engine_native(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    int ok = eng.native() != NULL;
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_engine::native", "valid", ok);
}

static int c_audio_engine_set_volume(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    int ok = eng.set_volume(0.5f, &e) == STDLIB_OK && e.ok();
    if (eng.set_volume(-2.0f, NULL) != STDLIB_OK) ok = 0; /* clamps */
    if (eng.set_volume(100.0f, &e) != STDLIB_OK) ok = 0;  /* clamps */
    eng.drop();
    if (eng.set_volume(0.5f, &e) != STDLIB_FAIL) ok = 0;
    ctx.drop();
    REPORT("audio", "audio_engine::set_volume", "oob", ok);
}

static int c_audio_engine_play(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    int ok = eng.play(wav_path(), &e) == STDLIB_OK;
    if (eng.play(NULL, &e) != STDLIB_FAIL || e.failed_now() != 1) ok = 0;
    if (eng.play(bad_wav_path(), &e) != STDLIB_FAIL) ok = 0; /* truncated */
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_engine::play", "null", ok);
}

static int c_audio_engine_load(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    audio_sound as_a = eng.load(wav_path(), &e);
    int ok = e.ok() && as_a.valid() == 1;
    as_a.drop();
    audio_sound as_b = eng.load(NULL, &e);
    if (as_b.valid() != 0 || e.failed_now() != 1) ok = 0;
    as_b.drop();
    audio_sound as_c = eng.load(bad_wav_path(), &e);
    if (as_c.valid() != 0) ok = 0;
    as_c.drop();
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_engine::load", "null", ok);
}

static int c_audio_engine_drop(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    eng.drop();
    int ok = eng.valid() == 0;
    ctx.drop();
    REPORT("audio", "audio_engine::drop", "valid", ok);
}

static int c_audio_engine_drop2(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    eng.drop();
    eng.drop();
    int ok = eng.valid() == 0;
    ctx.drop();
    REPORT("audio", "audio_engine::drop", "drop2", ok);
}

static int c_audio_sound_ctor(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    audio_sound as_a = audio_sound(eng.native(), wav_path(), &e);
    int ok = e.ok() && as_a.valid() == 1;
    as_a.drop();
    audio_sound as_b = audio_sound(NULL, wav_path(), &e);
    if (as_b.valid() != 0 || e.failed_now() != 1) ok = 0;
    as_b.drop();
    audio_sound as_c = audio_sound(eng.native(), NULL, &e);
    if (as_c.valid() != 0 || e.failed_now() != 1) ok = 0;
    as_c.drop();
    audio_sound as_d = audio_sound(eng.native(), bad_wav_path(), &e);
    if (as_d.valid() != 0) ok = 0;
    as_d.drop();
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_sound::ctor", "null", ok);
}

static int c_audio_sound_valid(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    audio_sound s = eng.load(wav_path(), &e);
    int ok = s.valid() == 1;
    s.drop();
    if (s.valid() != 0) ok = 0;
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_sound::valid", "valid", ok);
}

static int c_audio_sound_play(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    audio_sound s = eng.load(wav_path(), &e);
    int ok = s.play(&e) == STDLIB_OK;
    if (s.play(NULL) != STDLIB_OK) ok = 0;
    s.drop();
    if (s.play(&e) != STDLIB_FAIL || e.failed_now() != 1) ok = 0;
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_sound::play", "null", ok);
}

static int c_audio_sound_stop(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    audio_sound s = eng.load(wav_path(), &e);
    int ok = s.stop(&e) == STDLIB_OK;
    s.drop();
    if (s.stop(&e) != STDLIB_FAIL) ok = 0;
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_sound::stop", "valid", ok);
}

static int c_audio_sound_playing(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    audio_sound s = eng.load(wav_path(), &e);
    int ok = s.playing() == 0;
    s.play(&e);
    if (s.playing() != 1) ok = 0;
    s.drop();
    if (s.playing() != 0) ok = 0;
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_sound::playing", "valid", ok);
}

static int c_audio_sound_drop(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    audio_sound s = eng.load(wav_path(), &e);
    s.drop();
    int ok = s.valid() == 0;
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_sound::drop", "valid", ok);
}

static int c_audio_sound_drop2(void) {
    error e = error();
    ensure_wav();
    audio_context ctx = audio_context(1, &e);
    audio_engine eng = audio_engine(ctx.native(), &e);
    audio_sound s = eng.load(wav_path(), &e);
    s.drop();
    s.drop();
    int ok = s.valid() == 0;
    eng.drop();
    ctx.drop();
    REPORT("audio", "audio_sound::drop", "drop2", ok);
}

static int c_audio_device_ctor(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, audio_cb, NULL, &e);
    int ok = e.ok() && dev.valid() == 1;
    dev.drop();
    ctx.drop();
    REPORT("audio", "audio_device::ctor", "valid", ok);
}

static int c_audio_device_ctor_null(void) {
    error e = error();
    audio_device dev = audio_device(NULL, 0, 48000, 2, audio_cb, NULL, &e);
    int ok = !dev.valid() && e.failed_now() == 1;
    dev.drop();
    REPORT("audio", "audio_device::ctor", "null", ok);
}

static int c_audio_device_ctor_dim(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_device z = audio_device(ctx.native(), 0, 0, 2, audio_cb, NULL, &e);
    int ok1 = z.valid() == (e.ok() ? 1 : 0); /* 0 = default rate */
    z.drop();
    audio_device big = audio_device(ctx.native(), 0, 48000, 1024, audio_cb, NULL, &e);
    int ok2 = big.valid() == (e.ok() ? 1 : 0);
    big.drop();
    ctx.drop();
    REPORT("audio", "audio_device::ctor", "dim", ok1 && ok2);
}

static int c_audio_device_valid(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, audio_cb, NULL, &e);
    int ok = dev.valid() == 1;
    dev.drop();
    if (dev.valid() != 0) ok = 0;
    ctx.drop();
    REPORT("audio", "audio_device::valid", "valid", ok);
}

static int c_audio_device_start(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, audio_cb, NULL, &e);
    int ok = dev.start(&e) == STDLIB_OK;
    dev.stop(&e);
    dev.drop();
    if (dev.start(&e) != STDLIB_FAIL) ok = 0;
    ctx.drop();
    REPORT("audio", "audio_device::start", "valid", ok);
}

static int c_audio_device_running(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, audio_cb, NULL, &e);
    int ok = dev.running() == 0;
    dev.start(&e);
    if (dev.running() != 1) ok = 0;
    dev.stop(&e);
    if (dev.running() != 0) ok = 0;
    dev.drop();
    if (dev.running() != 0) ok = 0;
    ctx.drop();
    REPORT("audio", "audio_device::running", "valid", ok);
}

static int c_audio_device_stop(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, audio_cb, NULL, &e);
    dev.start(&e);
    int ok = dev.stop(&e) == STDLIB_OK;
    dev.drop();
    if (dev.stop(&e) != STDLIB_FAIL) ok = 0;
    ctx.drop();
    REPORT("audio", "audio_device::stop", "valid", ok);
}

static int c_audio_device_drop(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, audio_cb, NULL, &e);
    dev.drop();
    int ok = dev.valid() == 0;
    ctx.drop();
    REPORT("audio", "audio_device::drop", "valid", ok);
}

static int c_audio_device_drop2(void) {
    error e = error();
    audio_context ctx = audio_context(1, &e);
    audio_device dev = audio_device(ctx.native(), 0, 48000, 2, audio_cb, NULL, &e);
    dev.drop();
    dev.drop();
    int ok = dev.valid() == 0;
    ctx.drop();
    REPORT("audio", "audio_device::drop", "drop2", ok);
}

/* =========================================================================
 * gui.hp
 * ========================================================================= */
static int c_gui_ctor(void) {
    error e = error();
    sdl core = sdl(VFLAGS, &e);
    window w = window("edge100ui", 240, 160, WFLAGS, &e);
    renderer r = renderer(w.native(), "software", &e);
    gui gu_a = gui(w.native(), r.native(), &e);
    int ok = e.ok() && gu_a.valid() == 1 && gu_a.native() != NULL;
    gu_a.drop();
    gui gu_b = gui(NULL, r.native(), &e);
    int ok2 = !gu_b.valid() && e.failed_now() == 1;
    gu_b.drop();
    gui gu_c = gui(w.native(), NULL, &e);
    int ok3 = !gu_c.valid() && e.failed_now() == 1;
    gu_c.drop();
    if (!(ok && ok2 && ok3)) ok = 0;
    r.drop();
    w.drop();
    core.drop();
    REPORT("gui", "gui::ctor", "null", ok);
}

static int c_gui_valid(void) {
    GUI_DECLS;
    int ok = ui.valid() == 1;
    GUI_DROP;
    REPORT("gui", "gui::valid", "valid", ok);
}

static int c_gui_native(void) {
    GUI_DECLS;
    int ok = ui.native() != NULL;
    GUI_DROP;
    REPORT("gui", "gui::native", "valid", ok);
}

static int c_gui_handle_event(void) {
    GUI_DECLS;
    SDL_Event ev;
    memset(&ev, 0, sizeof(ev));
    int ok = ui.handle_event(NULL) == 0;
    (void)ui.handle_event(&ev); /* 0 or 1: consumed flag */
    GUI_DROP;
    REPORT("gui", "gui::handle_event", "null", ok);
}

static int c_gui_input_begin(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    int probe = ui.window_begin("probe", 4.0f, 4.0f, 100.0f, 40.0f, 0);
    ui.window_end();
    int ok = probe == 1;
    GUI_DROP;
    REPORT("gui", "gui::input_begin", "valid", ok);
}

static int c_gui_input_motion(void) {
    GUI_DECLS;
    struct nk_context *ctx = ui.native();
    ui.input_motion(60, 40);
    int ok = ctx->input.mouse.pos.x == 60.0f && ctx->input.mouse.pos.y == 40.0f;
    ui.input_motion(INT_MAX, INT_MIN); /* out-of-range coords: no crash */
    GUI_DROP;
    REPORT("gui", "gui::input_motion", "oob", ok);
}

static int c_gui_input_button(void) {
    GUI_DECLS;
    struct nk_context *ctx = ui.native();
    ui.input_motion(60, 40);
    ui.input_begin();
    ui.input_button(1);
    ui.input_end();
    int ok = ctx->input.mouse.buttons[NK_BUTTON_LEFT].down == 1;
    GUI_DROP;
    REPORT("gui", "gui::input_button", "valid", ok);
}

static int c_gui_input_end(void) {
    GUI_DECLS;
    struct nk_context *ctx = ui.native();
    ctx->input.mouse.grab = 1;
    ctx->input.mouse.ungrab = 1;
    ctx->input.mouse.grabbed = 1;
    ui.input_end();
    int ok = ctx->input.mouse.grab == 0 && ctx->input.mouse.ungrab == 0 &&
             ctx->input.mouse.grabbed == 0;
    GUI_DROP;
    REPORT("gui", "gui::input_end", "valid", ok);
}

static int c_gui_window_begin(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    /* NULL title returns 0 without opening a window: do NOT call
       window_end() afterwards (Nuklear asserts on an unmatched end). */
    int ok = 1;
    if (ui.window_begin(NULL, 0, 0, 10, 10, 0) != 0) ok = 0;
    int shown = ui.window_begin("edge", 8.0f, 8.0f, 200.0f, 120.0f, 0);
    if (shown != 1) ok = 0;
    ui.window_end();
    GUI_DROP;
    REPORT("gui", "gui::window_begin", "null", ok);
}

static int c_gui_window_end(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("edge", 8, 8, 200, 120, 0);
    ui.window_end();
    int ok = shown == 1 && ui.widget_bounds(NULL, NULL, NULL, NULL) == 0;
    GUI_DROP;
    REPORT("gui", "gui::window_end", "valid", ok);
}

static int c_gui_layout_dynamic(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("edge", 8, 8, 200, 120, 0);
    ui.layout_row_dynamic(0.0f, 0); /* zero columns: no-op */
    ui.layout_row_dynamic(30.0f, 1);
    ui.label("row");
    int w = 0, h = 0;
    int got = ui.widget_bounds(NULL, NULL, &w, &h);
    ui.window_end();
    int ok = shown == 1 && got == 1 && h > 0;
    GUI_DROP;
    REPORT("gui", "gui::layout_row_dynamic", "dim", ok);
}

static int c_gui_layout_static(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("edge", 8, 8, 200, 120, 0);
    ui.layout_row_static(24.0f, 80, 0); /* zero columns: no-op */
    ui.layout_row_static(24.0f, 80, 1);
    ui.label("row");
    int w = 0, h = 0;
    int got = ui.widget_bounds(NULL, NULL, &w, &h);
    ui.window_end();
    int ok = shown == 1 && got == 1 && w > 0 && w <= 80;
    GUI_DROP;
    REPORT("gui", "gui::layout_row_static", "dim", ok);
}

static int c_gui_button(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("edge", 8, 8, 200, 120, 0);
    ui.layout_row_dynamic(30.0f, 1);
    int pressed = ui.button("OK");
    int null_pressed = ui.button(NULL);
    ui.window_end();
    int ok = shown == 1 && null_pressed == 0 && (pressed == 0 || pressed == 1);
    GUI_DROP;
    REPORT("gui", "gui::button", "null", ok);
}

static int c_gui_label(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("edge", 8, 8, 200, 120, 0);
    ui.layout_row_dynamic(30.0f, 1);
    ui.label("text");
    ui.label(NULL); /* NULL text: no-op */
    ui.window_end();
    REPORT("gui", "gui::label", "null", shown == 1);
}

static int c_gui_slider(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("edge", 8, 8, 200, 120, 0);
    ui.layout_row_dynamic(30.0f, 1);
    int value = 5;
    int rc = ui.slider(&value, 0, 10);
    int null_rc = ui.slider(NULL, 0, 10);
    int bad_rc = ui.slider(&value, 10, 0); /* min > max */
    ui.window_end();
    int ok = shown == 1 && null_rc == 0 && bad_rc == 0 &&
             rc >= 0 && rc <= 10 && value >= 0 && value <= 10;
    GUI_DROP;
    REPORT("gui", "gui::slider", "oob", ok);
}

static int c_gui_widget_bounds(void) {
    GUI_DECLS;
    int before = ui.widget_bounds(NULL, NULL, NULL, NULL);
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("edge", 8, 8, 200, 120, 0);
    ui.layout_row_dynamic(30.0f, 1);
    ui.label("row");
    int x = 0, y = 0, w = 0, h = 0;
    int got = ui.widget_bounds(&x, &y, &w, &h);
    int null_got = ui.widget_bounds(NULL, NULL, NULL, NULL);
    ui.window_end();
    int ok = before == 0 && shown == 1 && got == 1 && w > 0 && h > 0 &&
             null_got == 1;
    GUI_DROP;
    REPORT("gui", "gui::widget_bounds", "null", ok);
}

static int c_gui_render(void) {
    GUI_DECLS;
    ui.input_begin();
    ui.input_end();
    int shown = ui.window_begin("edge", 8, 8, 200, 120, 0);
    ui.layout_row_dynamic(30.0f, 1);
    ui.label("row");
    ui.window_end();
    draw.set_color(0, 0, 0, 255, &e);
    draw.clear(&e);
    ui.render(0);
    draw.present(&e);
    int ok = shown == 1 && e.ok();
    GUI_DROP;
    REPORT("gui", "gui::render", "valid", ok);
}

static int c_gui_style_text_color(void) {
    GUI_DECLS;
    int ok = ui.style_text_color(200, 200, 200) == 1;
    GUI_DROP;
    REPORT("gui", "gui::style_text_color", "valid", ok);
}

static int c_gui_drop(void) {
    GUI_DECLS;
    ui.drop();
    int ok = ui.valid() == 0;
    GUI_DROP;
    REPORT("gui", "gui::drop", "valid", ok);
}

static int c_gui_drop2(void) {
    GUI_DECLS;
    ui.drop();
    ui.drop();
    int ok = ui.valid() == 0;
    GUI_DROP;
    REPORT("gui", "gui::drop", "drop2", ok);
}

/* =========================================================================
 * video.hp
 * ========================================================================= */
static int c_video_error_text(void) {
    char buf[128];
    video_error_text(-1, buf, sizeof(buf));
    int ok = buf[0] != '\0';
    video_error_text(-1, NULL, 0); /* NULL/size=0: no crash */
    buf[0] = 'x';
    video_error_text(0, buf, 0);
    if (buf[0] != 'x') ok = 0;
    REPORT("video", "video_error_text", "null", ok);
}

static int c_video_stream_params(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    int ok = video_stream_params(NULL, 0) == NULL &&
             video_stream_params(NULL, -1) == NULL;
    if (m.valid()) {
        if (video_stream_params(m.format, INT_MAX) != NULL) ok = 0;
        if (video_stream_params(m.format, 0) == NULL) ok = 0;
    } else {
        ok = 0;
    }
    m.drop();
    REPORT("video", "video_stream_params", "oob", ok);
}

static int c_video_codec_id(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    int ok = video_codec_id(NULL) == AV_CODEC_ID_NONE;
    if (m.valid()) {
        const AVCodecParameters *p = video_stream_params(m.format, 0);
        if (video_codec_id(p) == AV_CODEC_ID_NONE) ok = 0;
    } else {
        ok = 0;
    }
    m.drop();
    REPORT("video", "video_codec_id", "null", ok);
}

static int c_video_decoder_ctor(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_bad = decoder(NULL, -1, &e);
    int ok = !dc_bad.valid() && e.failed_now() == 1;
    dc_bad.drop();
    decoder oob = decoder(m.format, INT_MAX, &e);
    if (oob.valid() != 0) ok = 0;
    oob.drop();
    decoder dc_d = m.open_decoder(&e);
    if (!dc_d.valid()) ok = 0;
    dc_d.drop();
    m.drop();
    REPORT("video", "decoder::ctor", "null", ok);
}

static int c_video_media_ctor(void) {
    error e = error();
    ensure_media();
    media md_a = media("build/stdlib-tests/clip.mp4", &e);
    int ok = e.ok() && md_a.valid() == 1 && md_a.stream_count() > 0;
    md_a.drop();
    media md_b = media(NULL, &e);
    if (md_b.valid() != 0 || !e.ok()) ok = 0; /* NULL path: no error */
    md_b.drop();
    media md_c = media(bad_media_path(), &e);
    if (md_c.valid() != 0 || e.failed_now() != 1) ok = 0;
    md_c.drop();
    REPORT("video", "media::ctor", "trunc", ok);
}

static int c_video_media_valid(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    int ok = m.valid() == 1;
    m.drop();
    if (m.valid() != 0) ok = 0;
    REPORT("video", "media::valid", "valid", ok);
}

static int c_video_media_stream_count(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    media md_bad = media(NULL, &e);
    int ok = m.stream_count() > 0 && md_bad.stream_count() == 0;
    m.drop();
    md_bad.drop();
    REPORT("video", "media::stream_count", "valid", ok);
}

static int c_video_media_video_index(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    media md_bad = media(NULL, &e);
    int ok = m.video_index() >= 0 && md_bad.video_index() == -1;
    m.drop();
    md_bad.drop();
    REPORT("video", "media::video_index", "valid", ok);
}

static int c_video_media_video_width(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    media md_bad = media(NULL, &e);
    int ok = m.video_width() == 64 && md_bad.video_width() == 0;
    m.drop();
    md_bad.drop();
    REPORT("video", "media::video_width", "valid", ok);
}

static int c_video_media_video_height(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    media md_bad = media(NULL, &e);
    int ok = m.video_height() == 48 && md_bad.video_height() == 0;
    m.drop();
    md_bad.drop();
    REPORT("video", "media::video_height", "valid", ok);
}

static int c_video_media_video_codec(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    media md_bad = media(NULL, &e);
    int ok = strcmp(m.video_codec(), "h264") == 0 &&
             strcmp(md_bad.video_codec(), "none") == 0;
    m.drop();
    md_bad.drop();
    REPORT("video", "media::video_codec", "valid", ok);
}

static int c_video_media_duration(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    media md_bad = media(NULL, &e);
    int ok = m.duration_ms() > 0 && md_bad.duration_ms() == 0;
    m.drop();
    md_bad.drop();
    REPORT("video", "media::duration_ms", "valid", ok);
}

static int c_video_media_open_decoder(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    int ok = e.ok() && dc_d.valid() == 1;
    dc_d.drop();
    media md_bad = media(NULL, &e);
    decoder dc_none = md_bad.open_decoder(&e);
    if (dc_none.valid() != 0) ok = 0;
    dc_none.drop();
    md_bad.drop();
    m.drop();
    REPORT("video", "media::open_decoder", "null", ok);
}

static int c_video_decoder_valid(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    int ok = dc_d.valid() == 1;
    dc_d.drop();
    if (dc_d.valid() != 0) ok = 0;
    m.drop();
    REPORT("video", "decoder::valid", "valid", ok);
}

static int c_video_decoder_width(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    decoder dc_none = decoder(NULL, -1, &e);
    int ok = dc_d.width() == 64 && dc_none.width() == 0;
    dc_none.drop();
    dc_d.drop();
    m.drop();
    REPORT("video", "decoder::width", "valid", ok);
}

static int c_video_decoder_height(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    decoder dc_none = decoder(NULL, -1, &e);
    int ok = dc_d.height() == 48 && dc_none.height() == 0;
    dc_none.drop();
    dc_d.drop();
    m.drop();
    REPORT("video", "decoder::height", "valid", ok);
}

static int c_video_decoder_next_frame(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    frame f = frame(&e);
    int rc = dc_d.next_frame(&f, &e);
    int ok = rc == STDLIB_OK && f.valid() == 1;
    if (dc_d.next_frame(NULL, &e) != STDLIB_FAIL || e.failed_now() != 1) ok = 0;
    int last = rc;
    for (int i = 0; i < 2000 && last == STDLIB_OK; i++)
        last = dc_d.next_frame(&f, &e); /* drain to EOF */
    if (last != 1) ok = 0;
    f.drop();
    dc_d.drop();
    m.drop();
    REPORT("video", "decoder::next_frame", "null", ok);
}

static int c_video_frame_ctor(void) {
    error e = error();
    frame fr_a = frame(&e);
    int ok = e.ok() && fr_a.valid() == 1;
    fr_a.drop();
    frame fr_b = frame(NULL);
    if (!fr_b.valid()) ok = 0;
    fr_b.drop();
    REPORT("video", "frame::ctor", "null", ok);
}

static int c_video_frame_valid(void) {
    error e = error();
    frame f = frame(&e);
    int ok = f.valid() == 1;
    f.drop();
    if (f.valid() != 0) ok = 0;
    REPORT("video", "frame::valid", "valid", ok);
}

static int c_video_frame_width(void) {
    error e = error();
    frame fresh = frame(&e);
    int ok = fresh.width() == 0;
    fresh.drop();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    frame f = frame(&e);
    if (dc_d.next_frame(&f, &e) == STDLIB_OK && f.width() != 64) ok = 0;
    f.drop();
    dc_d.drop();
    m.drop();
    REPORT("video", "frame::width", "valid", ok);
}

static int c_video_frame_height(void) {
    error e = error();
    frame fresh = frame(&e);
    int ok = fresh.height() == 0;
    fresh.drop();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    frame f = frame(&e);
    if (dc_d.next_frame(&f, &e) == STDLIB_OK && f.height() != 48) ok = 0;
    f.drop();
    dc_d.drop();
    m.drop();
    REPORT("video", "frame::height", "valid", ok);
}

static int c_video_frame_format(void) {
    error e = error();
    frame fresh = frame(&e);
    int ok = fresh.format() == -1;
    fresh.drop();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    frame f = frame(&e);
    if (dc_d.next_frame(&f, &e) == STDLIB_OK && f.format() < 0) ok = 0;
    f.drop();
    dc_d.drop();
    m.drop();
    REPORT("video", "frame::format", "valid", ok);
}

static int c_video_frame_pts(void) {
    error e = error();
    frame fresh = frame(&e);
    /* A freshly allocated AVFrame has pts == AV_NOPTS_VALUE; only a dropped
       (invalid) frame is specified to report 0. */
    fresh.drop();
    int ok = fresh.pts() == 0;
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    frame f = frame(&e);
    if (dc_d.next_frame(&f, &e) == STDLIB_OK && f.pts() < 0) ok = 0;
    f.drop();
    dc_d.drop();
    m.drop();
    REPORT("video", "frame::pts", "valid", ok);
}

static int c_video_frame_drop(void) {
    error e = error();
    frame f = frame(&e);
    f.drop();
    int ok = f.valid() == 0 && f.width() == 0;
    REPORT("video", "frame::drop", "valid", ok);
}

static int c_video_frame_drop2(void) {
    error e = error();
    frame f = frame(&e);
    f.drop();
    f.drop();
    REPORT("video", "frame::drop", "drop2", f.valid() == 0);
}

static int c_video_decoder_drop(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    dc_d.drop();
    int ok = dc_d.valid() == 0 && dc_d.width() == 0;
    m.drop();
    REPORT("video", "decoder::drop", "valid", ok);
}

static int c_video_decoder_drop2(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    decoder dc_d = m.open_decoder(&e);
    dc_d.drop();
    dc_d.drop();
    int ok = dc_d.valid() == 0;
    m.drop();
    REPORT("video", "decoder::drop", "drop2", ok);
}

static int c_video_media_drop(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    m.drop();
    int ok = m.valid() == 0 && m.stream_count() == 0 && m.video_width() == 0;
    REPORT("video", "media::drop", "valid", ok);
}

static int c_video_media_drop2(void) {
    error e = error();
    media m = media("build/stdlib-tests/clip.mp4", &e);
    m.drop();
    m.drop();
    REPORT("video", "media::drop", "drop2", m.valid() == 0);
}

/* ---- case table --------------------------------------------------------- */
typedef int (*case_fn)(void);
struct case_entry {
    const char *hdr;
    const char *mth;
    const char *tag;
    case_fn fn;
};

#define C(H, M, T, F) { (H), (M), (T), (F) }

static const struct case_entry CASES[] = {
    /* window.hp (11 APIs) */
    C("window", "sdl::ctor", "valid", c_window_sdl_ctor_valid),
    C("window", "sdl::ctor", "null", c_window_sdl_ctor_null),
    C("window", "sdl::ctor", "dim", c_window_sdl_ctor_flags),
    C("window", "sdl::valid", "valid", c_window_sdl_valid),
    C("window", "sdl::subsystem_flags", "valid", c_window_sdl_flags_valid),
    C("window", "sdl::subsystem_flags", "dim", c_window_sdl_flags_zero),
    C("window", "sdl_backend_error", "valid", c_window_backend_error),
    C("window", "sdl::drop", "drop2", c_window_sdl_drop2),
    C("window", "window::ctor", "valid", c_window_ctor_valid),
    C("window", "window::ctor", "null", c_window_ctor_null),
    C("window", "window::ctor", "dim", c_window_ctor_dim),
    C("window", "window::ctor", "dimbig", c_window_ctor_dimbig),
    C("window", "window::valid", "valid", c_window_valid),
    C("window", "window::native", "valid", c_window_native),
    C("window", "window::size", "valid", c_window_size_valid),
    C("window", "window::size", "null", c_window_size_null),
    C("window", "window::set_title", "valid", c_window_set_title_valid),
    C("window", "window::set_title", "null", c_window_set_title_null),
    C("window", "window::drop", "valid", c_window_drop),
    C("window", "window::drop", "drop2", c_window_drop2),

    /* graphics.hp (17 APIs) */
    C("graphics", "renderer::ctor", "valid", c_gfx_renderer_ctor_valid),
    C("graphics", "renderer::ctor", "null", c_gfx_renderer_ctor_null),
    C("graphics", "renderer::ctor", "fmt", c_gfx_renderer_ctor_fmt),
    C("graphics", "renderer::valid", "valid", c_gfx_renderer_valid),
    C("graphics", "renderer::native", "valid", c_gfx_renderer_native),
    C("graphics", "renderer::set_color", "valid", c_gfx_renderer_set_color),
    C("graphics", "renderer::clear", "valid", c_gfx_renderer_clear),
    C("graphics", "renderer::fill_rect", "oob", c_gfx_renderer_fill_rect),
    C("graphics", "renderer::line", "oob", c_gfx_renderer_line),
    C("graphics", "renderer::set_vsync", "valid", c_gfx_renderer_set_vsync),
    C("graphics", "renderer::present", "valid", c_gfx_renderer_present),
    C("graphics", "renderer::read", "valid", c_gfx_renderer_read),
    C("graphics", "renderer::drop", "valid", c_gfx_renderer_drop),
    C("graphics", "renderer::drop", "drop2", c_gfx_renderer_drop2),
    C("graphics", "surface::ctor", "valid", c_gfx_surface_ctor),
    C("graphics", "surface::valid", "valid", c_gfx_surface_valid),
    C("graphics", "surface::width", "valid", c_gfx_surface_width),
    C("graphics", "surface::height", "valid", c_gfx_surface_height),
    C("graphics", "surface::pixel", "valid", c_gfx_surface_pixel),
    C("graphics", "surface::pixel", "oob", c_gfx_surface_pixel_oob),
    C("graphics", "surface::pixel", "null", c_gfx_surface_pixel_null),
    C("graphics", "surface::drop", "valid", c_gfx_surface_drop),
    C("graphics", "surface::drop", "drop2", c_gfx_surface_drop2),

    /* input.hp (10 APIs) */
    C("input", "poll_event", "valid", c_input_poll_valid),
    C("input", "poll_event", "null", c_input_poll_null),
    C("input", "event_is_quit", "null", c_input_is_quit),
    C("input", "event_is_key_down", "null", c_input_is_key_down),
    C("input", "event_is_key_up", "null", c_input_is_key_up),
    C("input", "event_scancode", "null", c_input_scancode),
    C("input", "event_text", "null", c_input_text),
    C("input", "keyboard_state", "valid", c_input_keyboard_state),
    C("input", "key_pressed", "oob", c_input_key_pressed),
    C("input", "mouse_state", "valid", c_input_mouse_state),
    C("input", "mouse_button_left", "valid", c_input_mouse_button_left),

    /* image.hp (12 APIs) */
    C("image", "image_from_pixels", "valid", c_image_from_pixels_valid),
    C("image", "image_from_pixels", "null", c_image_from_pixels_null),
    C("image", "image_from_pixels", "dim", c_image_from_pixels_dim),
    C("image", "image_from_pixels", "dimbig", c_image_from_pixels_dimbig),
    C("image", "image::ctor", "valid", c_image_ctor_valid),
    C("image", "image::ctor", "null", c_image_ctor_null),
    C("image", "image::ctor", "fmt", c_image_ctor_fmt),
    C("image", "image::ctor", "trunc", c_image_ctor_trunc),
    C("image", "image::ctor", "dim", c_image_ctor_channels),
    C("image", "image::valid", "valid", c_image_valid),
    C("image", "image::width", "valid", c_image_width),
    C("image", "image::height", "valid", c_image_height),
    C("image", "image::channels", "valid", c_image_channels),
    C("image", "image::size_bytes", "valid", c_image_size_bytes),
    C("image", "image::pixel", "valid", c_image_pixel),
    C("image", "image::pixel", "oob", c_image_pixel_oob),
    C("image", "image::write_png", "null", c_image_write_png),
    C("image", "image::write_bmp", "null", c_image_write_bmp),
    C("image", "image::drop", "valid", c_image_drop),
    C("image", "image::drop", "drop2", c_image_drop2),
    C("image", "image_from_memory", "valid", c_image_from_memory_valid),
    C("image", "image_from_memory", "null", c_image_from_memory_null),
    C("image", "image_from_memory", "trunc", c_image_from_memory_trunc),
    C("image", "image_from_memory", "fmt", c_image_from_memory_fmt),

    /* audio.hp (32 APIs) */
    C("audio", "audio_sleep_ms", "valid", c_audio_sleep),
    C("audio", "decoder_from_memory", "valid", c_audio_decoder_from_memory_valid),
    C("audio", "decoder_from_memory", "null", c_audio_decoder_from_memory_null),
    C("audio", "decoder_from_memory", "trunc", c_audio_decoder_from_memory_trunc),
    C("audio", "audio_decoder::ctor", "valid", c_audio_decoder_ctor),
    C("audio", "audio_decoder::ctor", "null", c_audio_decoder_ctor_null),
    C("audio", "audio_decoder::ctor", "trunc", c_audio_decoder_ctor_trunc),
    C("audio", "audio_decoder::ctor", "fmt", c_audio_decoder_ctor_fmt),
    C("audio", "audio_decoder::valid", "valid", c_audio_decoder_valid),
    C("audio", "audio_decoder::channels", "valid", c_audio_decoder_channels),
    C("audio", "audio_decoder::sample_rate", "valid", c_audio_decoder_sample_rate),
    C("audio", "audio_decoder::frame_count", "valid", c_audio_decoder_frame_count),
    C("audio", "audio_decoder::read", "null", c_audio_decoder_read),
    C("audio", "audio_decoder::drop", "valid", c_audio_decoder_drop),
    C("audio", "audio_decoder::drop", "drop2", c_audio_decoder_drop2),
    C("audio", "audio_context::ctor", "valid", c_audio_context_ctor),
    C("audio", "audio_context::ctor", "null", c_audio_context_ctor_null),
    C("audio", "audio_context::valid", "valid", c_audio_context_valid),
    C("audio", "audio_context::native", "valid", c_audio_context_native),
    C("audio", "audio_context::drop", "valid", c_audio_context_drop),
    C("audio", "audio_context::drop", "drop2", c_audio_context_drop2),
    C("audio", "audio_engine::ctor", "valid", c_audio_engine_ctor),
    C("audio", "audio_engine::ctor", "null", c_audio_engine_ctor_null),
    C("audio", "audio_engine::valid", "valid", c_audio_engine_valid),
    C("audio", "audio_engine::native", "valid", c_audio_engine_native),
    C("audio", "audio_engine::set_volume", "oob", c_audio_engine_set_volume),
    C("audio", "audio_engine::play", "null", c_audio_engine_play),
    C("audio", "audio_engine::load", "null", c_audio_engine_load),
    C("audio", "audio_engine::drop", "valid", c_audio_engine_drop),
    C("audio", "audio_engine::drop", "drop2", c_audio_engine_drop2),
    C("audio", "audio_sound::ctor", "null", c_audio_sound_ctor),
    C("audio", "audio_sound::valid", "valid", c_audio_sound_valid),
    C("audio", "audio_sound::play", "null", c_audio_sound_play),
    C("audio", "audio_sound::stop", "valid", c_audio_sound_stop),
    C("audio", "audio_sound::playing", "valid", c_audio_sound_playing),
    C("audio", "audio_sound::drop", "valid", c_audio_sound_drop),
    C("audio", "audio_sound::drop", "drop2", c_audio_sound_drop2),
    C("audio", "audio_device::ctor", "valid", c_audio_device_ctor),
    C("audio", "audio_device::ctor", "null", c_audio_device_ctor_null),
    C("audio", "audio_device::ctor", "dim", c_audio_device_ctor_dim),
    C("audio", "audio_device::valid", "valid", c_audio_device_valid),
    C("audio", "audio_device::start", "valid", c_audio_device_start),
    C("audio", "audio_device::running", "valid", c_audio_device_running),
    C("audio", "audio_device::stop", "valid", c_audio_device_stop),
    C("audio", "audio_device::drop", "valid", c_audio_device_drop),
    C("audio", "audio_device::drop", "drop2", c_audio_device_drop2),

    /* gui.hp (19 APIs) */
    C("gui", "gui::ctor", "null", c_gui_ctor),
    C("gui", "gui::valid", "valid", c_gui_valid),
    C("gui", "gui::native", "valid", c_gui_native),
    C("gui", "gui::handle_event", "null", c_gui_handle_event),
    C("gui", "gui::input_begin", "valid", c_gui_input_begin),
    C("gui", "gui::input_motion", "oob", c_gui_input_motion),
    C("gui", "gui::input_button", "valid", c_gui_input_button),
    C("gui", "gui::input_end", "valid", c_gui_input_end),
    C("gui", "gui::window_begin", "null", c_gui_window_begin),
    C("gui", "gui::window_end", "valid", c_gui_window_end),
    C("gui", "gui::layout_row_dynamic", "dim", c_gui_layout_dynamic),
    C("gui", "gui::layout_row_static", "dim", c_gui_layout_static),
    C("gui", "gui::button", "null", c_gui_button),
    C("gui", "gui::label", "null", c_gui_label),
    C("gui", "gui::slider", "oob", c_gui_slider),
    C("gui", "gui::widget_bounds", "null", c_gui_widget_bounds),
    C("gui", "gui::render", "valid", c_gui_render),
    C("gui", "gui::style_text_color", "valid", c_gui_style_text_color),
    C("gui", "gui::drop", "valid", c_gui_drop),
    C("gui", "gui::drop", "drop2", c_gui_drop2),

    /* video.hp (26 APIs) */
    C("video", "video_error_text", "null", c_video_error_text),
    C("video", "video_stream_params", "oob", c_video_stream_params),
    C("video", "video_codec_id", "null", c_video_codec_id),
    C("video", "decoder::ctor", "null", c_video_decoder_ctor),
    C("video", "media::ctor", "trunc", c_video_media_ctor),
    C("video", "media::valid", "valid", c_video_media_valid),
    C("video", "media::stream_count", "valid", c_video_media_stream_count),
    C("video", "media::video_index", "valid", c_video_media_video_index),
    C("video", "media::video_width", "valid", c_video_media_video_width),
    C("video", "media::video_height", "valid", c_video_media_video_height),
    C("video", "media::video_codec", "valid", c_video_media_video_codec),
    C("video", "media::duration_ms", "valid", c_video_media_duration),
    C("video", "media::open_decoder", "null", c_video_media_open_decoder),
    C("video", "decoder::valid", "valid", c_video_decoder_valid),
    C("video", "decoder::width", "valid", c_video_decoder_width),
    C("video", "decoder::height", "valid", c_video_decoder_height),
    C("video", "decoder::next_frame", "null", c_video_decoder_next_frame),
    C("video", "frame::ctor", "null", c_video_frame_ctor),
    C("video", "frame::valid", "valid", c_video_frame_valid),
    C("video", "frame::width", "valid", c_video_frame_width),
    C("video", "frame::height", "valid", c_video_frame_height),
    C("video", "frame::format", "valid", c_video_frame_format),
    C("video", "frame::pts", "valid", c_video_frame_pts),
    C("video", "frame::drop", "valid", c_video_frame_drop),
    C("video", "frame::drop", "drop2", c_video_frame_drop2),
    C("video", "decoder::drop", "valid", c_video_decoder_drop),
    C("video", "decoder::drop", "drop2", c_video_decoder_drop2),
    C("video", "media::drop", "valid", c_video_media_drop),
    C("video", "media::drop", "drop2", c_video_media_drop2),
};

#define NCASES ((int)(sizeof(CASES) / sizeof(CASES[0])))

/* Number of distinct (header, method) pairs == public APIs covered. */
static int count_apis(void) {
    int n = 0;
    int i;
    for (i = 0; i < NCASES; i++) {
        if (i == 0 || strcmp(CASES[i].hdr, CASES[i - 1].hdr) != 0 ||
            strcmp(CASES[i].mth, CASES[i - 1].mth) != 0)
            n++;
    }
    return n;
}

/* ---- fork-per-case driver with fr_a bounded 60s timeout -------------------- */
static volatile sig_atomic_t g_child_pid = 0;
static volatile sig_atomic_t g_timed_out = 0;

static void on_sigalrm(int sig) {
    (void)sig;
    g_timed_out = 1;
    if (g_child_pid > 0)
        kill((pid_t)g_child_pid, SIGKILL);
}

static int run_case(int id) {
    const struct case_entry *entry = &CASES[id];
    int status = 0;
    pid_t pid, w;
    fflush(stdout);
    pid = fork();
    if (pid < 0) {
        printf("%s %s %s FAIL (fork failed)\n", entry->hdr, entry->mth, entry->tag);
        return 1;
    }
    if (pid == 0) {
        int rc;
        g_child_pid = 0;
        rc = entry->fn(); /* prints exactly one result line */
        fflush(stdout);
        _exit(rc == 0 ? 0 : 1);
    }
    g_child_pid = (sig_atomic_t)pid;
    g_timed_out = 0;
    signal(SIGALRM, on_sigalrm);
    alarm(60); /* bounded timeout: 60 seconds per forked case */
    do {
        w = waitpid(pid, &status, 0);
    } while (w < 0 && errno == EINTR);
    alarm(0);
    signal(SIGALRM, SIG_DFL);
    g_child_pid = 0;
    if (w < 0) {
        printf("%s %s %s FAIL (waitpid error)\n", entry->hdr, entry->mth, entry->tag);
        return 1;
    }
    if (WIFSIGNALED(status)) {
        printf("%s %s %s FAIL (signal %d%s)\n", entry->hdr, entry->mth, entry->tag,
               WTERMSIG(status), g_timed_out ? " timeout" : "");
        return 1;
    }
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0)
        return 0;
    return 1; /* child already printed its FAIL line */
}

int main(int argc, char **argv) {
    int i, passed = 0, failed = 0;
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (argc == 2 && strcmp(argv[1], "list") == 0) {
        for (i = 0; i < NCASES; i++)
            printf("%d %s %s %s\n", i, CASES[i].hdr, CASES[i].mth,
                   CASES[i].tag);
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "apis") == 0) {
        printf("%d\n", count_apis());
        return 0;
    }
    if (argc == 2 && strcmp(argv[1], "all") == 0) {
        for (i = 0; i < NCASES; i++) {
            if (run_case(i) == 0)
                passed++;
            else
                failed++;
        }
        printf("EDGE100 multimedia %d %d %d %d\n", count_apis(), NCASES,
               passed, failed);
        return 0;
    }
    if (argc == 2) {
        int id = atoi(argv[1]);
        if (id < 0 || id >= NCASES) {
            fprintf(stderr, "case id out of range: %d (0..%d)\n", id,
                    NCASES - 1);
            return 2;
        }
        return run_case(id);
    }
    fprintf(stderr, "usage: %s list|apis|all|<case-id>\n", argv[0]);
    return 2;
}
