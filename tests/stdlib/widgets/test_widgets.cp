/* Headless widget-toolkit core tests (deterministic C+ program).
 *
 * Build from the repository root with --default / no sanitizer:
 *   ./build/c+ build tests/stdlib/widgets/test_widgets.cp \
 *       --default --sanitize=none -I. -- build/libc/libc/libcplus_hardened.a \
 *       -o /tmp/test_widgets
 *
 * Every check prints only on failure; the last line is
 *   WIDGETS <checks> <failures>
 * so run.sh can verify counts and byte-for-byte determinism.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/widgets.hp"

static int checks = 0;
static int fails = 0;

static void ck(int cond, const char *label) {
    checks++;
    if (!cond) {
        fails++;
        printf("FAIL %s\n", label);
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

/* Canaries detect any write outside the window struct. */
typedef struct guarded_window {
    unsigned char pre[64];
    stdlib_window win;
    unsigned char post[64];
} guarded_window;

static void guard_init(guarded_window *g) {
    memset(g, 0xCC, sizeof(*g));
    memset(g->pre, 0xA5, sizeof(g->pre));
    memset(g->post, 0x5A, sizeof(g->post));
}

static int guard_ok(const guarded_window *g) {
    return all_bytes(g->pre, sizeof(g->pre), 0xA5) &&
           all_bytes(g->post, sizeof(g->post), 0x5A);
}

/* Counting allocator with one-shot failure injection. */
typedef struct count_ctx {
    int allocs;
    int reallocs;
    int releases;
    int resets;
    int fail_next;
} count_ctx;

static void *cnt_alloc(void *ctx, size_t size, size_t align) {
    count_ctx *c = (count_ctx *)ctx;
    (void)align;
    c->allocs++;
    if (c->fail_next)
        return (void *)0;
    return malloc(size);
}

static void *cnt_realloc(void *ctx, void *ptr, size_t old_size,
                         size_t new_size, size_t align) {
    count_ctx *c = (count_ctx *)ctx;
    (void)old_size;
    (void)align;
    c->reallocs++;
    if (c->fail_next)
        return (void *)0;
    return realloc(ptr, new_size);
}

static void cnt_release(void *ctx, void *ptr, size_t size) {
    count_ctx *c = (count_ctx *)ctx;
    (void)size;
    c->releases++;
    free(ptr);
}

static void cnt_reset(void *ctx) {
    count_ctx *c = (count_ctx *)ctx;
    c->resets++;
}

int main(void) {
    error e = error();
    char big[STDLIB_WIDGET_LABEL_MAX + 16];
    char exact[STDLIB_WIDGET_LABEL_MAX];
    char serial[512];
    char serial2[512];
    char serial3[512];
    size_t out_index = 0u;
    size_t need = 0u;
    const char *expected;
    int idx;
    int i;

    stdlib_set_error_callback((stdlib_error_callback)0);

    /* ---- 1. lifecycle, refusals, flags, lookup, canaries ---------------- */
    {
        guarded_window g;
        guard_init(&g);
        ck(stdlib_window_init(&g.win, (const stdlib_allocator *)0,
                              0, 0, 80, 40, 2, &e) == STDLIB_OK, "init_ok");
        ck(stdlib_window_count(&g.win, &e) == 0u, "init_empty");

        idx = stdlib_widget_add(&g.win, 10, STDLIB_WIDGET_BUTTON,
                                "OK", 1, 2, 8, 3, &e);
        ck(idx == 0, "add_first");
        idx = stdlib_widget_add(&g.win, 20, STDLIB_WIDGET_LABEL,
                                "Cancel", 12, 2, 8, 3, &e);
        ck(idx == 1, "add_second");
        ck(stdlib_window_count(&g.win, &e) == 2u, "count_two");
        ck(g.win.items[0].flags == (STDLIB_WIDGET_ENABLED |
                                    STDLIB_WIDGET_VISIBLE), "flags_default");
        ck(strcmp(g.win.items[1].label, "Cancel") == 0, "label_copied");

        /* Duplicate id. */
        ck(stdlib_widget_add(&g.win, 10, 1, "dup", 0, 0, 1, 1, &e) ==
               STDLIB_FAIL, "add_duplicate_refused");
        ck(e.code() == CPLUS_EXIT_UNSUPPORTED, "add_duplicate_code");
        ck(stdlib_window_count(&g.win, &e) == 2u, "add_duplicate_no_change");

        /* Invalid bounds: zero/negative extent and int overflow. */
        ck(stdlib_widget_add(&g.win, 30, 1, "z", 0, 0, 0, 4, &e) ==
               STDLIB_FAIL, "add_zero_width_refused");
        ck(stdlib_widget_add(&g.win, 30, 1, "z", 0, 0, 4, 0, &e) ==
               STDLIB_FAIL, "add_zero_height_refused");
        ck(stdlib_widget_add(&g.win, 30, 1, "z", 0, -2, 4, 4, &e) >= 0,
           "add_negative_origin_ok");
        ck(stdlib_widget_add(&g.win, 31, 1, "z", INT_MAX, 0, 2, 2, &e) ==
               STDLIB_FAIL, "add_overflow_bounds_refused");
        ck(e.code() == CPLUS_EXIT_UNSUPPORTED, "add_bad_bounds_code");

        /* Oversized / NULL / control-character labels. */
        memset(big, 'x', sizeof(big) - 1u);
        big[sizeof(big) - 1u] = '\0';
        ck(stdlib_widget_add(&g.win, 40, 1, big, 0, 0, 2, 2, &e) ==
               STDLIB_FAIL, "add_oversized_label_refused");
        ck(e.code() == CPLUS_EXIT_UNSUPPORTED, "oversized_label_code");
        ck(stdlib_widget_add(&g.win, 40, 1, (const char *)0, 0, 0, 2, 2, &e) ==
               STDLIB_FAIL, "add_null_label_refused");
        ck(stdlib_widget_add(&g.win, 40, 1, "bad\nlabel", 0, 0, 2, 2, &e) ==
               STDLIB_FAIL, "add_newline_label_refused");

        /* Boundary label: exactly STDLIB_WIDGET_LABEL_MAX-1 bytes is accepted. */
        memset(exact, 'y', sizeof(exact) - 1u);
        exact[sizeof(exact) - 1u] = '\0';
        idx = stdlib_widget_add(&g.win, 50, 1, exact, 0, 0, 2, 2, &e);
        ck(idx >= 0, "add_max_label_ok");
        if (idx >= 0)
            ck(strlen(g.win.items[(size_t)idx].label) ==
                   STDLIB_WIDGET_LABEL_MAX - 1u, "max_label_length");

        /* Mutators and out-of-range indices. */
        ck(stdlib_widget_label(&g.win, 0u, "Yes", &e) == STDLIB_OK,
           "widget_label_ok");
        ck(strcmp(g.win.items[0].label, "Yes") == 0, "widget_label_applied");
        ck(stdlib_widget_label(&g.win, 0u, big, &e) == STDLIB_FAIL,
           "widget_label_oversized_refused");
        ck(strcmp(g.win.items[0].label, "Yes") == 0,
           "widget_label_failed_no_change");
        ck(stdlib_widget_set_bounds(&g.win, 0u, 5, 6, 7, 8, &e) == STDLIB_OK,
           "set_bounds_ok");
        ck(g.win.items[0].x == 5 && g.win.items[0].y == 6 &&
           g.win.items[0].w == 7 && g.win.items[0].h == 8,
           "set_bounds_applied");
        ck(stdlib_widget_set_bounds(&g.win, 0u, 5, 6, 0, 8, &e) == STDLIB_FAIL,
           "set_bounds_invalid_refused");
        ck(g.win.items[0].w == 7, "set_bounds_failed_no_change");
        ck(stdlib_widget_enable(&g.win, 0u, 0, &e) == STDLIB_OK, "disable_ok");
        ck((g.win.items[0].flags & STDLIB_WIDGET_ENABLED) == 0u,
           "disable_flag_cleared");
        ck(stdlib_widget_enable(&g.win, 0u, 1, &e) == STDLIB_OK, "enable_ok");
        ck(stdlib_widget_visible(&g.win, 0u, 0, &e) == STDLIB_OK, "hide_ok");
        ck((g.win.items[0].flags & STDLIB_WIDGET_VISIBLE) == 0u,
           "hide_flag_cleared");
        ck(stdlib_widget_visible(&g.win, 0u, 1, &e) == STDLIB_OK, "show_ok");

        ck(stdlib_widget_label(&g.win, 999u, "x", &e) == STDLIB_FAIL,
           "label_index_oor");
        ck(e.code() == CPLUS_EXIT_BOUNDS, "label_index_oor_code");
        ck(stdlib_widget_set_bounds(&g.win, (size_t)-1, 0, 0, 1, 1, &e) ==
               STDLIB_FAIL, "bounds_index_oor");
        ck(stdlib_widget_enable(&g.win, 999u, 0, &e) == STDLIB_FAIL,
           "enable_index_oor");
        ck(stdlib_widget_visible(&g.win, 999u, 0, &e) == STDLIB_FAIL,
           "visible_index_oor");

        /* Lookup. */
        ck(stdlib_widget_index_by_id(&g.win, 20, &e) == 1, "index_by_id_found");
        ck(stdlib_widget_index_by_id(&g.win, 999, &e) == -1,
           "index_by_id_missing");
        ck(stdlib_widget_find_by_id(&g.win, 10, &e) == &g.win.items[0],
           "find_by_id_pointer");
        ck(stdlib_widget_find_by_id(&g.win, 999, &e) == (stdlib_widget *)0,
           "find_by_id_missing");

        /* NULL window is refused by every public entry point. */
        ck(stdlib_widget_add((stdlib_window *)0, 1, 1, "x", 0, 0, 1, 1, &e) ==
               STDLIB_FAIL, "add_null_window");
        ck(stdlib_widget_label((stdlib_window *)0, 0u, "x", &e) == STDLIB_FAIL,
           "label_null_window");
        ck(stdlib_widget_set_bounds((stdlib_window *)0, 0u, 0, 0, 1, 1, &e) ==
               STDLIB_FAIL, "bounds_null_window");
        ck(stdlib_widget_enable((stdlib_window *)0, 0u, 1, &e) == STDLIB_FAIL,
           "enable_null_window");
        ck(stdlib_widget_visible((stdlib_window *)0, 0u, 1, &e) == STDLIB_FAIL,
           "visible_null_window");
        ck(stdlib_widget_find_by_id((stdlib_window *)0, 1, &e) ==
               (stdlib_widget *)0, "find_null_window");
        ck(e.code() == CPLUS_EXIT_NULL_POINTER, "null_window_code");
        ck(stdlib_window_count((stdlib_window *)0, &e) == 0u,
           "count_null_window");
        ck(stdlib_window_layout((stdlib_window *)0, &e) == STDLIB_FAIL,
           "layout_null_window");
        ck(stdlib_window_hit_test((stdlib_window *)0, 0, 0, &out_index, &e) ==
               STDLIB_FAIL, "hit_null_window");
        ck(stdlib_window_serialize((stdlib_window *)0, serial, sizeof(serial),
                                   &e) == STDLIB_FAIL, "serialize_null_window");
        ck(stdlib_window_init((stdlib_window *)0, (const stdlib_allocator *)0,
                              0, 0, 10, 10, 1, &e) == STDLIB_FAIL,
           "init_null_window");

        ck(guard_ok(&g), "canaries_after_mutations");
        stdlib_window_drop(&g.win, &e);
        ck(g.win.items == (stdlib_widget *)0 && g.win.count == 0u &&
           g.win.capacity == 0u, "drop_resets_vector");
        ck(guard_ok(&g), "canaries_after_drop");
        stdlib_window_drop(&g.win, &e); /* idempotent */
        ck(guard_ok(&g), "canaries_after_double_drop");
    }

    /* ---- 2. deterministic grid layout + serialization ------------------ */
    {
        stdlib_window w;
        const char *names[5] = { "w0", "w1", "w2", "w3", "w4" };
        ck(stdlib_window_init(&w, (const stdlib_allocator *)0,
                              10, 20, 100, 50, 2, &e) == STDLIB_OK,
           "grid_init");
        for (i = 0; i < 5; i++) {
            idx = stdlib_widget_add(&w, 100 + i, 1 + i, names[i],
                                    1, 1, 2, 2, &e);
            ck(idx == i, "grid_add");
        }
        expected =
            "WIDGETS 5\n"
            "W 100 1 11 21 48 14 3 w0\n"
            "W 101 2 61 21 48 14 3 w1\n"
            "W 102 3 11 37 48 14 3 w2\n"
            "W 103 4 61 37 48 14 3 w3\n"
            "W 104 5 11 53 48 14 3 w4\n";

        memset(serial, 0x5A, sizeof(serial));
        ck(stdlib_window_serialize(&w, serial, sizeof(serial), &e) ==
               STDLIB_OK, "serialize_before_layout");
        ck(stdlib_window_layout(&w, &e) == STDLIB_OK, "layout_ok");
        ck(stdlib_window_serialize(&w, serial, sizeof(serial), &e) ==
               STDLIB_OK, "serialize_after_layout");
        ck(strcmp(serial, expected) == 0, "layout_exact_serialization");
        if (strcmp(serial, expected) != 0)
            printf("  got: %s\n", serial);
        ck(stdlib_window_serialized_size(&w, &need, &e) == STDLIB_OK &&
           need == strlen(expected) + 1u, "serialized_size_exact");
        ck(all_bytes((const unsigned char *)serial + strlen(expected) + 1u,
                     sizeof(serial) - strlen(expected) - 1u, 0x5A),
           "serialize_tail_canary");

        /* Re-running layout is a fixed point; same bytes every time. */
        ck(stdlib_window_layout(&w, &e) == STDLIB_OK, "layout_twice");
        memset(serial2, 0x3C, sizeof(serial2));
        ck(stdlib_window_serialize(&w, serial2, sizeof(serial2), &e) ==
               STDLIB_OK, "serialize_second");
        ck(strcmp(serial, serial2) == 0, "layout_deterministic");

        /* Exact-size buffer works; one byte short is refused, buffer intact. */
        ck(stdlib_window_serialize(&w, serial3, need, &e) == STDLIB_OK,
           "serialize_exact_buffer");
        memset(serial3, 0x3C, sizeof(serial3));
        ck(stdlib_window_serialize(&w, serial3, need - 1u, &e) ==
               STDLIB_FAIL, "serialize_short_buffer_refused");
        ck(e.code() == CPLUS_EXIT_BOUNDS, "serialize_short_code");
        ck(all_bytes((const unsigned char *)serial3, sizeof(serial3), 0x3C),
           "serialize_short_buffer_untouched");
        ck(stdlib_window_serialize(&w, (char *)0, sizeof(serial), &e) ==
               STDLIB_FAIL, "serialize_null_buffer");
        ck(stdlib_window_serialized_size(&w, (size_t *)0, &e) ==
               STDLIB_FAIL, "serialized_size_null");

        /* ---- 3. hit testing ------------------------------------------- */
        ck(stdlib_window_hit_test(&w, 11, 21, &out_index, &e) == 1 &&
           out_index == 0u, "hit_first_cell");
        ck(stdlib_window_hit_test(&w, 61, 21, &out_index, &e) == 1 &&
           out_index == 1u, "hit_second_cell");
        ck(stdlib_window_hit_test(&w, 11, 53, &out_index, &e) == 1 &&
           out_index == 4u, "hit_last_cell");
        ck(stdlib_window_hit_test(&w, 10, 20, &out_index, &e) == 0,
           "hit_inset_miss");
        ck(stdlib_window_hit_test(&w, 200, 200, &out_index, &e) == 0,
           "hit_outside_miss");
        ck(stdlib_window_hit_test(&w, 0, 0, (size_t *)0, &e) == STDLIB_FAIL,
           "hit_null_output");
        ck(e.code() == CPLUS_EXIT_NULL_POINTER, "hit_null_output_code");

        ck(stdlib_widget_enable(&w, 1u, 0, &e) == STDLIB_OK, "hit_disable");
        ck(stdlib_window_hit_test(&w, 61, 21, &out_index, &e) == 0,
           "hit_disabled_miss");
        ck(stdlib_widget_enable(&w, 1u, 1, &e) == STDLIB_OK, "hit_reenable");
        ck(stdlib_widget_visible(&w, 0u, 0, &e) == STDLIB_OK, "hit_hide");
        ck(stdlib_window_hit_test(&w, 11, 21, &out_index, &e) == 0,
           "hit_hidden_miss");
        ck(stdlib_widget_visible(&w, 0u, 1, &e) == STDLIB_OK, "hit_show");

        /* Overlap: topmost (highest index) enabled + visible widget wins. */
        ck(stdlib_widget_set_bounds(&w, 4u, 11, 21, 8, 8, &e) == STDLIB_OK,
           "hit_overlap_setup");
        ck(stdlib_window_hit_test(&w, 12, 22, &out_index, &e) == 1 &&
           out_index == 4u, "hit_topmost_overlap");

        /* Flags and label changes are reflected in serialization. */
        ck(stdlib_widget_enable(&w, 0u, 0, &e) == STDLIB_OK, "flag_serialize_off");
        ck(stdlib_widget_visible(&w, 1u, 0, &e) == STDLIB_OK, "flag_serialize_hide");
        ck(stdlib_widget_label(&w, 0u, "Go", &e) == STDLIB_OK, "label_serialize_set");
        memset(serial, 0x5A, sizeof(serial));
        ck(stdlib_window_serialize(&w, serial, sizeof(serial), &e) == STDLIB_OK,
           "serialize_flags");
        ck(strcmp(serial,
                  "WIDGETS 5\n"
                  "W 100 1 11 21 48 14 2 Go\n"
                  "W 101 2 61 21 48 14 1 w1\n"
                  "W 102 3 11 37 48 14 3 w2\n"
                  "W 103 4 61 37 48 14 3 w3\n"
                  "W 104 5 11 21 8 8 3 w4\n") == 0,
           "flags_serialization_exact");

        stdlib_window_drop(&w, &e);
    }

    /* ---- 4. allocator-aware storage, failure injection ----------------- */
    {
        count_ctx ctx;
        stdlib_allocator A;
        guarded_window g;
        stdlib_widget snap[8];
        guard_init(&g);
        memset(&ctx, 0, sizeof(count_ctx));
        A.ctx = &ctx;
        A.alloc = cnt_alloc;
        A.realloc = cnt_realloc;
        A.release = cnt_release;
        A.reset = cnt_reset;

        ck(stdlib_window_init(&g.win, &A, 0, 0, 40, 20, 4, &e) == STDLIB_OK,
           "custom_alloc_init");
        ck(ctx.allocs == 0 && ctx.reallocs == 0, "custom_alloc_lazy");
        for (i = 0; i < 8; i++) {
            idx = stdlib_widget_add(&g.win, 1000 + i, 1, "a",
                                    0, 0, 2, 2, &e);
            ck(idx == i, "custom_alloc_add");
        }
        ck(ctx.reallocs == 1, "custom_alloc_growth_count");
        ck(g.win.capacity == 8u, "custom_alloc_capacity");

        for (i = 0; i < 8; i++)
            snap[i] = g.win.items[i];
        ctx.fail_next = 1;
        ck(stdlib_widget_add(&g.win, 2000, 1, "b", 0, 0, 2, 2, &e) ==
               STDLIB_FAIL, "alloc_failure_refused");
        ck(e.code() == CPLUS_EXIT_RESOURCE, "alloc_failure_code");
        ck(stdlib_window_count(&g.win, &e) == 8u, "alloc_failure_count_intact");
        ck(snap[0].id == g.win.items[0].id &&
           snap[7].id == g.win.items[7].id &&
           strcmp(snap[7].label, g.win.items[7].label) == 0 &&
           snap[7].w == g.win.items[7].w, "alloc_failure_items_intact");
        ck(guard_ok(&g), "alloc_failure_canaries");

        ctx.fail_next = 0;
        ck(stdlib_widget_add(&g.win, 2000, 1, "b", 0, 0, 2, 2, &e) == 8,
           "alloc_retry_succeeds");
        ck(ctx.reallocs == 3, "custom_alloc_retry_count");
        stdlib_allocator_reset(&A);
        ck(ctx.resets == 1, "custom_alloc_reset_dispatch");

        stdlib_window_drop(&g.win, &e);
        ck(ctx.releases == 1, "custom_alloc_release_once");
        stdlib_window_drop(&g.win, &e);
        ck(ctx.releases == 1, "custom_alloc_no_double_release");
        ck(guard_ok(&g), "custom_alloc_canaries");
    }

    /* ---- 5. configuration refusals, empty window ----------------------- */
    {
        stdlib_window w;
        ck(stdlib_window_init(&w, (const stdlib_allocator *)0,
                              0, 0, 10, 10, 0, &e) == STDLIB_FAIL,
           "init_zero_columns_refused");
        ck(stdlib_window_init(&w, (const stdlib_allocator *)0,
                              0, 0, 0, 10, 2, &e) == STDLIB_FAIL,
           "init_zero_width_refused");
        ck(e.code() == CPLUS_EXIT_UNSUPPORTED, "init_bad_config_code");

        ck(stdlib_window_init(&w, (const stdlib_allocator *)0,
                              0, 0, 100, 50, 2, &e) == STDLIB_OK,
           "empty_layout_init");
        ck(stdlib_window_layout(&w, &e) == STDLIB_OK, "empty_layout_ok");
        ck(stdlib_window_serialize(&w, serial, sizeof(serial), &e) ==
               STDLIB_OK && strcmp(serial, "WIDGETS 0\n") == 0,
           "empty_serialization");
        ck(stdlib_window_hit_test(&w, 5, 5, &out_index, &e) == 0,
           "empty_hit_miss");
        stdlib_window_drop(&w, &e);

        ck(stdlib_window_init(&w, (const stdlib_allocator *)0,
                              0, 0, 3, 3, 2, &e) == STDLIB_OK,
           "tiny_grid_init");
        ck(stdlib_widget_add(&w, 1, 1, "x", 0, 0, 1, 1, &e) == 0,
           "tiny_grid_add");
        ck(stdlib_widget_set_bounds(&w, 0u, 7, 7, 1, 1, &e) == STDLIB_OK,
           "tiny_grid_mark");
        ck(stdlib_window_layout(&w, &e) == STDLIB_FAIL,
           "tiny_grid_refused");
        ck(e.code() == CPLUS_EXIT_BOUNDS, "tiny_grid_code");
        ck(w.items[0].x == 7 && w.items[0].y == 7,
           "tiny_grid_no_change_on_failure");
        stdlib_window_drop(&w, &e);
    }

    /* ---- 6. extreme-but-valid coordinates (no signed overflow) --------- */
    {
        stdlib_window w;
        ck(stdlib_window_init(&w, (const stdlib_allocator *)0,
                              0, 0, 100, 50, 2, &e) == STDLIB_OK,
           "extreme_init");
        ck(stdlib_widget_add(&w, -5, 1, "neg", -3, -4, 5, 6, &e) == 0,
           "extreme_add_negative");
        ck(stdlib_widget_add(&w, INT_MAX, 1, "far", INT_MAX - 2, 0, 3, 3, &e)
               == 1, "extreme_add_far");
        ck(stdlib_window_serialize(&w, serial, sizeof(serial), &e) ==
               STDLIB_OK &&
           strcmp(serial,
                  "WIDGETS 2\n"
                  "W -5 1 -3 -4 5 6 3 neg\n"
                  "W 2147483647 1 2147483645 0 3 3 3 far\n") == 0,
           "extreme_serialization");
        ck(stdlib_window_hit_test(&w, INT_MAX - 1, 1, &out_index, &e) == 1 &&
           out_index == 1u, "extreme_hit_right_edge");
        ck(stdlib_window_hit_test(&w, INT_MAX, 1, &out_index, &e) == 1,
           "extreme_hit_last_pixel");
        ck(stdlib_window_hit_test(&w, -3, -4, &out_index, &e) == 1 &&
           out_index == 0u, "extreme_hit_negative");
        stdlib_window_drop(&w, &e);
    }

    printf("WIDGETS %d %d\n", checks, fails);
    return fails == 0 ? 0 : 1;
}
