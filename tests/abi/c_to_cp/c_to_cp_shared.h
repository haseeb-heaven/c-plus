/* c_to_cp_shared.h -- plain-C declarations shared by /usr/bin/clang and ./build/c+.
 *
 * This header is the ABI boundary contract of the C-to-C+ interoperability lab.
 * It uses ONLY ordinary ISO C types and signatures: no C+ extensions, no C++
 * constructs, no cplus headers.  Both the C library (c_to_cp_lib.c, compiled by
 * clang into a real object archive) and the C+ caller (c_to_cp_caller.cp,
 * compiled by ./build/c+) include this same file.
 *
 * Allocator boundary note:
 *   cto_span carries a host-C-allocator (malloc/free) buffer.  Memory crossing
 *   this header MUST be allocated and released by the SAME allocator.  The
 *   C+ managed allocator (cplus_alloc/cplus_release / stdlib_alloc) and the
 *   hardened libc allocator (cplus_malloc/cplus_free) are SEPARATE allocators
 *   with independent metadata; they must never be mixed.  See README.md,
 *   section "UNSUPPORTED".
 */
#ifndef CTO_LAB_SHARED_H
#define CTO_LAB_SHARED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- error codes (returned by int32_t functions) ------------------------ */
#define CTO_OK           0
#define CTO_ERR_NULL     (-1)
#define CTO_ERR_RANGE    (-2)
#define CTO_ERR_NOMEM    (-3)
#define CTO_ERR_STATE    (-4)
#define CTO_ERR_CALLBACK (-5)

/* ---- C-compatible records ---------------------------------------------- */
typedef struct cto_point {
    int32_t x;
    int32_t y;
} cto_point;

typedef struct cto_rect {
    cto_point lo;
    cto_point hi;
} cto_rect;

/* A host-allocator buffer plus its element count.  data is owned by whoever
 * receives it; release with free() (the same allocator that produced it). */
typedef struct cto_span {
    int32_t *data;
    size_t   nelems;
} cto_span;

/* ---- opaque handle ------------------------------------------------------ */
typedef struct cto_store cto_store;   /* definition hidden from callers */

/* ---- function-pointer types -------------------------------------------- */
typedef int32_t (*cto_binary_op)(int32_t, int32_t);
typedef int32_t (*cto_event_cb)(int32_t value, void *ctx);

/* ---- struct arguments / returns ---------------------------------------- */
cto_point cto_point_translate(cto_point p, int32_t dx, int32_t dy);
cto_rect  cto_rect_from_points(cto_point a, cto_point b);
int32_t   cto_rect_area(const cto_rect *rect, int32_t *out);

/* ---- pointer arguments -------------------------------------------------- */
int32_t cto_sum_ints(const int32_t *src, size_t n, int32_t *out);
int32_t cto_scale_in_place(int32_t *src, size_t n, int32_t factor);

/* ---- callback registration --------------------------------------------- */
int32_t cto_register_callback(cto_event_cb cb, void *ctx);
int32_t cto_fire_callback(int32_t value, int32_t *out);
int32_t cto_clear_callback(void);

/* ---- function pointer passed as an argument ---------------------------- */
int32_t cto_apply_binary(int32_t a, int32_t b, cto_binary_op op);

/* ---- opaque handle API -------------------------------------------------- */
cto_store *cto_store_create(size_t capacity);
int32_t    cto_store_put(cto_store *store, size_t index, int32_t value);
int32_t    cto_store_get(cto_store *store, size_t index, int32_t *out);
size_t     cto_store_size(const cto_store *store);
void       cto_store_destroy(cto_store *store);

/* ---- memory allocated by C, released by C+ (same host allocator) ------- */
int32_t cto_make_int_buffer(size_t n, cto_span *out);

/* ---- memory allocated by C+ (host allocator), released by C ------------ */
/* Returns the checksum C observed before releasing the buffer. */
int32_t cto_consume_and_free_int_buffer(cto_span span);

/* ---- variadic interface ------------------------------------------------- */
int32_t cto_vsum(int32_t count, ...);

#ifdef __cplusplus
}
#endif

#endif /* CTO_LAB_SHARED_H */
