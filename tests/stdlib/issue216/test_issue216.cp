/* Issue 216 regression: serialized size must count '-' signs, and the
 * serializer must bound label/newline/NUL writes.
 *
 * A window with negative ids/coordinates used to get an exact-sized buffer
 * from stdlib_window_serialized_size() that was too small; the label and
 * trailing bytes were then written past the end.  Canary bytes after the
 * reported size make that overflow observable without depending on ASan.
 */
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/widgets.hp"

static int failures = 0;

static void check(const char *label, int ok) {
    print("issue216", label, ok ? "PASS" : "FAIL");
    if (!ok)
        failures++;
}

static int canary_intact(const char *buf, size_t from, size_t total) {
    size_t i;
    for (i = from; i < total; ++i)
        if ((unsigned char)buf[i] != 0xA5u)
            return 0;
    return 1;
}

static int make_window(stdlib_window *win, int with_more, error *err) {
    if (stdlib_window_init(win, (const stdlib_allocator *)0,
                           0, 0, 100, 100, 2, err) != STDLIB_OK)
        return 0;
    if (stdlib_widget_add(win, -1, 1, "hello", -10, -20, 50, 50, err) < 0)
        return 0;
    if (with_more) {
        if (stdlib_widget_add(win, -2, -3, "x", -4, -5, 6, 7, err) < 0)
            return 0;
        if (stdlib_widget_add(win, 7, 1, "", 0, 0, 1, 1, err) < 0)
            return 0;
    }
    return 1;
}

int main(void) {
    error err = error();
    stdlib_window win;
    stdlib_window more;
    char buf[128];
    size_t need = 0u;
    size_t need_more = 0u;
    size_t i;
    int rc;

    check("digits-zero", stdlib_widgets_dec_digits(0LL) == 1u);
    check("digits-pos", stdlib_widgets_dec_digits(12345LL) == 5u);
    check("digits-neg", stdlib_widgets_dec_digits(-1LL) == 2u);
    check("digits-neg5", stdlib_widgets_dec_digits(-12345LL) == 6u);
    check("digits-min", stdlib_widgets_dec_digits(LLONG_MIN) == 20u);

    check("make", make_window(&win, 0, &err));
    check("make-more", make_window(&more, 1, &err));

    error_reset(&err);
    rc = stdlib_window_serialized_size(&win, &need, &err);
    check("size", rc == STDLIB_OK && err.ok() && need == 40u);
    error_reset(&err);
    rc = stdlib_window_serialized_size(&more, &need_more, &err);
    check("size-more", rc == STDLIB_OK && err.ok() && need_more == 79u);

    memset(buf, 0xA5, sizeof(buf));
    error_reset(&err);
    rc = stdlib_window_serialize(&win, buf, need, &err);
    check("serialize", rc == STDLIB_OK && err.ok());
    check("exact-output",
          strcmp(buf, "WIDGETS 1\nW -1 1 -10 -20 50 50 3 hello\n") == 0 &&
          strlen(buf) + 1u == need);
    check("canary", canary_intact(buf, need, sizeof(buf)));

    memset(buf, 0xA5, sizeof(buf));
    error_reset(&err);
    rc = stdlib_window_serialize(&more, buf, need_more, &err);
    check("serialize-more", rc == STDLIB_OK && err.ok());
    check("exact-output-more",
          strcmp(buf, "WIDGETS 3\n"
                      "W -1 1 -10 -20 50 50 3 hello\n"
                      "W -2 -3 -4 -5 6 7 3 x\n"
                      "W 7 1 0 0 1 1 3 \n") == 0 &&
          strlen(buf) + 1u == need_more);
    check("canary-more", canary_intact(buf, need_more, sizeof(buf)));

    /* Too-small buffer: fail with BOUNDS and leave the destination alone. */
    memset(buf, 0xA5, sizeof(buf));
    error_reset(&err);
    rc = stdlib_window_serialize(&win, buf, need - 1u, &err);
    check("too-small", rc == STDLIB_FAIL && err.failed_now() &&
                       err.category() == STDLIB_CATEGORY_BOUNDS);
    for (i = 0u; i < sizeof(buf); ++i) {
        if ((unsigned char)buf[i] != 0xA5u)
            break;
    }
    check("untouched", i == sizeof(buf));

    stdlib_window_drop(&win, &err);
    stdlib_window_drop(&more, &err);
    print("issue216-summary", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
