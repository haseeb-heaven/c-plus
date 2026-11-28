/* c_to_cp_caller.cp -- the "C+" side of the lab.
 *
 * A real C+ translation unit (.cp) lowered by ./build/c+ and linked against a
 * real object archive produced by /usr/bin/clang.  Every symbol it calls is an
 * ordinary C function declared in c_to_cp_shared.h.  This file honours the
 * strict rewriter constraints: unique lab_-prefixed names, no subscripting of
 * pointer parameters, no variable division/modulo, no chained temporaries.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "c_to_cp_shared.h"

static int lab_checks = 0;
static int lab_failures = 0;

static void lab_ck(const char *name, int ok)
{
    lab_checks++;
    if (!ok)
        lab_failures++;
    printf("%-62s %s\n", name, ok ? "OK" : "FAIL");
}

/* ---- callback implemented in C+, registered into the C library --------- */
static int32_t lab_ctx_base = 100;

static int32_t lab_on_event(int32_t value, void *ctx)
{
    int32_t lab_base = *((int32_t *)ctx);
    return value + lab_base;
}

/* ---- plain functions passed as function-pointer arguments -------------- */
static int32_t lab_add_op(int32_t a, int32_t b)
{
    return a + b;
}

static int32_t lab_mul_op(int32_t a, int32_t b)
{
    return a * b;
}

int main(void)
{
    cto_point  lab_p;
    cto_point  lab_shifted;
    cto_point  lab_corner_a;
    cto_point  lab_corner_b;
    cto_rect   lab_rect;
    int32_t    lab_area;
    int32_t    lab_rc;
    int32_t    lab_sum;
    int32_t    lab_array[5];
    int32_t    lab_fired;
    cto_store *lab_store;
    int32_t    lab_stored;
    cto_span   lab_from_c;
    cto_span   lab_to_c;
    int32_t   *lab_writer;
    int32_t   *lab_owned;
    int32_t    lab_checksum;

    printf("c_to_cp_caller: begin\n");

    /* ---- 1. struct arguments / returns --------------------------------- */
    lab_p.x = 3;
    lab_p.y = 9;
    lab_shifted = cto_point_translate(lab_p, 4, -2);
    lab_ck("struct return: cto_point_translate -> {7,7}",
           lab_shifted.x == 7 && lab_shifted.y == 7);

    lab_corner_a.x = 1;
    lab_corner_a.y = 2;
    lab_corner_b.x = 5;
    lab_corner_b.y = 7;
    lab_rect = cto_rect_from_points(lab_corner_a, lab_corner_b);
    lab_ck("struct return: cto_rect_from_points lo",
           lab_rect.lo.x == 1 && lab_rect.lo.y == 2);
    lab_ck("struct return: cto_rect_from_points hi",
           lab_rect.hi.x == 5 && lab_rect.hi.y == 7);

    lab_area = 0;
    lab_rc = cto_rect_area(&lab_rect, &lab_area);
    lab_ck("struct ptr arg + error code: rect_area -> 20",
           lab_rc == CTO_OK && lab_area == 20);
    lab_rc = cto_rect_area((const cto_rect *)0, &lab_area);
    lab_ck("error code: rect_area(NULL) -> CTO_ERR_NULL",
           lab_rc == CTO_ERR_NULL);

    /* ---- 2. pointer arguments ------------------------------------------ */
    lab_array[0] = 1;
    lab_array[1] = 2;
    lab_array[2] = 3;
    lab_array[3] = 4;
    lab_array[4] = 5;
    lab_sum = 0;
    lab_rc = cto_sum_ints(lab_array, 5u, &lab_sum);
    lab_ck("pointer arg: cto_sum_ints -> 15",
           lab_rc == CTO_OK && lab_sum == 15);

    lab_rc = cto_scale_in_place(lab_array, 5u, 2);
    lab_sum = 0;
    (void)cto_sum_ints(lab_array, 5u, &lab_sum);
    lab_ck("pointer arg: scale_in_place then sum -> 30",
           lab_rc == CTO_OK && lab_sum == 30);

    lab_rc = cto_sum_ints((const int32_t *)0, 5u, &lab_sum);
    lab_ck("error code: sum_ints(NULL) -> CTO_ERR_NULL",
           lab_rc == CTO_ERR_NULL);

    /* ---- 3. callback registration ------------------------------------- */
    lab_rc = cto_register_callback(lab_on_event, &lab_ctx_base);
    lab_ck("callback: register C+ function into C", lab_rc == CTO_OK);
    lab_fired = 0;
    lab_rc = cto_fire_callback(10, &lab_fired);
    lab_ck("callback: C fires C+ callback -> 110",
           lab_rc == CTO_OK && lab_fired == 110);
    (void)cto_clear_callback();
    lab_rc = cto_fire_callback(10, &lab_fired);
    lab_ck("error code: fire after clear -> CTO_ERR_CALLBACK",
           lab_rc == CTO_ERR_CALLBACK);

    /* ---- 4. function pointers ----------------------------------------- */
    lab_ck("function pointer arg: apply add -> 13",
           cto_apply_binary(6, 7, lab_add_op) == 13);
    lab_ck("function pointer arg: apply mul -> 42",
           cto_apply_binary(6, 7, lab_mul_op) == 42);
    lab_rc = cto_apply_binary(6, 7, (cto_binary_op)0);
    lab_ck("error code: apply(NULL op) -> CTO_ERR_NULL",
           lab_rc == CTO_ERR_NULL);

    /* ---- 5. opaque handles -------------------------------------------- */
    lab_store = cto_store_create(4u);
    lab_ck("opaque handle: create returns non-NULL",
           lab_store != (cto_store *)0);
    lab_ck("opaque handle: initial size 0", cto_store_size(lab_store) == 0u);
    lab_ck("opaque handle: put(2,99)",
           cto_store_put(lab_store, 2u, 99) == CTO_OK);
    lab_ck("opaque handle: size after put == 3",
           cto_store_size(lab_store) == 3u);
    lab_stored = 0;
    lab_rc = cto_store_get(lab_store, 2u, &lab_stored);
    lab_ck("opaque handle: get(2) -> 99",
           lab_rc == CTO_OK && lab_stored == 99);
    lab_rc = cto_store_get(lab_store, 3u, &lab_stored);
    lab_ck("error code: get out of range -> CTO_ERR_RANGE",
           lab_rc == CTO_ERR_RANGE);
    cto_store_destroy(lab_store);
    lab_ck("opaque handle: destroy completed", 1);

    /* ---- 6. memory allocated by C, released by C+ (same allocator) ---- */
    lab_rc = cto_make_int_buffer(4u, &lab_from_c);
    lab_ck("C->C+ memory: make buffer -> OK, len 4",
           lab_rc == CTO_OK && lab_from_c.nelems == 4u);
    lab_writer = lab_from_c.data;
    *lab_writer = 10;
    lab_writer++;
    *lab_writer = 20;
    lab_writer++;
    *lab_writer = 30;
    lab_writer++;
    *lab_writer = 40;
    lab_sum = 0;
    (void)cto_sum_ints(lab_from_c.data, lab_from_c.nelems, &lab_sum);
    lab_ck("C->C+ memory: C+ reads C buffer -> 100", lab_sum == 100);
    free(lab_from_c.data);          /* C+ releases C's host malloc block */
    lab_ck("C->C+ memory: C+ free() of C malloc block completed", 1);
    lab_rc = cto_make_int_buffer(4u, (cto_span *)0);
    lab_ck("error code: make buffer(NULL out) -> CTO_ERR_NULL",
           lab_rc == CTO_ERR_NULL);

    /* ---- 7. memory allocated by C+, released by C (same allocator) ---- */
    lab_owned = (int32_t *)malloc(4u * sizeof(int32_t));
    lab_ck("C+->C memory: host malloc non-NULL", lab_owned != (int32_t *)0);
    lab_writer = lab_owned;
    *lab_writer = 1;
    lab_writer++;
    *lab_writer = 2;
    lab_writer++;
    *lab_writer = 3;
    lab_writer++;
    *lab_writer = 4;
    lab_to_c.data = lab_owned;
    lab_to_c.nelems = 4u;
    lab_checksum = cto_consume_and_free_int_buffer(lab_to_c);
    lab_ck("C+->C memory: C consumed values, checksum 10, freed",
           lab_checksum == 10);

    /* ---- 8. variadic interface ---------------------------------------- */
    lab_ck("variadic: cto_vsum(5, 1..5) -> 15",
           cto_vsum(5, 1, 2, 3, 4, 5) == 15);
    lab_ck("variadic: cto_vsum(3, -1, 0, 1) -> 0",
           cto_vsum(3, -1, 0, 1) == 0);
    lab_ck("variadic: cto_vsum(0) -> 0", cto_vsum(0) == 0);

    printf("c_to_cp_caller: %d/%d calls match\n",
           lab_checks - lab_failures, lab_checks);
    return lab_failures == 0 ? 0 : 1;
}
