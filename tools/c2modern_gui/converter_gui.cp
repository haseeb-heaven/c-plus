/* converter_gui.cp -- headless-testable Nuklear front-end for the C -> modern-C
 * conversion tool.
 *
 * This is a HEADLESS TEACHING GUI, not a production IDE.  It wraps a
 * conservative converter report.  The converter CANNOT guarantee semantics:
 * a textual C -> modern-C rewrite is never a proof of behaviour preservation.
 *
 * The real conversion CLI (tools/c2modern/convert.sh) is owned by a sibling
 * component and is not ready yet.  gui_convert() therefore MODELS the report
 * deterministically and marks status=PENDING, build=NOT_RUN, run=NOT_RUN.
 * Nothing here invokes cplus_system(): there is no subprocess, no clock and no
 * interactive requirement, so every run is byte-for-byte reproducible.  See
 * NOTES.md for the exact PENDING declaration and the commands used to verify.
 *
 * Two paths share ONE deterministic core (gui_convert + c2_describe):
 *   1. `describe <id>` / `convert <in> <out>` -- pure core only.  No SDL init.
 *   2. `render <w> <h> <id>` -- real SDL3 + Nuklear frame with the SAME values,
 *      run under SDL_VIDEODRIVER=dummy.
 *   `refuse <case>` drives the guarded refusals (empty / oversized / non-.c /
 *   null context / null paths / missing file / bad dimensions).
 *
 * Build (repo root, after build/ext/libnuklear.a exists):
 *   ./build/c+ build tools/c2modern_gui/converter_gui.cp --strict \
 *     --sanitize=none -I. $(pkg-config --cflags --libs sdl3) \
 *     -Ithird_party/nuklear -Lbuild/ext -lnuklear -o <bin>
 * Run (all under `timeout 30`):
 *   SDL_VIDEODRIVER=dummy <bin> describe 0
 *   SDL_VIDEODRIVER=dummy <bin> render 640 480 0
 *   SDL_VIDEODRIVER=dummy <bin> convert tools/c2modern_gui/fixtures/hello.c out.cpp
 *   SDL_VIDEODRIVER=dummy <bin> refuse empty
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
#include "stdlib/file.hp"

/* A path longer than 256 bytes is refused before any filesystem access. */
#define C2GUI_PATH_MAX 256
#define C2GUI_PATH_CAP (C2GUI_PATH_MAX + 1)
#define C2GUI_LINE_CAP 256
#define C2GUI_SOURCE_LINE_CAP 4096
#define C2GUI_DESC_CAP 640
#define C2GUI_MAX_DIM 4096
#define C2GUI_SCENARIOS 3
#define C2GUI_FRAME_W 640
#define C2GUI_FRAME_H 480

/* The deterministic, conservative modeled report. */
#define C2GUI_STATUS "PENDING"
#define C2GUI_BUILD "NOT_RUN"
#define C2GUI_RUN "NOT_RUN"

typedef struct c2_report {
    char input[C2GUI_PATH_CAP];
    char output[C2GUI_PATH_CAP];
    char status[16];
    char build[16];
    char run[16];
    int source_lines;
    int source_bytes;
} c2_report;

typedef struct c2_scenario {
    int id;
    const char *title;
    const char *input;
    const char *output;
} c2_scenario;

/* ---- guarded path / context validation ---------------------------------- */

/* Copy a checked string argument into a local, known-extent buffer.  All
 * subsequent inspection happens on that buffer, never on the unverified
 * pointer parameter (strict-mode safe). */
static int c2_local_copy(const char *src, char *dst, size_t dst_cap,
                         size_t *out_len) {
    size_t len;
    if (src == NULL || dst == NULL || dst_cap == 0)
        return -1;
    len = strlen(src);
    if (len >= dst_cap)
        return -1;
    __builtin_memcpy(dst, src, len);
    dst[len] = '\0';
    if (out_len != NULL)
        *out_len = len;
    return 0;
}

static int c2_validate_path(const char *path, int require_dot_c, error *err) {
    char local[C2GUI_PATH_CAP];
    size_t len = 0;
    error_reset(err);
    if (path == NULL) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER,
                       "c2gui: null path");
        return STDLIB_FAIL;
    }
    len = strlen(path);
    if (len == 0) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "c2gui: empty path");
        return STDLIB_FAIL;
    }
    if (len > C2GUI_PATH_MAX) {
        error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "c2gui: path exceeds 256 bytes");
        return STDLIB_FAIL;
    }
    if (c2_local_copy(path, local, sizeof(local), &len) != 0) {
        error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "c2gui: path exceeds 256 bytes");
        return STDLIB_FAIL;
    }
    if (require_dot_c &&
        !(len >= 2 && local[len - 2] == '.' && local[len - 1] == 'c')) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "c2gui: input must be a .c file");
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

/* A NULL Nuklear/gui context is refused before any widget or conversion call. */
static int c2_validate_context(struct nk_context *ctx, error *err) {
    error_reset(err);
    if (ctx == NULL) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER,
                       "c2gui: null GUI context");
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

/* ---- deterministic core: source metrics via stdlib/file.hp -------------- */

/* Count lines and the sum of their (newline-stripped) lengths.  Fixed input
 * bytes => fixed metrics; no clock, randomness or interactive state. */
static int c2_read_source_metrics(const char *path, int *lines, int *bytes,
                                  error *err) {
    char line[C2GUI_LINE_CAP];
    int nlines = 0;
    int nbytes = 0;
    file src = file(path, "r");
    error_reset(err);
    if (!src.is_open()) {
        error_fail_cat(err, STDLIB_CATEGORY_IO, CPLUS_EXIT_IO,
                       "c2gui: cannot open input file");
        return STDLIB_FAIL;
    }
    for (;;) {
        int rc = src.read_line(line, (int)sizeof(line), err);
        if (rc == STDLIB_EOF)
            break;
        if (rc != STDLIB_OK) {
            src.drop();
            return STDLIB_FAIL;
        }
        nlines++;
        nbytes += (int)strlen(line);
        if (nlines >= C2GUI_SOURCE_LINE_CAP)
            break;
    }
    src.drop();
    *lines = nlines;
    *bytes = nbytes;
    return STDLIB_OK;
}

/* ---- the deterministic conversion core ---------------------------------- */

/* gui_convert(input_path, output_path) -- pure, reproducible model of the
 * converter's report.  It is NOT a real conversion: status is PENDING because
 * the CLI converter is not ready.  All guard rails live here so describe,
 * render and convert share identical validation and results. */
static int gui_convert(const char *input_path, const char *output_path,
                       c2_report *out, error *err) {
    error_reset(err);
    if (out == NULL || err == NULL) {
        if (err != NULL)
            error_fail_cat(err, STDLIB_CATEGORY_INVALID,
                           CPLUS_EXIT_NULL_POINTER, "c2gui: null report or error");
        return STDLIB_FAIL;
    }
    if (c2_validate_path(input_path, 1, err) != STDLIB_OK)
        return STDLIB_FAIL;
    if (c2_validate_path(output_path, 0, err) != STDLIB_OK)
        return STDLIB_FAIL;

    (void)c2_local_copy(input_path, out->input, sizeof(out->input), NULL);
    (void)c2_local_copy(output_path, out->output, sizeof(out->output), NULL);
    (void)c2_local_copy(C2GUI_STATUS, out->status, sizeof(out->status), NULL);
    (void)c2_local_copy(C2GUI_BUILD, out->build, sizeof(out->build), NULL);
    (void)c2_local_copy(C2GUI_RUN, out->run, sizeof(out->run), NULL);
    out->source_lines = 0;
    out->source_bytes = 0;

    return c2_read_source_metrics(input_path, &out->source_lines,
                                  &out->source_bytes, err);
}

/* gui_convert plus an explicit Nuklear-context guard; used by the null-context
 * refusal so the guard is reachable without a working SDL backend. */
static int gui_convert_context(struct nk_context *ctx, const char *input_path,
                               const char *output_path, c2_report *out,
                               error *err) {
    if (c2_validate_context(ctx, err) != STDLIB_OK)
        return STDLIB_FAIL;
    return gui_convert(input_path, output_path, out, err);
}

/* ---- fixed scenarios and deterministic transcripts ---------------------- */

static int c2_scenario_get(int id, c2_scenario *out, error *err) {
    error_reset(err);
    if (out == NULL) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "c2gui: null scenario output");
        return STDLIB_FAIL;
    }
    if (id < 0 || id >= C2GUI_SCENARIOS) {
        error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "c2gui: scenario id out of range");
        return STDLIB_FAIL;
    }
    out->id = id;
    if (id == 0) {
        out->title = "convert-hello";
        out->input = "tools/c2modern_gui/fixtures/hello.c";
        out->output = "tools/c2modern_gui/results/hello.cpp";
    } else if (id == 1) {
        out->title = "convert-util";
        out->input = "tools/c2modern_gui/fixtures/util.c";
        out->output = "tools/c2modern_gui/results/util.cpp";
    } else {
        out->title = "convert-main";
        out->input = "tools/c2modern_gui/fixtures/main.c";
        out->output = "tools/c2modern_gui/results/main.cpp";
    }
    return STDLIB_OK;
}

/* One fixed transcript line: widgets, field values and the conversion result.
 * Pure function of (scenario, report); identical on every machine and run. */
static void c2_describe(const c2_scenario *s, const c2_report *r, char *buf,
                        int cap) {
    snprintf(buf, cap,
             "C2GUI_UI id=%d title=%s "
             "widgets=input:field,convert:button,output:field,status:label,build:label,run:label "
             "input=%s output=%s status=%s build=%s run=%s "
             "source_lines=%d source_bytes=%d",
             s->id, s->title, r->input, r->output, r->status, r->build, r->run,
             r->source_lines, r->source_bytes);
}

static void c2_report_line(const c2_report *r, char *buf, int cap) {
    snprintf(buf, cap,
             "C2GUI_REPORT input=%s output=%s status=%s build=%s run=%s "
             "source_lines=%d source_bytes=%d",
             r->input, r->output, r->status, r->build, r->run,
             r->source_lines, r->source_bytes);
}

/* ---- guarded dimensions / refusals -------------------------------------- */

static int c2_validate_dims(int width, int height, error *err) {
    error_reset(err);
    if (width <= 0 || height <= 0) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "c2gui: window dimensions must be positive");
        return STDLIB_FAIL;
    }
    if (width > C2GUI_MAX_DIM || height > C2GUI_MAX_DIM) {
        error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "c2gui: window dimensions exceed 4096");
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

static void c2_print_refusal(const char *tag, error *err) {
    print("C2GUI_REFUSED", tag, "category", err->category(), "code",
          err->code(), "message", err->what());
}

/* ---- real headless SDL3 + Nuklear frame ---------------------------------- */

static int c2_render(int width, int height, const c2_scenario *s,
                     c2_report *r, error *err) {
    char input_field[C2GUI_PATH_CAP];
    char output_field[C2GUI_PATH_CAP];
    struct nk_context *ctx;
    int shown = 0;
    int widgets = 0;
    int rc = STDLIB_FAIL;

    if (c2_validate_dims(width, height, err) != STDLIB_OK)
        return STDLIB_FAIL;
    if (c2_local_copy(r->input, input_field, sizeof(input_field), NULL) != 0 ||
        c2_local_copy(r->output, output_field, sizeof(output_field), NULL) != 0) {
        error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "c2gui: report path does not fit edit field");
        return STDLIB_FAIL;
    }

    /* Dropped in reverse order on the way out: gui -> renderer -> window -> sdl. */
    {
        sdl core = sdl(SDL_INIT_VIDEO, err);
        if (core.valid()) {
            window win = window("C to modern C converter", width, height,
                                SDL_WINDOW_HIDDEN, err);
            if (win.valid()) {
                renderer draw = renderer(win.native(), "software", err);
                if (draw.valid()) {
                    gui ui = gui(win.native(), draw.native(), err);
                    if (ui.valid()) {
                        ctx = ui.native();
                        if (c2_validate_context(ctx, err) == STDLIB_OK) {
                            ui.input_begin();
                            ui.input_end();

                            shown = ui.window_begin(s->title, 8.0f, 8.0f,
                                                    336.0f, 300.0f, 0);
                            if (shown) {
                                ui.layout_row_dynamic(20.0f, 1);
                                ui.label("Input .c path");
                                widgets++;
                                ui.layout_row_dynamic(26.0f, 1);
                                nk_edit_string_zero_terminated(
                                    ctx, NK_EDIT_FIELD, input_field,
                                    (int)sizeof(input_field), nk_filter_default);
                                widgets++;
                                ui.layout_row_dynamic(30.0f, 1);
                                (void)ui.button("Convert");
                                widgets++;
                                ui.layout_row_dynamic(20.0f, 1);
                                ui.label("Output path");
                                widgets++;
                                ui.layout_row_dynamic(26.0f, 1);
                                nk_edit_string_zero_terminated(
                                    ctx, NK_EDIT_FIELD, output_field,
                                    (int)sizeof(output_field), nk_filter_default);
                                widgets++;
                                ui.layout_row_dynamic(20.0f, 1);
                                ui.label("Conversion status: PENDING (modeled; CLI converter not ready)");
                                widgets++;
                                ui.layout_row_dynamic(20.0f, 1);
                                ui.label("Build result: NOT_RUN");
                                widgets++;
                                ui.layout_row_dynamic(20.0f, 1);
                                ui.label("Run result: NOT_RUN");
                                widgets++;
                            }
                            ui.window_end();

                            if (draw.set_color(16, 16, 16, 255, err) == STDLIB_OK &&
                                draw.clear(err) == STDLIB_OK) {
                                ui.render(1);
                                if (draw.present(err) == STDLIB_OK) {
                                    print("C2GUI_RENDER", width, height, s->id,
                                          "shown", shown, "widgets", widgets,
                                          "frame", 1);
                                    rc = shown && widgets == 8 ? STDLIB_OK
                                                               : STDLIB_FAIL;
                                }
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

static int c2_parse_int(const char *text, int *out) {
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

static char *c2_argv_at(char **argv, int index) {
    char *value = NULL;
    __builtin_memcpy(&value, argv + index, sizeof(value));
    return value;
}

/* ---- command handlers ---------------------------------------------------- */

static void cmd_describe(int argc, char **argv, error *err) {
    char tag[C2GUI_DESC_CAP];
    char desc[C2GUI_DESC_CAP];
    c2_scenario sc;
    c2_report rep;
    int id = 0;
    int ok = argc >= 3 && c2_parse_int(c2_argv_at(argv, 2), &id) == 0 &&
             c2_scenario_get(id, &sc, err) == STDLIB_OK &&
             gui_convert(sc.input, sc.output, &rep, err) == STDLIB_OK;
    snprintf(tag, sizeof(tag), "describe:%d", id);
    if (ok) {
        c2_describe(&sc, &rep, desc, sizeof(desc));
        print(desc);
        print("C2GUI_RESULT", tag, "OK", 0);
    } else {
        if (err->failed_now() == 0)
            error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                           "describe: missing or invalid scenario id");
        c2_print_refusal(tag, err);
        print("C2GUI_RESULT", tag, "REFUSED", err->code());
    }
}

static void cmd_convert(int argc, char **argv, error *err) {
    char tag[C2GUI_DESC_CAP];
    char line[C2GUI_DESC_CAP];
    c2_report rep;
    const char *in = argc >= 3 ? c2_argv_at(argv, 2) : NULL;
    const char *outp = argc >= 4 ? c2_argv_at(argv, 3) : NULL;
    int ok = argc >= 4 && gui_convert(in, outp, &rep, err) == STDLIB_OK;
    snprintf(tag, sizeof(tag), "convert");
    if (ok) {
        c2_report_line(&rep, line, sizeof(line));
        print(line);
        print("C2GUI_RESULT", tag, "OK", 0);
    } else {
        if (err->failed_now() == 0)
            error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                           "convert: missing input or output path");
        c2_print_refusal(tag, err);
        print("C2GUI_RESULT", tag, "REFUSED", err->code());
    }
}

static void cmd_render(int argc, char **argv, error *err) {
    char tag[C2GUI_DESC_CAP];
    char desc[C2GUI_DESC_CAP];
    c2_scenario sc;
    c2_report rep;
    int width = 0;
    int height = 0;
    int id = 0;
    int ok = 0;

    if (argc >= 5) {
        ok = c2_parse_int(c2_argv_at(argv, 2), &width) == 0 &&
             c2_parse_int(c2_argv_at(argv, 3), &height) == 0 &&
             c2_parse_int(c2_argv_at(argv, 4), &id) == 0;
        if (ok)
            ok = c2_scenario_get(id, &sc, err) == STDLIB_OK;
        if (ok)
            ok = gui_convert(sc.input, sc.output, &rep, err) == STDLIB_OK;
        if (ok) {
            c2_describe(&sc, &rep, desc, sizeof(desc));
            print(desc);
            ok = c2_render(width, height, &sc, &rep, err) == STDLIB_OK;
        }
    }
    snprintf(tag, sizeof(tag), "render:%d,%d,%d", width, height, id);
    if (ok) {
        print("C2GUI_RESULT", tag, "OK", 0);
    } else {
        if (err->failed_now() == 0)
            error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                           "render: missing or invalid arguments");
        c2_print_refusal(tag, err);
        print("C2GUI_RESULT", tag, "REFUSED", err->code());
    }
}

static void cmd_refuse(int argc, char **argv, error *err) {
    const char *name = argc >= 3 ? c2_argv_at(argv, 2) : "missing";
    c2_report rep;
    char oversized[C2GUI_PATH_CAP + 8];
    c2_scenario sc;

    error_reset(err);
    if (strcmp(name, "empty-input") == 0) {
        (void)gui_convert("", "out.cpp", &rep, err);
    } else if (strcmp(name, "empty-output") == 0) {
        (void)gui_convert("tools/c2modern_gui/fixtures/hello.c", "", &rep, err);
    } else if (strcmp(name, "oversized") == 0) {
        size_t i;
        for (i = 0; i < C2GUI_PATH_MAX + 1; i++)
            oversized[i] = 'a';
        oversized[C2GUI_PATH_MAX + 1] = '.';
        oversized[C2GUI_PATH_MAX + 2] = 'c';
        oversized[C2GUI_PATH_MAX + 3] = '\0';
        (void)gui_convert(oversized, "out.cpp", &rep, err);
    } else if (strcmp(name, "non-c") == 0) {
        (void)gui_convert("notes.txt", "out.cpp", &rep, err);
    } else if (strcmp(name, "null-input") == 0) {
        (void)gui_convert((const char *)0, "out.cpp", &rep, err);
    } else if (strcmp(name, "null-output") == 0) {
        (void)gui_convert("tools/c2modern_gui/fixtures/hello.c",
                          (const char *)0, &rep, err);
    } else if (strcmp(name, "null-context") == 0) {
        /* Guard reachable without SDL: NULL Nuklear context is refused before
         * any widget or conversion call.  First prove the gui wrapper itself
         * refuses NULL native handles instead of dereferencing them, then let
         * the explicit context guard record the final, reported error. */
        {
            gui bad = gui((SDL_Window *)0, (SDL_Renderer *)0, err);
            if (bad.valid()) {
                error_fail_cat(err, STDLIB_CATEGORY_INVALID,
                               CPLUS_EXIT_UNSUPPORTED,
                               "c2gui: null-context built a live gui");
            }
            bad.drop();
        }
        (void)gui_convert_context((struct nk_context *)0,
                                  "tools/c2modern_gui/fixtures/hello.c",
                                  "out.cpp", &rep, err);
    } else if (strcmp(name, "missing-file") == 0) {
        (void)gui_convert("tools/c2modern_gui/fixtures/no_such_file.c",
                          "out.cpp", &rep, err);
    } else if (strcmp(name, "zero-width") == 0) {
        (void)c2_validate_dims(0, C2GUI_FRAME_H, err);
    } else if (strcmp(name, "oversized-dims") == 0) {
        (void)c2_validate_dims(100000, 100000, err);
    } else if (strcmp(name, "bad-scenario") == 0) {
        (void)c2_scenario_get(99, &sc, err);
    } else {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "c2gui: unknown refusal case");
    }

    c2_print_refusal(name, err);
    print("C2GUI_RESULT", "refuse", name, "REFUSED", err->code());
}

int main(int argc, char **argv) {
    error err = error();
    const char *cmd = "describe";

    /* Keep the transcript deterministic: no optional diagnostics callback. */
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_alloc_fail_reset();

    if (argc > 1)
        cmd = c2_argv_at(argv, 1);

    if (strcmp(cmd, "describe") == 0) {
        cmd_describe(argc, argv, &err);
    } else if (strcmp(cmd, "convert") == 0) {
        cmd_convert(argc, argv, &err);
    } else if (strcmp(cmd, "render") == 0) {
        cmd_render(argc, argv, &err);
    } else if (strcmp(cmd, "refuse") == 0) {
        cmd_refuse(argc, argv, &err);
    } else {
        error_fail_cat(&err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                       "c2gui: unknown command");
        c2_print_refusal("command", &err);
        print("C2GUI_RESULT", "command", "REFUSED", err.code());
    }

    /* Every invocation is a fixed transcript and exits 0: refusals are
     * expected, demonstrated outcomes, not unhandled failures. */
    return 0;
}
