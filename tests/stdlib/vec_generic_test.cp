/* Deterministic checks for the additive generic Vec(T)/Slice(T) layer.
 * Covers int, float, struct Foo and string, heap RAII, arena/datastack-backed
 * storage, bounds refusal and stale-slice rejection through the lifetime
 * registry.  Every line of stdout is fixed; the final record is
 * VEC_GENERIC <checks> <failures>.
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/arena.hp"
#include "stdlib/datastack.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"

typedef struct Foo { int id; double weight; } Foo;

VEC_DEFINE(int, VEC_NO_DROP)
VEC_DEFINE(float, VEC_NO_DROP)
VEC_DEFINE(Foo, VEC_NO_DROP)
VEC_DEFINE(string, VEC_STRING_DROP)

#define VEC_DEFAULT_TYPE int

static int checks = 0;
static int failures = 0;
static int alloc_ops = 0;

static void ck(int ok, const char *tag) {
    ++checks;
    if (!ok) { ++failures; printf("FAIL %s\n", tag); }
}

static void *count_arena_alloc(void *ctx, size_t n, size_t a) {
    ++alloc_ops;
    return stdlib_arena_op_alloc(ctx, n, a);
}
static void *count_ds_alloc(void *ctx, size_t n, size_t a) {
    error e = error_ctor();
    ++alloc_ops;
    return stdlib_datastack_alloc((stdlib_datastack *)ctx, n, a, &e);
}
static void count_ds_release(void *ctx, void *p, size_t n) {
    error e = error_ctor();
    stdlib_datastack_release((stdlib_datastack *)ctx, p, n, &e);
}

static int heap_delta(const stdlib_telemetry_stats *a,
                      const stdlib_telemetry_stats *b) {
    return (int)(b->alloc_count - a->alloc_count);
}

int main(void) {
    error e = error();
    stdlib_telemetry_stats before, after;
    stdlib_arena arena;
    stdlib_datastack stack;
    stdlib_allocator counted;
    int out = -1;
    float f = 0;
    Foo foo;
    int i;

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();

    /* ---- heap-backed RAII: one buffer, automatic drop at scope exit ----- */
    stdlib_telemetry_snapshot(&before);
    {
        Vec(int) v = Vec_init(int);
        vec_owner guard = Vec_guard(int, v);
        ck(Vec_push(int, &v, 7) == 0 && Vec_push(int, &v, 8) == 0, "int_push");
        ck(vec_get(&v, 0, &out) == 0 && out == 7, "vec_get");
        ck(vec_set(&v, 0, 9) == 0, "vec_set");
        ck(vec_get(&v, 0, &out) == 0 && out == 9, "vec_set_readback");
        ck(vec_get(&v, 2, &out) == -1 && vec_set(&v, 2, 5) == -1,
           "bounds_refused");
        ck(out == 9, "bounds_no_write");
        printf("INT %d %d\n", (int)v.used, out);
    }
    stdlib_telemetry_snapshot(&after);
    ck(heap_delta(&before, &after) == 1, "heap_single_buffer");

    /* ---- float --------------------------------------------------------- */
    {
        Vec(float) v = Vec_init(float);
        vec_owner guard = Vec_guard(float, v);
        ck(Vec_push(float, &v, 1.5f) == 0, "float_push");
        ck(Vec_get(float, &v, 0, &f) == 0 && f == 1.5f, "float_get");
        ck(Vec_set(float, &v, 0, 2.5f) == 0 &&
           Vec_get(float, &v, 0, &f) == 0 && f == 2.5f, "float_set");
        printf("FLOAT %.1f\n", (double)f);
    }

    /* ---- struct Foo ---------------------------------------------------- */
    {
        Vec(Foo) v = Vec_init(Foo);
        vec_owner guard = Vec_guard(Foo, v);
        foo.id = 3; foo.weight = 4.25;
        ck(Vec_push(Foo, &v, foo) == 0, "struct_push");
        ck(Vec_get(Foo, &v, 0, &foo) == 0 && foo.id == 3 &&
           foo.weight == 4.25, "struct_get");
        printf("STRUCT %d %.2f\n", foo.id, foo.weight);
    }

    /* ---- string: element destructor runs exactly once ------------------ */
    {
        Vec(string) v = Vec_init(string);
        vec_owner guard = Vec_guard(string, v);
        string src = string("alpha");
        string seen;
        ck(Vec_push(string, &v, cplus_move(src)) == 0, "string_push");
        ck(Vec_get(string, &v, 0, &seen) == 0 && seen.equals("alpha"),
           "string_get");
        printf("STRING %s\n", seen.c_str());
        cplus_zero(&seen, sizeof(seen)); /* borrowed read; Vec owns the element */
    }

    /* ---- arena with exact init spelling and no per-element heap -------- */
    ck(stdlib_arena_init(&arena, 4096, &e) == STDLIB_OK, "arena_init");
    counted = stdlib_arena_as_allocator(&arena);
    counted.alloc = count_arena_alloc;
    alloc_ops = 0;
    stdlib_telemetry_snapshot(&before);
    {
        Vec(int) v = Vec_init_arena(&counted);
        Slice(int) stale;
        ck(Vec_push(int, &v, 1) == 0, "arena_first_push");
        stale = Vec_view(int, &v);
        ck(Slice_stale(int, &stale) == 0, "arena_slice_live");
        for (i = 0; i < 12; ++i) ck(Vec_push(int, &v, i) == 0, "arena_push");
        ck(Vec_get(int, &v, 11, &out) == 0 && out == 10, "arena_get");
        ck(alloc_ops == 3, "arena_single_buffer_growth"); /* 4 -> 8 -> 16 */
        ck(Slice_stale(int, &stale) == 1, "arena_slice_stale_after_growth");
        ck(Slice_get(int, &stale, 0, &out) == -1, "stale_read_refused");
        ck(Slice_set(int, &stale, 0, 99) == -1, "stale_write_refused");
        printf("ARENA %d %d\n", alloc_ops, out);
    }
    stdlib_telemetry_snapshot(&after);
    ck(heap_delta(&before, &after) == 0, "arena_no_heap");
    stdlib_arena_destroy(&arena, &e);

    /* ---- datastack-backed allocation ----------------------------------- */
    ck(stdlib_datastack_init(&stack, 1u << 20u, &e) == STDLIB_OK, "ds_init");
    counted = stdlib_heap_allocator();
    counted.ctx = (void *)&stack;
    counted.alloc = count_ds_alloc;
    counted.release = count_ds_release;
    alloc_ops = 0;
    stdlib_telemetry_snapshot(&before);
    {
        Vec(int) v = Vec_init_arena_t(int, &counted);
        for (i = 0; i < 12; ++i) ck(Vec_push(int, &v, i) == 0, "ds_push");
        ck(Vec_get(int, &v, 11, &out) == 0 && out == 11, "ds_get");
        ck(alloc_ops == 3, "ds_single_buffer_growth");
        printf("STACK %d %d\n", alloc_ops, out);
        Vec_drop(int, &v);
    }
    stdlib_telemetry_snapshot(&after);
    ck(heap_delta(&before, &after) == 0, "ds_no_heap");
    stdlib_datastack_destroy(&stack, &e);

    /* ---- stale view after owner drop ----------------------------------- */
    {
        Vec(int) v = Vec_init(int);
        vec_owner guard = Vec_guard(int, v);
        Slice(int) s = Vec_view(int, &v); /* empty: unbound, not stale */
        ck(Slice_stale(int, &s) == 0, "empty_view_not_stale");
        Vec_push(int, &v, 5);
        s = Vec_view(int, &v);
        ck(Slice_stale(int, &s) == 0 && Slice_data(int, &s) != (int *)0,
           "live_view");
        Vec_drop(int, &v);
        ck(Slice_stale(int, &s) == 1 && Slice_data(int, &s) == (int *)0,
           "view_stale_after_drop");
    }

    stdlib_telemetry_set_enabled(0);
    printf("VEC_GENERIC %d %d\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
