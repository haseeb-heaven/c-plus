/* dashboard.cp -- a headless-testable Nuklear GUI example written in C+.
 *
 * This is a TEACHING example, not a production GUI application.  It shows the
 * safe-shape pattern for the C+ stdlib GUI wrappers:
 *
 *   - stdlib/window.hp  -- `sdl` process lifetime + `window` + `renderer`
 *   - stdlib/gui.hp     -- RAII-style `gui` around a real Nuklear context,
 *                          with label / layout / button / render calls
 *   - stdlib/error.hp   -- recoverable `error *` refusals for hostile inputs
 *
 * Two independent paths share ONE deterministic core (`dash_scenario_get` +
 * `dash_describe`):
 *
 *   1. `describe <id>` -- pure core only.  No SDL init, no display, no wall
 *      clock.  Produces the fixed UI description string for a fixed scenario.
 *      This is the "testable without a real display" path.
 *   2. `render <w> <h> <id>` -- builds a real SDL3 window + Nuklear context,
 *      emits one frame with the SAME labels/button states as the core, renders
 *      it, and drops every handle in reverse order.  Run with
 *      SDL_VIDEODRIVER=dummy so no physical display is required.
 *
 * `refuse <case>` drives the error* refusal cases: zero / negative / oversized
 * window dimensions and a null GUI context.  Each prints an explicit
 * `DASH_REFUSED ...` line.  All output is fixed; no time, randomness or real
 * input is involved.
 *
 * Build (from the repository root, after libnuklear.a exists in build/ext):
 *   ./build/c+ build examples/modern_gui/dashboard/dashboard.cp --strict \
 *     --sanitize=none -I. $(pkg-config --cflags --libs sdl3) \
 *     -Ithird_party/nuklear -Lbuild/ext -lnuklear -o <bin>
 * Run:
 *   SDL_VIDEODRIVER=dummy timeout 30 <bin> describe 0
 *   SDL_VIDEODRIVER=dummy timeout 30 <bin> render 640 480 0
 *   SDL_VIDEODRIVER=dummy timeout 30 <bin> refuse zero-width
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"

/* Reject absurd window requests before they ever reach SDL. */
#define DASH_MAX_DIM 4096
#define DASH_SCENARIOS 3
#define DASH_DESC_CAP 256

/* A fixed UI scenario: three labels and three buttons with fixed states.
 * These values are the single source of truth for BOTH the pure description
 * and the widgets emitted into the real Nuklear frame. */
typedef struct dash_scenario {
    int id;
    const char *title;
    const char *l0;
    const char *l1;
    const char *l2;
    const char *b0;
    const char *b1;
    const char *b2;
    int s0;
    int s1;
    int s2;
} dash_scenario;

/* ---- deterministic core (no SDL, no display, no clock) ------------------ */

static int dash_scenario_get(int id, dash_scenario *out, error *err) {
    error_reset(err);
    if (out == NULL) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "scenario: null output");
        return STDLIB_FAIL;
    }
    if (id < 0 || id >= DASH_SCENARIOS) {
        error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "scenario: id out of range");
        return STDLIB_FAIL;
    }
    out->id = id;
    if (id == 0) {
        out->title = "overview";
        out->l0 = "CPU";
        out->l1 = "MEM";
        out->l2 = "NET";
        out->b0 = "RUN";
        out->b1 = "STOP";
        out->b2 = "RESET";
        out->s0 = 1;
        out->s1 = 0;
        out->s2 = 1;
    } else if (id == 1) {
        out->title = "operations";
        out->l0 = "TASKS";
        out->l1 = "QUEUE";
        out->l2 = "ERRORS";
        out->b0 = "START";
        out->b1 = "PAUSE";
        out->b2 = "CLEAR";
        out->s0 = 0;
        out->s1 = 1;
        out->s2 = 0;
    } else {
        out->title = "diagnostics";
        out->l0 = "TEMP";
        out->l1 = "FAN";
        out->l2 = "PWR";
        out->b0 = "ACK";
        out->b1 = "MUTE";
        out->b2 = "RESET";
        out->s0 = 1;
        out->s1 = 1;
        out->s2 = 0;
    }
    return STDLIB_OK;
}

/* Fixed "UI description" string: label text plus button name=state pairs.
 * Pure function of the scenario value; identical on every machine and run. */
static void dash_describe(dash_scenario s, char *buf, int cap) {
    snprintf(buf, cap,
             "DASH_UI id=%d title=%s labels=%s,%s,%s buttons=%s:%s,%s:%s,%s:%s",
             s.id, s.title, s.l0, s.l1, s.l2, s.b0, s.s0 ? "ON" : "OFF",
             s.b1, s.s1 ? "ON" : "OFF", s.b2, s.s2 ? "ON" : "OFF");
}

/* ---- guarded inputs: every hostile value becomes a recoverable refusal --- */

static int dash_validate_dims(int width, int height, error *err) {
    error_reset(err);
    if (width <= 0 || height <= 0) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "dashboard: window dimensions must be positive");
        return STDLIB_FAIL;
    }
    if (width > DASH_MAX_DIM || height > DASH_MAX_DIM) {
        error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "dashboard: window dimensions exceed DASH_MAX_DIM");
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

static void dash_print_refusal(const char *tag, error *err) {
    print("DASH_REFUSED", tag, "category", err->category(), "code", err->code(),
          "message", err->what());
}

/* ---- real headless render ------------------------------------------------ */

static int dash_render(int width, int height, dash_scenario s, error *err) {
    int shown = 0;
    int widgets = 0;
    int rc = STDLIB_FAIL;

    if (dash_validate_dims(width, height, err) != STDLIB_OK)
        return STDLIB_FAIL;

    /* Each wrapper is created at its declaration and dropped in reverse order
     * on the way out: gui -> renderer -> window -> sdl. */
    {
        sdl core = sdl(SDL_INIT_VIDEO, err);
        if (core.valid()) {
            window win = window("dashboard", width, height, SDL_WINDOW_HIDDEN, err);
            if (win.valid()) {
                renderer draw = renderer(win.native(), "software", err);
                if (draw.valid()) {
                    gui ui = gui(win.native(), draw.native(), err);
                    if (ui.valid()) {
                        ui.input_begin();
                        ui.input_end();

                        shown = ui.window_begin(s.title, 8.0f, 8.0f,
                                                224.0f, 172.0f, 0);
                        if (shown) {
                            ui.layout_row_dynamic(24.0f, 1);
                            ui.label(s.l0);
                            widgets += 1;
                            ui.layout_row_dynamic(24.0f, 1);
                            ui.label(s.l1);
                            widgets += 1;
                            ui.layout_row_dynamic(24.0f, 1);
                            ui.label(s.l2);
                            widgets += 1;
                            ui.layout_row_dynamic(30.0f, 3);
                            (void)ui.button(s.b0);
                            (void)ui.button(s.b1);
                            (void)ui.button(s.b2);
                            widgets += 3;
                        }
                        ui.window_end();

                        if (draw.set_color(16, 16, 16, 255, err) == STDLIB_OK &&
                            draw.clear(err) == STDLIB_OK) {
                            ui.render(1);
                            if (draw.present(err) == STDLIB_OK) {
                                print("DASH_RENDER", width, height, s.id,
                                      "shown", shown, "widgets", widgets,
                                      "frame", 1);
                                rc = shown && widgets == 6 ? STDLIB_OK
                                                           : STDLIB_FAIL;
                            }
                        }
                        ui.drop();
                    }
                    draw.drop();
                }
                win.drop();
            }
            core.drop();
        }
    }
    return rc;
}

/* ---- small argv helpers (strict-mode-safe pointer reads) ---------------- */

static int dash_parse_int(const char *text, int *out) {
    char *end = NULL;
    long value;
    if (text == NULL || out == NULL)
        return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE)
        return -1;
    if (value < INT_MIN || value > INT_MAX)
        return -1;
    *out = (int)value;
    return 0;
}

static char *dash_argv_at(char **argv, int index) {
    char *value = NULL;
    __builtin_memcpy(&value, argv + index, sizeof(value));
    return value;
}

static void dash_tag(char *buf, int cap, const char *kind, int a, int b, int c) {
    snprintf(buf, cap, "%s:%d,%d,%d", kind, a, b, c);
}

/* ---- command handlers ---------------------------------------------------- */

static void cmd_describe(int argc, char **argv, error *err) {
    char tag[DASH_DESC_CAP];
    char desc[DASH_DESC_CAP];
    dash_scenario sc;
    int id = 0;
    int ok = argc >= 3 && dash_parse_int(dash_argv_at(argv, 2), &id) == 0 &&
             dash_scenario_get(id, &sc, err) == STDLIB_OK;
    dash_tag(tag, sizeof(tag), "describe", id, 0, 0);
    if (ok) {
        dash_describe(sc, desc, sizeof(desc));
        print(desc);
        print("DASH_RESULT", tag, "OK", 0);
    } else {
        if (err->failed_now() == 0)
            error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                           "describe: missing or invalid scenario id");
        dash_print_refusal(tag, err);
        print("DASH_RESULT", tag, "REFUSED", err->code());
    }
}

static void cmd_render(int argc, char **argv, error *err) {
    char tag[DASH_DESC_CAP];
    char desc[DASH_DESC_CAP];
    dash_scenario sc;
    int width = 0;
    int height = 0;
    int id = 0;
    int ok;

    if (argc >= 5) {
        ok = dash_parse_int(dash_argv_at(argv, 2), &width) == 0 &&
             dash_parse_int(dash_argv_at(argv, 3), &height) == 0 &&
             dash_parse_int(dash_argv_at(argv, 4), &id) == 0;
        if (ok)
            ok = dash_scenario_get(id, &sc, err) == STDLIB_OK;
        if (ok) {
            /* Emit the shared core description before the rendered frame so
             * the transcript reads description -> render -> result. */
            dash_describe(sc, desc, sizeof(desc));
            print(desc);
            ok = dash_render(width, height, sc, err) == STDLIB_OK;
        }
    } else {
        ok = 0;
    }
    dash_tag(tag, sizeof(tag), "render", width, height, id);
    if (ok) {
        print("DASH_RESULT", tag, "OK", 0);
    } else {
        if (err->failed_now() == 0)
            error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                           "render: missing or invalid arguments");
        dash_print_refusal(tag, err);
        print("DASH_RESULT", tag, "REFUSED", err->code());
    }
}

static void cmd_refuse(int argc, char **argv, error *err) {
    const char *name = argc >= 3 ? dash_argv_at(argv, 2) : "missing";
    dash_scenario sc;

    error_reset(err);
    if (strcmp(name, "zero-width") == 0) {
        (void)dash_validate_dims(0, 480, err);
    } else if (strcmp(name, "negative-height") == 0) {
        (void)dash_validate_dims(640, -10, err);
    } else if (strcmp(name, "oversized") == 0) {
        (void)dash_validate_dims(100000, 100000, err);
    } else if (strcmp(name, "bad-scenario") == 0) {
        (void)dash_scenario_get(99, &sc, err);
    } else if (strcmp(name, "null-context") == 0) {
        /* Construct a gui with NULL native handles: the wrapper must refuse
         * instead of dereferencing them.  No SDL init is needed for this. */
        gui bad = gui((SDL_Window *)0, (SDL_Renderer *)0, err);
        if (bad.valid()) {
            error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                           "refuse: null-context unexpectedly produced a context");
        }
        bad.drop();
    } else {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "refuse: unknown case");
    }

    dash_print_refusal(name, err);
    print("DASH_RESULT", "refuse", name, "REFUSED", err->code());
}

int main(int argc, char **argv) {
    error err = error();
    const char *cmd = "describe";

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    if (argc > 1)
        cmd = dash_argv_at(argv, 1);

    if (strcmp(cmd, "describe") == 0) {
        cmd_describe(argc, argv, &err);
    } else if (strcmp(cmd, "render") == 0) {
        cmd_render(argc, argv, &err);
    } else if (strcmp(cmd, "refuse") == 0) {
        cmd_refuse(argc, argv, &err);
    } else {
        error_fail_cat(&err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "dashboard: unknown command");
        dash_print_refusal("command", &err);
        print("DASH_RESULT", "command", "REFUSED", err.code());
    }

    /* Every invocation is a fixed transcript and exits 0: refusals are
     * expected, demonstrated outcomes, not unhandled failures. */
    return 0;
}
