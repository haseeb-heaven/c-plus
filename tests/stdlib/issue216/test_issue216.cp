/* Issue #216 regression: negative widget IDs/coordinates must be counted in
 * the serialized size (the '-' sign is a byte too), and the direct label/newline
 * writes in stdlib_window_serialize() must stay inside the caller's buffer.
 *
 * Headless, deterministic C+ program. Every check prints only on failure and
 * the last line is "ISSUE216 <checks> <failures>".
 */
#include <stdio.h>
#include <string.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/widgets.hp"

static int checks = 0;
static int failed = 0;

static void ck(int cond, const char *tag) {
    checks++;
    if (!cond) {
        failed++;
        printf("FAIL %s\n", tag);
    }
}

static int all_bytes(const unsigned char *p, size_t n, unsigned char value) {
    size_t i;
    for (i = 0u; i < n; i++) {
        if (p[i] != value)
            return 0;
    }
    return 1;
}

/* Exact reported size, byte-exact text and an intact tail canary. */
static void check_serialization(stdlib_window *w, const char *expected,
                                const char *tag, error *err) {
    size_t need = 0u;
    char serial[256];
    size_t text_len = strlen(expected);

    ck(stdlib_window_serialized_size(w, &need, err) == STDLIB_OK &&
           need == text_len + 1u,
       tag);
    if (need == 0u || need > sizeof(serial))
        return;

    memset(serial, 0x5A, sizeof(serial));
    ck(stdlib_window_serialize(w, serial, need, err) == STDLIB_OK,
       "serialize_exact_fit");
    ck(strcmp(serial, expected) == 0, "serialize_text");
    ck(serial[need - 1u] == '\0', "serialize_nul");
    ck(all_bytes((const unsigned char *)serial + need,
                 sizeof(serial) - need, 0x5Au),
       "serialize_tail_canary");

    /* One byte short is still refused, with the destination untouched. */
    memset(serial, 0x3C, sizeof(serial));
    ck(stdlib_window_serialize(w, serial, need - 1u, err) == STDLIB_FAIL &&
           err->code() == CPLUS_EXIT_BOUNDS,
       "serialize_short_refused");
    ck(all_bytes((const unsigned char *)serial, sizeof(serial), 0x3Cu),
       "serialize_short_untouched");
}

int main(void) {
    error e = error();
    stdlib_window w;
    size_t need = 0u;
    char serial[256];
    const char *one =
        "WIDGETS 1\n"
        "W -1 1 -10 -20 50 50 3 hello\n";
    const char *four =
        "WIDGETS 4\n"
        "W -1 1 -10 -20 50 50 3 a\n"
        "W -22 2 -333 -4 7 8 3 bb\n"
        "W -4000 3 -5 -66 9 10 3 ccc\n"
        "W -7 4 -80000 -9 11 12 3 dddd\n";

    stdlib_set_error_callback((stdlib_error_callback)0);

    /* ---- 1. digit accounting includes the sign ----------------------- */
    ck(stdlib_widgets_dec_digits(0) == 1u, "digits_zero");
    ck(stdlib_widgets_dec_digits(9) == 1u, "digits_positive_1");
    ck(stdlib_widgets_dec_digits(10) == 2u, "digits_positive_2");
    ck(stdlib_widgets_dec_digits(-1) == 2u, "digits_minus_one");
    ck(stdlib_widgets_dec_digits(-9) == 2u, "digits_minus_one_digit");
    ck(stdlib_widgets_dec_digits(-10) == 3u, "digits_minus_two");
    ck(stdlib_widgets_dec_digits(-999) == 4u, "digits_minus_three");
    ck(stdlib_widgets_dec_digits(LLONG_MAX) == 19u, "digits_llong_max");
    ck(stdlib_widgets_dec_digits(-LLONG_MAX) == 20u, "digits_negative_llong");
    ck(stdlib_widgets_dec_digits(LLONG_MIN) == 20u, "digits_llong_min");

    /* ---- 2. issue reproducer: 3 negative fields, exact-size buffer --- */
    ck(stdlib_window_init(&w, (const stdlib_allocator *)0, 0, 0, 120, 80, 2,
                          &e) == STDLIB_OK,
       "init_one");
    ck(stdlib_widget_add(&w, -1, STDLIB_WIDGET_BUTTON, "hello", -10, -20,
                         50, 50, &e) == 0,
       "add_one");
    ck(stdlib_window_serialized_size(&w, &need, &e) == STDLIB_OK &&
           need == strlen(one) + 1u,
       "issue_repro_size");
    memset(serial, 0x5A, sizeof(serial));
    ck(stdlib_window_serialize(&w, serial, need, &e) == STDLIB_OK,
       "issue_repro_serialize");
    ck(strcmp(serial, one) == 0, "issue_repro_text");
    ck(serial[need - 1u] == '\0', "issue_repro_nul");
    ck(all_bytes((const unsigned char *)serial + need,
                 sizeof(serial) - need, 0x5Au),
       "issue_repro_no_overflow");
    stdlib_window_drop(&w, &e);

    /* ---- 3. size error scales with the number of negative widgets ---- */
    ck(stdlib_window_init(&w, (const stdlib_allocator *)0, 0, 0, 120, 80, 2,
                          &e) == STDLIB_OK,
       "init_four");
    ck(stdlib_widget_add(&w, -1, 1, "a", -10, -20, 50, 50, &e) == 0,
       "add_a");
    ck(stdlib_widget_add(&w, -22, 2, "bb", -333, -4, 7, 8, &e) == 1,
       "add_bb");
    ck(stdlib_widget_add(&w, -4000, 3, "ccc", -5, -66, 9, 10, &e) == 2,
       "add_ccc");
    ck(stdlib_widget_add(&w, -7, 4, "dddd", -80000, -9, 11, 12, &e) == 3,
       "add_dddd");
    check_serialization(&w, four, "four_negative_size", &e);
    stdlib_window_drop(&w, &e);

    printf("ISSUE216 %d %d\n", checks, failed);
    return failed == 0 ? 0 : 1;
}
