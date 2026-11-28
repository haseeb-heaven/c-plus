/*
 * Regression tests for parse_word() in stdlib/safe/src/safe_diag.c.
 *
 * The old implementation used the leading-whitespace-skip index of `value` to
 * index `word` as well.  With e.g. CPLUS_DIAG="     rich" that read word[5]
 * past the 5-byte .rodata literal "rich" and never matched a padded value.
 * These cases drive the public env API so they fail on the old code both by
 * assertion and (under ASan) by a global-buffer-overflow report.
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_diag.h"

static int g_fail = 0;

#define CHECK(cond, label)                                                    \
    do {                                                                      \
        if (!(cond)) {                                                        \
            printf("FAIL %s\n", label);                                       \
            ++g_fail;                                                         \
        } else {                                                              \
            printf("PASS %s\n", label);                                       \
        }                                                                     \
    } while (0)

static void put(const char *name, const char *value) {
    if (value != NULL)
        (void)setenv(name, value, 1);
    else
        (void)unsetenv(name);
}

/* Reload configuration from a controlled environment on every call. */
static void load(const char *diag, const char *color, const char *no_color) {
    put("CPLUS_DIAG", diag);
    put("CPLUS_COLOR", color);
    put("NO_COLOR", no_color);
    cplus_diag_init_from_env();
}

int main(void) {
    /* Short literals with more leading whitespace than the word length. */
    load("     rich", NULL, NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_RICH, "diag_5_spaces_rich");

    load("                                rich", NULL, NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_RICH, "diag_many_spaces_rich");

    load("\t\trich \t", NULL, NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_RICH, "diag_tab_pad_rich");

    load("  rich  ", NULL, NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_RICH, "diag_pad_rich");

    /* "plain" is exactly 5 bytes: the old code compared from word[2] onward. */
    load("   plain   ", NULL, NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_PLAIN, "diag_pad_plain");

    load("  legacy  ", NULL, NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_LEGACY, "diag_pad_legacy");

    /* A padded unknown value must leave a programmatic mode untouched. */
    cplus_diag_set_mode(CPLUS_DIAG_RICH);
    load("     bogus", NULL, NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_RICH, "diag_pad_unknown_keeps_mode");

    /* Trailing garbage is still rejected (the trim is whitespace-only). */
    cplus_diag_set_mode(CPLUS_DIAG_LEGACY);
    load("  rich junk", NULL, NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_LEGACY, "diag_pad_trailing_garbage");

    /* The same bug affected CPLUS_COLOR with short values ("auto", "never"). */
    load(NULL, "     never", NULL);
    CHECK(cplus_diag_get_color() == CPLUS_DIAG_COLOR_NEVER, "color_5_spaces_never");

    cplus_diag_set_color(CPLUS_DIAG_COLOR_NEVER);
    load(NULL, "  auto  ", NULL);
    CHECK(cplus_diag_get_color() == CPLUS_DIAG_COLOR_AUTO, "color_pad_auto");

    load(NULL, "   always   ", NULL);
    CHECK(cplus_diag_get_color() == CPLUS_DIAG_COLOR_ALWAYS, "color_pad_always");

    load(NULL, "\tnever \t", NULL);
    CHECK(cplus_diag_get_color() == CPLUS_DIAG_COLOR_NEVER, "color_tab_pad_never");

    /* Effective colour decision with padded env values. */
    load("     rich", "     always", NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_RICH &&
              cplus_diag_get_color() == CPLUS_DIAG_COLOR_ALWAYS &&
              cplus_diag_color_enabled(stderr) == 1,
          "padded_rich_always_enabled");

    load("  rich", "  never", NULL);
    CHECK(cplus_diag_color_enabled(stderr) == 0, "padded_rich_never_disabled");

    /* NO_COLOR still overrides a padded CPLUS_COLOR=always. */
    load("  plain", "   always  ", "1");
    CHECK(cplus_diag_get_color() == CPLUS_DIAG_COLOR_NEVER &&
              cplus_diag_color_enabled(stderr) == 0,
          "padded_always_no_color");

    /* Empty env keeps the documented defaults. */
    load("", "", NULL);
    CHECK(cplus_diag_get_mode() == CPLUS_DIAG_LEGACY &&
              cplus_diag_get_color() == CPLUS_DIAG_COLOR_AUTO,
          "empty_env_defaults");

    put("CPLUS_DIAG", NULL);
    put("CPLUS_COLOR", NULL);
    put("NO_COLOR", NULL);
    printf("parse_word regressions: %d failure(s)\n", g_fail);
    return g_fail == 0 ? 0 : 1;
}
