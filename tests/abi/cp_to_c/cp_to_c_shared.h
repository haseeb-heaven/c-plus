/* Plain-C declarations shared by the C+ library (cp_to_c_lib.cp) and the
 * ordinary C driver (cp_to_c_driver.c).
 *
 * This header is consumed by BOTH ./build/c+ (strict mode) and /usr/bin/clang.
 * It deliberately contains no C+ syntax, no C++ constructs and no C+ headers:
 * every type is an ordinary ISO C type, every function has an ordinary C
 * signature.  The whole point of the lab is that the two compilers agree on
 * the boundary described here.
 *
 * Target layout (arm64-apple-darwin, LP64) for cp_to_c_record:
 *   sizeof = 48, alignof = 8
 *   i @ 0, u @ 4, d @ 8, p @ 16, n @ 24, q @ 32, tag @ 40
 * The authoritative values are printed by both sides and compared by the
 * driver; cp_to_c_layout_report() returns the C+ side's view.
 */
#ifndef CP_TO_C_SHARED_H
#define CP_TO_C_SHARED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A C-compatible record exchanged by value AND by pointer.  No C+ ownership
 * annotations, no RAII metadata, no destructors: raw ISO C layout. */
typedef struct cp_to_c_record {
    int32_t       i;
    uint32_t      u;
    double        d;
    void         *p;
    size_t        n;
    int64_t       q;
    unsigned char tag[3];
} cp_to_c_record;

/* Filled in by C+ (cp_to_c_layout_report) with the C+ compiler's view of the
 * public record layout and pointer width.  The C driver compares every field
 * with its own sizeof/alignof/offsetof values. */
typedef struct cp_to_c_layout {
    size_t record_size;
    size_t record_align;
    size_t off_i;
    size_t off_u;
    size_t off_d;
    size_t off_p;
    size_t off_n;
    size_t off_q;
    size_t off_tag;
    size_t pointer_size;
} cp_to_c_layout;

/* Stable error codes.  A non-zero return always identifies the failing
 * condition; codes round-trip unchanged across the boundary. */
#define CP_TO_C_OK             0
#define CP_TO_C_ERR_NULL        1
#define CP_TO_C_ERR_SIZE        2
#define CP_TO_C_ERR_RECORD_I    3
#define CP_TO_C_ERR_RECORD_U    4
#define CP_TO_C_ERR_RECORD_D    5
#define CP_TO_C_ERR_RECORD_P    6
#define CP_TO_C_ERR_RECORD_N    7
#define CP_TO_C_ERR_RECORD_Q    8
#define CP_TO_C_ERR_RECORD_TAG  9
#define CP_TO_C_ERR_RANGE      10
#define CP_TO_C_ERR_META       11
#define CP_TO_C_ERR_FREE       12
#define CP_TO_C_ERR_ARG_A      20
#define CP_TO_C_ERR_ARG_B      21
#define CP_TO_C_ERR_ARG_P      22
#define CP_TO_C_ERR_ARG_C      23
#define CP_TO_C_ERR_ARG_D      24
#define CP_TO_C_ERR_ARG_E      25
#define CP_TO_C_ERR_ARG_F      26

/* ---- implemented in C+ (cp_to_c_lib.cp), called from ordinary C ---------- */

/* Class-lowered addition: CpAdder::apply is exported as plain symbol
 * CpAdder_apply; this is the public plain-C entry point. */
int  cp_to_c_add(int32_t a, int32_t b);

/* Seven mixed arguments (int, double, pointer, int, double, size_t, int) to
 * exercise the platform calling convention and argument ordering. */
int  cp_to_c_mixcall(int32_t a, double b, const void *p,
                     int32_t c, double d, size_t e, int32_t f);

/* Struct-by-value and struct-by-pointer entry points. */
int  cp_to_c_record_by_value(cp_to_c_record r);
int  cp_to_c_record_by_ptr(const cp_to_c_record *r);

/* Writes the exact bit pattern of a const void* argument into `out`.
 * Proves 8-byte pointer representation is preserved in both directions. */
int  cp_to_c_ptr_roundtrip(const void *p, void *out, size_t out_size);

/* C+ compiler's view of the shared record layout. */
int  cp_to_c_layout_report(cp_to_c_layout *out);

/* Pointer-parameter element access: sum n doubles through __builtin_memcpy
 * in a lowered class method and scale by factor. */
int  cp_to_c_scale_sum(const double *p, size_t n, double factor, double *out);

/* Ownership contract: C+ allocates a tracked buffer and transfers ownership
 * to C as an opaque handle.  C must return it through destroy(), which clears
 * the caller's pointer. */
void *cp_to_c_buffer_create(size_t elem_count);
int  cp_to_c_buffer_write(void *h, size_t idx, int32_t value);
int  cp_to_c_buffer_read(const void *h, size_t idx, int32_t *out);
int  cp_to_c_buffer_meta(const void *h, size_t *out_byte_size,
                         size_t *out_elem_count);
int  cp_to_c_buffer_destroy(void **h_inout);

/* Error-code identity: returns `code` unchanged. */
int  cp_to_c_echo_err(int code);

#ifdef __cplusplus
}
#endif

#endif /* CP_TO_C_SHARED_H */
