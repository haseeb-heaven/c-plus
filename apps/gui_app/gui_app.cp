/* gui_app.cp -- a deterministic headless GUI app built on stdlib/widgets.hp.
 *
 * This is a teaching/demo app, NOT a production window application and NOT a
 * memory-safety proof.  It uses two deliberately separate paths that share one
 * deterministic widget core:
 *
 *   1. `describe <id>` -- pure widget core.  Build a fixed tree of labels,
 *      buttons, inputs and a panel; run integer grid layout; hit-test two
 *      fixed points; serialize the tree; print the exact text and a CRC32 over
 *      the serialized bytes.  No SDL init, no display, no clock, no network.
 *
 *   2. `render <w> <h> <id>` -- the same core, then a REAL SDL3 window and
 *      software renderer (hidden, `SDL_VIDEODRIVER=dummy` in run.sh).  Each
 *      laid-out widget becomes an SDL_FRect; one frame is cleared, filled and
 *      presented.  The transcript is the core plus one GUI_SDL line.
 *
 *   3. `refuse <case>` -- fault injection through the widgets.hp `error *`
 *      contract: null windows/buffers/outputs, bad geometry, duplicate ids,
 *      oversized/control-character labels, out-of-range indices and invalid
 *      SDL window sizes.  Each emits a stable GUI_REFUSED line.
 *
 * Only stdlib/widgets.hp is used for widget state, layout, hit testing and
 * serialization.  SDL3 is used only for the real headless frame; it never
 * decides the transcript.  All numbers are fixed; the same argv always gives
 * the same stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/gui_app/gui_app.cp --default --sanitize=none -I. \
 *     $(pkg-config --cflags --libs sdl3) -o apps/gui_app/bin/gui_app \
 *     -- build/libc/libcplus_hardened.a
 * Run:
 *   SDL_VIDEODRIVER=dummy timeout 30 apps/gui_app/bin/gui_app describe 0
 *   SDL_VIDEODRIVER=dummy timeout 30 apps/gui_app/bin/gui_app render 640 480 0
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL3/SDL.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/widgets.hp"

/* Reject absurd SDL window requests before they reach SDL. */
#define GUI_MAX_DIM 4096
#define GUI_SCENARIOS 3
#define GUI_SERIAL_CAP 4096
#define GUI_TAG_CAP 128

/* Widget definitions use the public kind tags from widgets.hp. */
typedef struct gui_def {
    int id;
    int kind;
    const char *label;
} gui_def;

typedef struct gui_scenario {
    int id;
    const char *title;
    int grid_x;
    int grid_y;
    int grid_w;
    int grid_h;
    int columns;
    int hit1_x;
    int hit1_y;
    int hit2_x;
    int hit2_y;
    int disable_pos; /* widget position disabled before hit2, or -1 */
    int defs_count;
    const gui_def *defs;
} gui_scenario;

/* ---- fixed widget trees -------------------------------------------------- */

static const gui_def g_login[] = {
    {1, STDLIB_WIDGET_PANEL, "root"},
    {2, STDLIB_WIDGET_LABEL, "user"},
    {3, STDLIB_WIDGET_INPUT, "name"},
    {4, STDLIB_WIDGET_LABEL, "pass"},
    {5, STDLIB_WIDGET_INPUT, "secret"},
    {6, STDLIB_WIDGET_BUTTON, "OK"},
};

static const gui_def g_toolbar[] = {
    {10, STDLIB_WIDGET_PANEL, "tools"},
    {11, STDLIB_WIDGET_BUTTON, "NEW"},
    {12, STDLIB_WIDGET_BUTTON, "OPEN"},
    {13, STDLIB_WIDGET_BUTTON, "SAVE"},
    {14, STDLIB_WIDGET_LABEL, "sep"},
    {15, STDLIB_WIDGET_BUTTON, "CUT"},
    {16, STDLIB_WIDGET_BUTTON, "COPY"},
    {17, STDLIB_WIDGET_BUTTON, "PASTE"},
};

static const gui_def g_dialog[] = {
    {20, STDLIB_WIDGET_PANEL, "dlg"},
    {21, STDLIB_WIDGET_LABEL, "sure"},
    {22, STDLIB_WIDGET_BUTTON, "YES"},
    {23, STDLIB_WIDGET_BUTTON, "NO"},
};

static const gui_scenario g_scenarios[GUI_SCENARIOS] = {
    /* login: 6 widgets, 3 columns -> 2 rows of 80x45 cells. */
    {0, "login", 0, 0, 240, 90, 3, 5, 5, 85, 5, -1, 6, g_login},
    /* toolbar: 8 widgets, 4 columns -> 2 rows of 80x40 cells. */
    {1, "toolbar", 0, 0, 320, 80, 4, 5, 5, 245, 45, -1, 8, g_toolbar},
    /* dialog: disable YES (position 2) then hit its cell -> a miss. */
    {2, "dialog", 0, 0, 200, 80, 2, 5, 5, 5, 45, 2, 4, g_dialog},
};

static const gui_scenario *gui_scenario_get(int id, error *err) {
    error_reset(err);
    if (id < 0 || id >= GUI_SCENARIOS) {
        (void)error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                             "scenario: id out of range");
        return (const gui_scenario *)0;
    }
    return &g_scenarios[id];
}

/* ---- deterministic core -------------------------------------------------- */

/* Reserve placeholder rects (valid until layout replaces them). */
static int gui_tree_build(const gui_scenario *sc, stdlib_window *win,
                          error *err) {
    int i;
    int added = -1;
    if (stdlib_window_init(win, (const stdlib_allocator *)0, sc->grid_x,
                           sc->grid_y, sc->grid_w, sc->grid_h, sc->columns,
                           err) != STDLIB_OK)
        return STDLIB_FAIL;
    for (i = 0; i < sc->defs_count; i++) {
        const gui_def *d = &sc->defs[i];
        added = stdlib_widget_add(win, d->id, d->kind, d->label, 1, 1, 8, 8,
                                  err);
        if (added < 0) {
            stdlib_window_drop(win, err);
            return STDLIB_FAIL;
        }
    }
    if (stdlib_window_layout(win, err) != STDLIB_OK) {
        stdlib_window_drop(win, err);
        return STDLIB_FAIL;
    }
    if (sc->disable_pos >= 0 &&
        stdlib_widget_enable(win, (size_t)sc->disable_pos, 0, err) !=
            STDLIB_OK) {
        stdlib_window_drop(win, err);
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

/* Reflected IEEE CRC32 over the exact serialized bytes (no table, no locale). */
static unsigned int gui_crc32(const char *data, size_t n) {
    unsigned int crc = 0xffffffffu;
    size_t i;
    for (i = 0u; i < n; i++) {
        int bit;
        crc ^= (unsigned int)(unsigned char)data[i];
        for (bit = 0; bit < 8; bit++)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

static void gui_hit_line(const stdlib_window *win, int px, int py, error *err) {
    size_t hit_pos = (size_t)-1;
    int hit = stdlib_window_hit_test(win, px, py, &hit_pos, err);
    if (hit == 1 && hit_pos < win->count) {
        const stdlib_widget *w = &win->items[hit_pos];
        printf("GUI_HIT p=%d,%d index=%zu id=%d label=%s\n", px, py, hit_pos,
               w->id, w->label);
    } else if (hit == 0) {
        printf("GUI_HIT p=%d,%d miss=1\n", px, py);
    } else {
        printf("GUI_HIT p=%d,%d ERROR=%d\n", px, py, hit);
    }
}

/* Print the whole deterministic transcript for a built tree. */
static int gui_tree_report(const gui_scenario *sc, const stdlib_window *win,
                           error *err) {
    char report_serial[GUI_SERIAL_CAP];
    size_t need = 0u;
    int cell_w = 0;
    int cell_h = 0;

    if (stdlib_window_serialized_size(win, &need, err) != STDLIB_OK)
        return STDLIB_FAIL;
    if (need == 0u || need > (size_t)GUI_SERIAL_CAP)
        return error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                              "widgets: serialized tree exceeds demo cap"),
               STDLIB_FAIL;
    if (stdlib_window_serialize(win, report_serial, need, err) != STDLIB_OK)
        return STDLIB_FAIL;

    printf("GUI_WIDGETS scenario=%d title=%s count=%zu columns=%d grid=%dx%d\n",
           sc->id, sc->title, win->count, sc->columns, sc->grid_w, sc->grid_h);
    if (win->count > 0u) {
        cell_w = win->items[0].w + 2 * STDLIB_WIDGET_GRID_INSET;
        cell_h = win->items[0].h + 2 * STDLIB_WIDGET_GRID_INSET;
    }
    printf("GUI_LAYOUT cell=%dx%d origin=%d,%d\n", cell_w, cell_h, sc->grid_x,
           sc->grid_y);
    printf("%s", report_serial);
    printf("GUI_SERIAL bytes=%zu crc32=%08x\n", need - 1u,
           gui_crc32(report_serial, need - 1u));
    gui_hit_line(win, sc->hit1_x, sc->hit1_y, err);
    gui_hit_line(win, sc->hit2_x, sc->hit2_y, err);
    return STDLIB_OK;
}

/* ---- real headless SDL3 frame ------------------------------------------- */

static int gui_validate_dims(int width, int height, error *err) {
    error_reset(err);
    if (width <= 0 || height <= 0)
        return error_fail_cat(err, STDLIB_CATEGORY_INVALID,
                              CPLUS_EXIT_UNSUPPORTED,
                              "gui_app: window dimensions must be positive"),
               STDLIB_FAIL;
    if (width > GUI_MAX_DIM || height > GUI_MAX_DIM)
        return error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                              "gui_app: window dimensions exceed GUI_MAX_DIM"),
               STDLIB_FAIL;
    return STDLIB_OK;
}

static int gui_sdl_frame(const stdlib_window *win, int width, int height,
                         error *err) {
    SDL_Window *native_window = (SDL_Window *)0;
    SDL_Renderer *native_renderer = (SDL_Renderer *)0;
    size_t i;
    int failed = 0;

    if (gui_validate_dims(width, height, err) != STDLIB_OK)
        return STDLIB_FAIL;

    /* C+ lowering may re-evaluate a side-effecting condition expression, and
     * SDL_Init is not idempotent: call it once into a local and test that. */
    {
        int init_rc = SDL_Init(SDL_INIT_VIDEO);
        if (init_rc == 0) {
            error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                           "gui_app: SDL_Init(VIDEO) failed");
            return STDLIB_FAIL;
        }
    }
    native_window = SDL_CreateWindow("gui_app", width, height,
                                     SDL_WINDOW_HIDDEN);
    if (native_window == (SDL_Window *)0) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "gui_app: SDL_CreateWindow failed");
        failed = 1;
    } else {
        native_renderer = SDL_CreateRenderer(native_window, (const char *)0);
        if (native_renderer == (SDL_Renderer *)0) {
            error_fail_cat(err, STDLIB_CATEGORY_INVALID,
                           CPLUS_EXIT_UNSUPPORTED,
                           "gui_app: SDL_CreateRenderer failed");
            failed = 1;
        }
    }

    if (!failed) {
        SDL_FRect rect;
        (void)SDL_SetRenderDrawColor(native_renderer, 16, 20, 28, 255);
        (void)SDL_RenderClear(native_renderer);
        for (i = 0u; i < win->count; i++) {
            const stdlib_widget *w = &win->items[i];
            if (w->kind == STDLIB_WIDGET_BUTTON)
                (void)SDL_SetRenderDrawColor(native_renderer, 60, 140, 220,
                                             255);
            else if (w->kind == STDLIB_WIDGET_PANEL)
                (void)SDL_SetRenderDrawColor(native_renderer, 40, 48, 60, 255);
            else if (w->kind == STDLIB_WIDGET_INPUT)
                (void)SDL_SetRenderDrawColor(native_renderer, 220, 180, 60,
                                             255);
            else
                (void)SDL_SetRenderDrawColor(native_renderer, 120, 120, 130,
                                             255);
            if ((w->flags & STDLIB_WIDGET_VISIBLE) == 0u)
                continue;
            rect.x = (float)w->x;
            rect.y = (float)w->y;
            rect.w = (float)w->w;
            rect.h = (float)w->h;
            (void)SDL_RenderFillRect(native_renderer, &rect);
        }
        (void)SDL_RenderPresent(native_renderer);
        printf("GUI_SDL window=%dx%d hidden=1 frame=1 widgets=%zu driver=%s\n",
               width, height, win->count, SDL_GetCurrentVideoDriver());
    }

    if (native_renderer != (SDL_Renderer *)0)
        SDL_DestroyRenderer(native_renderer);
    if (native_window != (SDL_Window *)0)
        SDL_DestroyWindow(native_window);
    SDL_Quit();
    return failed ? STDLIB_FAIL : STDLIB_OK;
}

/* ---- commands ------------------------------------------------------------ */

static void gui_print_refusal(const char *tag, error *err) {
    printf("GUI_REFUSED tag=%s category=%d code=%d message=%s\n", tag,
           err->category(), err->code(), err->what());
}

static char *gui_arg(char **argv, int pos) {
    char *value = (char *)0;
    __builtin_memcpy(&value, argv + pos, sizeof(value));
    return value;
}

static int gui_parse_int(const char *text, int *out) {
    char *end = (char *)0;
    long value;
    if (text == (const char *)0 || out == (int *)0)
        return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE)
        return -1;
    if (value < (long)INT_MIN || value > (long)INT_MAX)
        return -1;
    *out = (int)value;
    return 0;
}

static void gui_tag(char *buf, int cap, const char *kind, int a, int b, int c) {
    snprintf(buf, (size_t)cap, "%s:%d,%d,%d", kind, a, b, c);
}

static void cmd_describe(int argc, char **argv, error *err) {
    char tag[GUI_TAG_CAP];
    const gui_scenario *sc = (const gui_scenario *)0;
    stdlib_window win;
    int id = 0;
    int ok = 0;

    if (argc >= 3 && gui_parse_int(gui_arg(argv, 2), &id) == 0) {
        sc = gui_scenario_get(id, err);
        ok = sc != (const gui_scenario *)0;
    }
    if (ok)
        ok = gui_tree_build(sc, &win, err) == STDLIB_OK;
    if (ok)
        ok = gui_tree_report(sc, &win, err) == STDLIB_OK;
    if (ok)
        stdlib_window_drop(&win, err);

    gui_tag(tag, (int)sizeof(tag), "describe", id, 0, 0);
    if (ok) {
        print("GUI_RESULT", tag, "OK");
    } else {
        if (err->failed_now() == 0)
            error_fail_cat(err, STDLIB_CATEGORY_INVALID,
                           CPLUS_EXIT_UNSUPPORTED,
                           "describe: missing or invalid arguments");
        gui_print_refusal(tag, err);
        print("GUI_RESULT", tag, "REFUSED", err->code());
    }
}

static void cmd_render(int argc, char **argv, error *err) {
    char tag[GUI_TAG_CAP];
    char saved_message[STDLIB_ERROR_SIZE];
    const gui_scenario *sc = (const gui_scenario *)0;
    stdlib_window win;
    int width = 0;
    int height = 0;
    int id = 0;
    int ok = 0;
    int saved_category = STDLIB_CATEGORY_OK;
    int saved_code = 0;
    int tree_built = 0;

    if (argc >= 5 && gui_parse_int(gui_arg(argv, 2), &width) == 0 &&
        gui_parse_int(gui_arg(argv, 3), &height) == 0 &&
        gui_parse_int(gui_arg(argv, 4), &id) == 0) {
        sc = gui_scenario_get(id, err);
        ok = sc != (const gui_scenario *)0;
    }
    if (ok) {
        ok = gui_tree_build(sc, &win, err) == STDLIB_OK;
        tree_built = ok;
    }
    if (ok)
        ok = gui_tree_report(sc, &win, err) == STDLIB_OK;
    if (ok)
        ok = gui_sdl_frame(&win, width, height, err) == STDLIB_OK;

    /* gui_tree_build/report/frame report through `err`; cleanup resets it, so
     * snapshot the refusal before dropping the widget vector. */
    if (!ok && err->failed_now()) {
        saved_category = err->category();
        saved_code = err->code();
        snprintf(saved_message, sizeof(saved_message), "%s", err->what());
    }
    if (tree_built)
        stdlib_window_drop(&win, err);
    if (!ok && saved_code != 0)
        error_fail_cat(err, saved_category, (cplus_exit_code)saved_code,
                       saved_message);

    gui_tag(tag, (int)sizeof(tag), "render", width, height, id);
    if (ok) {
        print("GUI_RESULT", tag, "OK");
    } else {
        if (err->failed_now() == 0)
            error_fail_cat(err, STDLIB_CATEGORY_INVALID,
                           CPLUS_EXIT_UNSUPPORTED,
                           "render: missing or invalid arguments");
        gui_print_refusal(tag, err);
        print("GUI_RESULT", tag, "REFUSED", err->code());
    }
}

/* Fault injection: every case must fail closed through widgets.hp error *. */
static void cmd_refuse(int argc, char **argv, error *err) {
    const char *name = argc >= 3 ? gui_arg(argv, 2) : "missing";
    stdlib_window win;
    const gui_scenario *sc = (const gui_scenario *)0;
    char refuse_serial[8];
    size_t hit_pos = (size_t)-1;
    size_t need = 0u;
    char long_label[128];
    int added;
    int i;
    int refusal_code = 0;

    error_reset(err);
    memset(&win, 0, sizeof(win));
    memset(long_label, 'x', sizeof(long_label));
    long_label[sizeof(long_label) - 1u] = '\0';

    if (strcmp(name, "bad-scenario") == 0) {
        (void)gui_scenario_get(99, err);
    } else if (strcmp(name, "bad-window-geometry") == 0) {
        (void)stdlib_window_init(&win, (const stdlib_allocator *)0, 0, 0, 0, 0,
                                 0, err);
    } else if (strcmp(name, "null-window") == 0) {
        (void)stdlib_window_layout((stdlib_window *)0, err);
    } else if (strcmp(name, "small-grid") == 0) {
        (void)stdlib_window_init(&win, (const stdlib_allocator *)0, 0, 0, 6, 6,
                                 3, err);
        for (i = 0; i < 6; i++) {
            added = stdlib_widget_add(&win, i + 1, STDLIB_WIDGET_BUTTON, "b",
                                      1, 1, 8, 8, err);
            if (added < 0)
                break;
        }
        if (added >= 0)
            (void)stdlib_window_layout(&win, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "bad-widget-bounds") == 0) {
        (void)stdlib_window_init(&win, (const stdlib_allocator *)0, 0, 0, 240,
                                 90, 3, err);
        (void)stdlib_widget_add(&win, 1, STDLIB_WIDGET_BUTTON, "OK", 1, 1, 0,
                                8, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "duplicate-id") == 0) {
        (void)stdlib_window_init(&win, (const stdlib_allocator *)0, 0, 0, 240,
                                 90, 3, err);
        (void)stdlib_widget_add(&win, 7, STDLIB_WIDGET_BUTTON, "A", 1, 1, 8, 8,
                                err);
        (void)stdlib_widget_add(&win, 7, STDLIB_WIDGET_BUTTON, "B", 1, 1, 8, 8,
                                err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "long-label") == 0) {
        (void)stdlib_window_init(&win, (const stdlib_allocator *)0, 0, 0, 240,
                                 90, 3, err);
        (void)stdlib_widget_add(&win, 1, STDLIB_WIDGET_BUTTON, long_label, 1, 1,
                                8, 8, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "newline-label") == 0) {
        (void)stdlib_window_init(&win, (const stdlib_allocator *)0, 0, 0, 240,
                                 90, 3, err);
        (void)stdlib_widget_add(&win, 1, STDLIB_WIDGET_BUTTON, "a\nb", 1, 1, 8,
                                8, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "null-label") == 0) {
        (void)stdlib_window_init(&win, (const stdlib_allocator *)0, 0, 0, 240,
                                 90, 3, err);
        (void)stdlib_widget_add(&win, 1, STDLIB_WIDGET_BUTTON,
                                (const char *)0, 1, 1, 8, 8, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "bad-index") == 0) {
        (void)stdlib_window_init(&win, (const stdlib_allocator *)0, 0, 0, 240,
                                 90, 3, err);
        (void)stdlib_widget_enable(&win, (size_t)99, 0, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "null-hit-output") == 0) {
        sc = gui_scenario_get(0, err);
        (void)gui_tree_build(sc, &win, err);
        (void)stdlib_window_hit_test(&win, 5, 5, (size_t *)0, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "null-serialize-buffer") == 0) {
        sc = gui_scenario_get(0, err);
        (void)gui_tree_build(sc, &win, err);
        (void)stdlib_window_serialize(&win, (char *)0, 8u, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "small-serialize-buffer") == 0) {
        sc = gui_scenario_get(0, err);
        (void)gui_tree_build(sc, &win, err);
        (void)stdlib_window_serialized_size(&win, &need, err);
        (void)stdlib_window_serialize(&win, refuse_serial, 1u, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "bad-render-zero") == 0) {
        (void)gui_validate_dims(0, 480, err);
    } else if (strcmp(name, "bad-render-huge") == 0) {
        (void)gui_validate_dims(100000, 100000, err);
    } else if (strcmp(name, "null-serial-size-output") == 0) {
        sc = gui_scenario_get(0, err);
        (void)gui_tree_build(sc, &win, err);
        (void)stdlib_window_serialized_size(&win, (size_t *)0, err);
        /* drop is deferred until after the refusal is printed: drop resets err */
    } else if (strcmp(name, "bad-hit-test-window") == 0) {
        (void)stdlib_window_hit_test((stdlib_window *)0, 5, 5, &hit_pos, err);
    } else {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "refuse: unknown case");
    }

    gui_print_refusal(name, err);
    refusal_code = err->code();
    stdlib_window_drop(&win, err); /* resets err; refusal already printed */
    print("GUI_RESULT", "refuse", name, "REFUSED", refusal_code);
}

int main(int argc, char **argv) {
    error err = error();
    const char *cmd = "describe";

    stdlib_set_error_callback((stdlib_error_callback)0);

    if (argc > 1)
        cmd = gui_arg(argv, 1);

    if (strcmp(cmd, "describe") == 0) {
        cmd_describe(argc, argv, &err);
    } else if (strcmp(cmd, "render") == 0) {
        cmd_render(argc, argv, &err);
    } else if (strcmp(cmd, "refuse") == 0) {
        cmd_refuse(argc, argv, &err);
    } else {
        error_fail_cat(&err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "gui_app: unknown command");
        gui_print_refusal("command", &err);
        print("GUI_RESULT", "command", "REFUSED", err.code());
    }
    return 0;
}
