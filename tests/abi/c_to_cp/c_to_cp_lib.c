/* c_to_cp_lib.c -- the "C" side of the lab.
 *
 * Compiled ONLY with /usr/bin/clang (never with ./build/c+).  It has no C+
 * headers and no C+ extensions.  It is archived with /usr/bin/ar into a real
 * static archive that the C+ driver links via its supported `-- <archive>`
 * forwarding path.
 */
#include "c_to_cp_shared.h"

#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ---- internal opaque-handle definition (incomplete in the shared header) */
struct cto_store {
    size_t   capacity;
    size_t   used;
    int32_t *slots;
};

/* ---- internal callback state ------------------------------------------- */
static cto_event_cb lab_cb = (cto_event_cb)0;
static void        *lab_cb_ctx = (void *)0;

/* ---- struct arguments / returns ---------------------------------------- */
cto_point cto_point_translate(cto_point p, int32_t dx, int32_t dy)
{
    cto_point out;
    out.x = p.x + dx;
    out.y = p.y + dy;
    return out;
}

cto_rect cto_rect_from_points(cto_point a, cto_point b)
{
    cto_rect out;
    out.lo = a;
    out.hi = b;
    return out;
}

int32_t cto_rect_area(const cto_rect *rect, int32_t *out)
{
    int32_t lab_w;
    int32_t lab_h;
    if (rect == (const cto_rect *)0 || out == (int32_t *)0)
        return CTO_ERR_NULL;
    lab_w = rect->hi.x - rect->lo.x;
    lab_h = rect->hi.y - rect->lo.y;
    if (lab_w < 0 || lab_h < 0)
        return CTO_ERR_RANGE;
    *out = lab_w * lab_h;
    return CTO_OK;
}

/* ---- pointer arguments -------------------------------------------------- */
int32_t cto_sum_ints(const int32_t *src, size_t n, int32_t *out)
{
    size_t   lab_i;
    int32_t  lab_total = 0;
    if (src == (const int32_t *)0 || out == (int32_t *)0)
        return CTO_ERR_NULL;
    for (lab_i = 0; lab_i < n; ++lab_i)
        lab_total += src[lab_i];
    *out = lab_total;
    return CTO_OK;
}

int32_t cto_scale_in_place(int32_t *src, size_t n, int32_t factor)
{
    size_t lab_i;
    if (src == (int32_t *)0)
        return CTO_ERR_NULL;
    for (lab_i = 0; lab_i < n; ++lab_i)
        src[lab_i] = src[lab_i] * factor;
    return CTO_OK;
}

/* ---- callback registration --------------------------------------------- */
int32_t cto_register_callback(cto_event_cb cb, void *ctx)
{
    if (cb == (cto_event_cb)0)
        return CTO_ERR_NULL;
    lab_cb = cb;
    lab_cb_ctx = ctx;
    return CTO_OK;
}

int32_t cto_fire_callback(int32_t value, int32_t *out)
{
    if (out == (int32_t *)0)
        return CTO_ERR_NULL;
    if (lab_cb == (cto_event_cb)0)
        return CTO_ERR_CALLBACK;
    *out = lab_cb(value, lab_cb_ctx);
    return CTO_OK;
}

int32_t cto_clear_callback(void)
{
    lab_cb = (cto_event_cb)0;
    lab_cb_ctx = (void *)0;
    return CTO_OK;
}

/* ---- function pointer passed as an argument ---------------------------- */
int32_t cto_apply_binary(int32_t a, int32_t b, cto_binary_op op)
{
    if (op == (cto_binary_op)0)
        return CTO_ERR_NULL;
    return op(a, b);
}

/* ---- opaque handle API -------------------------------------------------- */
cto_store *cto_store_create(size_t capacity)
{
    cto_store *lab_s;
    if (capacity == 0u)
        return (cto_store *)0;
    lab_s = (cto_store *)malloc(sizeof(cto_store));
    if (lab_s == (cto_store *)0)
        return (cto_store *)0;
    lab_s->slots = (int32_t *)malloc(capacity * sizeof(int32_t));
    if (lab_s->slots == (int32_t *)0) {
        free(lab_s);
        return (cto_store *)0;
    }
    memset(lab_s->slots, 0, capacity * sizeof(int32_t));
    lab_s->capacity = capacity;
    lab_s->used = 0u;
    return lab_s;
}

int32_t cto_store_put(cto_store *store, size_t index, int32_t value)
{
    if (store == (cto_store *)0)
        return CTO_ERR_NULL;
    if (index >= store->capacity)
        return CTO_ERR_RANGE;
    store->slots[index] = value;
    if (index >= store->used)
        store->used = index + 1u;
    return CTO_OK;
}

int32_t cto_store_get(cto_store *store, size_t index, int32_t *out)
{
    if (store == (cto_store *)0 || out == (int32_t *)0)
        return CTO_ERR_NULL;
    if (index >= store->used)
        return CTO_ERR_RANGE;
    *out = store->slots[index];
    return CTO_OK;
}

size_t cto_store_size(const cto_store *store)
{
    if (store == (cto_store *)0)
        return 0u;
    return store->used;
}

void cto_store_destroy(cto_store *store)
{
    if (store == (cto_store *)0)
        return;
    free(store->slots);
    free(store);
}

/* ---- memory allocated by C, released by C+ (same host allocator) ------- */
int32_t cto_make_int_buffer(size_t n, cto_span *out)
{
    if (out == (cto_span *)0)
        return CTO_ERR_NULL;
    out->data = (int32_t *)malloc(n * sizeof(int32_t));
    if (out->data == (int32_t *)0) {
        out->nelems = 0u;
        return CTO_ERR_NOMEM;
    }
    out->nelems = n;
    return CTO_OK;
}

/* ---- memory allocated by C+ (host allocator), released by C ------------ */
int32_t cto_consume_and_free_int_buffer(cto_span span)
{
    size_t  lab_i;
    int32_t lab_total = 0;
    for (lab_i = 0u; lab_i < span.nelems; ++lab_i)
        lab_total += span.data[lab_i];
    free(span.data);   /* SAME allocator as the caller's malloc() */
    return lab_total;
}

/* ---- variadic interface ------------------------------------------------- */
int32_t cto_vsum(int32_t count, ...)
{
    va_list lab_ap;
    int32_t lab_total = 0;
    int32_t lab_i;
    va_start(lab_ap, count);
    for (lab_i = 0; lab_i < count; ++lab_i)
        lab_total += va_arg(lab_ap, int32_t);
    va_end(lab_ap);
    return lab_total;
}
