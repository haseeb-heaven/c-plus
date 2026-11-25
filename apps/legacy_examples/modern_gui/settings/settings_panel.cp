/* Modern GUI teaching app: a headless Nuklear settings panel with refusals.
 *
 * The panel is real Nuklear + the official SDL3 renderer backend.  It is run on
 * SDL_VIDEODRIVER=dummy with a hidden software-rendered window, so the only
 * observable result is a deterministic transcript.  There is no interactive
 * input requirement: `argv[1..]` select fixed scenarios from the table below.
 *
 * Every input is validated through the checked stdlib/error.hp APIs and a
 * checked stdlib/string.hp copy before any state is written.  Invalid input is
 * REFUSED with a stable category + cplus_exit_code; the state is left at its
 * sentinel.  The check boxes and the text edit use the borrowed Nuklear context
 * from gui::native(); the slider uses gui::slider().
 *
 * Scope: this is a headless teaching app and runtime refusal EVIDENCE, not a
 * proof of GUI correctness, memory safety, or freedom from all invalid states.
 *
 * Build (from repo root, after build/ext/libnuklear.a exists):
 *   ./build/c+ build apps/legacy_examples/modern_gui/settings/settings_panel.cp \
 *     --strict --sanitize=none -I. $(pkg-config --cflags sdl3) \
 *     -Ithird_party/nuklear -Lbuild/ext -lnuklear $(pkg-config --libs sdl3) \
 *     -o apps/legacy_examples/modern_gui/settings/settings_panel
 */
#include <stdio.h>
#include <string.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/window.hp"
#include "stdlib/graphics.hp"
#include "stdlib/gui.hp"
#include "stdlib/string.hp"

#define SETTINGS_TEXT_CAP 32      /* includes the terminating NUL */
#define SETTINGS_TEXT_MAX (SETTINGS_TEXT_CAP - 1)
#define SETTINGS_SLIDER_MIN 0
#define SETTINGS_SLIDER_MAX 100
#define SETTINGS_FLAGS_MAX 15u
#define SETTINGS_FRAMES 2

struct settings_state {
    char text[SETTINGS_TEXT_CAP];
    int slider;
    unsigned int flags;
};

struct render_info {
    int windows;
    int width;
    int height;
};

struct settings_case {
    const char *name;
    const char *text;
    int slider;
    unsigned int flags;
    int expect;       /* 0 = APPLIED, 1 = REFUSED */
    int expect_code;  /* cplus_exit_code when refused */
    int expect_cat;   /* STDLIB_CATEGORY_* when refused */
};

/* Fixed scenarios.  The oversized text is 40 bytes; the field holds 31 + NUL.
 * The slider case uses 101, one past the inclusive maximum of 100. */
static const struct settings_case cases[] = {
    {"apply",         "guest",                                  42,  3u, 0, CPLUS_EXIT_OK,          STDLIB_CATEGORY_OK},
    {"text_oversize", "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA", 42, 3u, 1, CPLUS_EXIT_BOUNDS,     STDLIB_CATEGORY_BOUNDS},
    {"slider_range",  "guest",                                 101,  3u, 1, CPLUS_EXIT_BOUNDS,     STDLIB_CATEGORY_RANGE},
    {"null_context",  "guest",                                  42,  3u, 1, CPLUS_EXIT_NULL_POINTER, STDLIB_CATEGORY_INVALID},
    {"null_text",     (const char *)0,                           42,  3u, 1, CPLUS_EXIT_NULL_POINTER, STDLIB_CATEGORY_INVALID},
    {"invalid_utf8",  "\xC0\x80",                                42,  3u, 1, CPLUS_EXIT_UNSUPPORTED, STDLIB_CATEGORY_UTF8},
};
#define SETTINGS_CASE_COUNT ((int)(sizeof(cases) / sizeof(cases[0])))

static void state_reset(struct settings_state *s) {
    char unset[6];
    unset[0] = 'U';
    unset[1] = 'N';
    unset[2] = 'S';
    unset[3] = 'E';
    unset[4] = 'T';
    unset[5] = '\0';
    memset(s->text, 0, sizeof(s->text));
    memcpy(s->text, unset, 6);
    s->slider = -7;
    s->flags = 0u;
}

static int state_is_sentinel(const struct settings_state *s) {
    return strcmp(s->text, "UNSET") == 0 && s->slider == -7 && s->flags == 0u;
}

static int find_case(const char *name, int *index) {
    int i;
    if (name == NULL || index == NULL)
        return 0;
    for (i = 0; i < SETTINGS_CASE_COUNT; i++) {
        if (strcmp(cases[i].name, name) == 0) {
            *index = i;
            return 1;
        }
    }
    return 0;
}

/* Checked, bounded text validation: NULL, length, UTF-8, allocation and a
 * failure-atomic stdlib/string.hp copy are all reported through `error`. */
static int validate_text(const char *text, char *out, size_t out_size, error *err) {
    size_t len;
    error_reset(err);
    if (text == NULL || out == NULL || out_size == 0) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER,
                       "settings: null text or output buffer");
        return STDLIB_FAIL;
    }
    len = strlen(text);
    if (len >= out_size) {
        error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                       "settings: text exceeds field capacity");
        return STDLIB_FAIL;
    }
    {
        string_view view = string_view(text, len);
        if (!view.is_valid_utf8()) {
            error_fail_cat(err, STDLIB_CATEGORY_UTF8, CPLUS_EXIT_UNSUPPORTED,
                           "settings: text is not well-formed UTF-8");
            return STDLIB_FAIL;
        }
    }
    {
        string copy = string(text);
        if (!copy.valid()) {
            error_fail_cat(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                           "settings: checked string allocation failed");
            return STDLIB_FAIL;
        }
        if (copy.substr(0, len, out, out_size) != 0) {
            error_fail_cat(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                           "settings: bounded text copy failed");
            copy.drop();
            return STDLIB_FAIL;
        }
        copy.drop();
    }
    return STDLIB_OK;
}

static int validate_slider(int value, error *err) {
    error_reset(err);
    if (value < SETTINGS_SLIDER_MIN || value > SETTINGS_SLIDER_MAX) {
        error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "settings: slider value outside widget range");
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

static int validate_flags(unsigned int flags, error *err) {
    error_reset(err);
    if (flags > SETTINGS_FLAGS_MAX) {
        error_fail_cat(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                       "settings: flag mask outside supported bits");
        return STDLIB_FAIL;
    }
    return STDLIB_OK;
}

/* Validate everything, THEN commit to state.  A NULL gui is refused before any
 * Nuklear call or state write; a refused case never touches `state`. */
static int apply_settings(gui *ui, struct settings_state *state,
                          const char *text, int slider, unsigned int flags,
                          error *err) {
    char checked[SETTINGS_TEXT_CAP];
    error_reset(err);
    if (ui == NULL || state == NULL || err == NULL) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER,
                       "settings: null gui, state or error object");
        return STDLIB_FAIL;
    }
    if (!ui->valid()) {
        error_fail_cat(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_NULL_POINTER,
                       "settings: gui context is not valid");
        return STDLIB_FAIL;
    }
    if (validate_text(text, checked, sizeof(checked), err) != STDLIB_OK)
        return STDLIB_FAIL;
    if (validate_slider(slider, err) != STDLIB_OK)
        return STDLIB_FAIL;
    if (validate_flags(flags, err) != STDLIB_OK)
        return STDLIB_FAIL;
    memcpy(state->text, checked, sizeof(state->text));
    state->slider = slider;
    state->flags = flags;
    return STDLIB_OK;
}

/* Two real Nuklear frames: text edit, wrapped slider, two flag check boxes and
 * an Apply button.  No pointer events are injected, so the transcript is fixed. */
static int render_panel(gui *ui, renderer *draw, struct settings_state *state,
                        error *err) {
    struct nk_context *ctx;
    int frame;
    int windows = 0;
    if (ui == NULL || draw == NULL || state == NULL)
        return 0;
    ctx = ui->native();
    if (ctx == NULL)
        return 0;
    for (frame = 0; frame < SETTINGS_FRAMES; frame++) {
        ui->input_begin();
        ui->input_end();
        if (ui->window_begin("settings", 8.0f, 8.0f, 288.0f, 208.0f, 0)) {
            windows++;
            ui->layout_row_dynamic(22.0f, 1);
            ui->label("Profile name");
            ui->layout_row_dynamic(26.0f, 1);
            nk_edit_string_zero_terminated(ctx, NK_EDIT_FIELD, state->text,
                                           (int)sizeof(state->text),
                                           nk_filter_default);
            ui->layout_row_dynamic(22.0f, 1);
            ui->label("Volume");
            ui->layout_row_dynamic(26.0f, 1);
            ui->slider(&state->slider, SETTINGS_SLIDER_MIN, SETTINGS_SLIDER_MAX);
            ui->layout_row_dynamic(22.0f, 1);
            ui->label("Flags");
            ui->layout_row_dynamic(26.0f, 2);
            nk_checkbox_flags_label(ctx, "audio", &state->flags, 1u << 0);
            nk_checkbox_flags_label(ctx, "network", &state->flags, 1u << 1);
            ui->layout_row_dynamic(30.0f, 1);
            (void)ui->button("Apply");
        }
        ui->window_end();
        if (draw->set_color(24, 24, 24, 255, err) != STDLIB_OK ||
            draw->clear(err) != STDLIB_OK)
            return windows;
        ui->render(1);
        if (draw->present(err) != STDLIB_OK)
            return windows;
    }
    return windows;
}

static int run_case(const struct settings_case *c, struct settings_state *state,
                    struct render_info *render, error *err) {
    int applied;
    render->windows = 0;
    render->width = 0;
    render->height = 0;
    /* The null-context guard must be reachable even without a working SDL
     * backend, so exercise it before creating any Nuklear context. */
    if (strcmp(c->name, "null_context") == 0)
        return apply_settings((gui *)0, state, c->text, c->slider, c->flags, err);

    sdl core = sdl(SDL_INIT_VIDEO, err);
    if (!core.valid())
        return STDLIB_FAIL;
    window win = window("settings panel", 320, 240, SDL_WINDOW_HIDDEN, err);
    if (!win.valid()) {
        core.drop();
        return STDLIB_FAIL;
    }
    renderer draw = renderer(win.native(), "software", err);
    if (!draw.valid()) {
        win.drop();
        core.drop();
        return STDLIB_FAIL;
    }
    gui ui = gui(win.native(), draw.native(), err);
    if (!ui.valid()) {
        draw.drop();
        win.drop();
        core.drop();
        return STDLIB_FAIL;
    }
    applied = apply_settings(&ui, state, c->text, c->slider, c->flags, err);
    if (applied == STDLIB_OK) {
        render->windows = render_panel(&ui, &draw, state, err);
        if (render->windows != SETTINGS_FRAMES || err->failed_now() != 0) {
            error_fail_cat(err, STDLIB_CATEGORY_IO, CPLUS_EXIT_IO,
                           "settings: headless render did not complete");
            applied = STDLIB_FAIL;
        } else {
            surface shot = draw.read(err);
            render->width = shot.width();
            render->height = shot.height();
            if (!shot.valid() || render->width <= 0 || render->height <= 0) {
                error_fail_cat(err, STDLIB_CATEGORY_IO, CPLUS_EXIT_IO,
                               "settings: headless readback failed");
                applied = STDLIB_FAIL;
            }
            shot.drop();
        }
    }
    ui.drop();
    draw.drop();
    win.drop();
    core.drop();
    return applied;
}

static void print_detail(const struct settings_case *c, char *out, size_t cap) {
    if (strcmp(c->name, "null_context") == 0)
        snprintf(out, cap, "field=context");
    else if (strcmp(c->name, "slider_range") == 0)
        snprintf(out, cap, "field=slider value=%d min=%d max=%d", c->slider,
                 SETTINGS_SLIDER_MIN, SETTINGS_SLIDER_MAX);
    else if (c->text == NULL)
        snprintf(out, cap, "field=text is_null=1");
    else
        snprintf(out, cap, "field=text len=%zu max=%d", strlen(c->text),
                 SETTINGS_TEXT_MAX);
}

int main(int argc, char **argv) {
    error err = error();
    struct settings_state state;
    struct render_info render;
    int i;
    int failures = 0;

    /* Keep the transcript deterministic: no optional diagnostics callback. */
    stdlib_set_error_callback((stdlib_error_callback)0);

    if (argc < 2) {
        printf("SETTINGS usage <scenario>...\n");
        printf("SETTINGS scenarios apply text_oversize slider_range null_context null_text invalid_utf8\n");
        return 2;
    }

    for (i = 1; i < argc; i++) {
        int index = 0;
        const struct settings_case *c;
        int applied;
        int passed;
        char detail[96];

        if (!find_case(argv[i], &index)) {
            printf("SETTINGS %s FAIL code=%d cat=%s unknown_scenario\n", argv[i],
                   CPLUS_EXIT_UNSUPPORTED, "invalid");
            failures++;
            continue;
        }
        c = &cases[index];
        state_reset(&state);
        applied = run_case(c, &state, &render, &err);
        print_detail(c, detail, sizeof(detail));

        if (c->expect == 0) {
            passed = (applied == STDLIB_OK && err.failed_now() == 0 &&
                      err.code() == c->expect_code &&
                      err.category() == c->expect_cat &&
                      !state_is_sentinel(&state));
            printf("SETTINGS %s %s code=%d cat=%s %s text=%s slider=%d flags=%u "
                   "frames=%d windows=%d surface=%dx%d\n",
                   c->name, passed ? "APPLIED" : "FAIL", err.code(),
                   stdlib_category_name(err.category()), detail, state.text,
                   state.slider, state.flags, SETTINGS_FRAMES, render.windows,
                   render.width, render.height);
        } else {
            passed = (applied == STDLIB_FAIL && err.failed_now() == 1 &&
                      err.code() == c->expect_code &&
                      err.category() == c->expect_cat &&
                      state_is_sentinel(&state));
            printf("SETTINGS %s %s code=%d cat=%s %s\n", c->name,
                   passed ? "REFUSED" : "FAIL", err.code(),
                   stdlib_category_name(err.category()), detail);
        }
        if (!passed)
            failures++;
        fflush(stdout);
    }
    return failures == 0 ? 0 : 1;
}
