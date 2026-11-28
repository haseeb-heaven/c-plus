/* Plain-C ABI contract shared by the precompiled C library and the C+ app.
 *
 * This header is consumed by BOTH /usr/bin/clang (library side) and the C+
 * driver (app side).  It contains no C+-only syntax, no C++ constructs and no
 * C+ headers: every type is an ordinary ISO C type and every function uses an
 * ordinary C signature.  That is the whole point of the lab: a binary built by
 * the system C compiler must remain usable by a C+ translation unit that may
 * be rebuilt at any time after stdlib source changes.
 *
 * Layout contract for precomp_record (arm64-apple-darwin / LP64):
 *   sizeof = 32, alignof = 8
 *   i @ 0, u @ 4, d @ 8, p @ 16, n @ 24
 * The C library reports its own view of these constants through
 * precomp_record_sizeof() / precomp_record_alignof() / precomp_offset_*();
 * the C+ app cross-checks those against its own sizeof/__alignof__ and against
 * the field values it reads back after precomp_fill_record().
 */
#ifndef PRECOMP_SHARED_H
#define PRECOMP_SHARED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Plain C record exchanged by value AND by pointer across the boundary.
 * No ownership annotations, no RAII, no metadata. */
typedef struct precomp_record {
    int32_t  i;
    uint32_t u;
    double   d;
    void    *p;
    size_t   n;
} precomp_record;

/* ---- implemented in precomp_lib.c (built ONCE, by /usr/bin/clang) -------- */
int    precomp_lib_version(void);
int    precomp_mul(int a, int b);
int    precomp_checksum(const unsigned char *data, size_t len);
void   precomp_fill_record(precomp_record *out);
int    precomp_verify_record_by_value(precomp_record r);
int    precomp_verify_record_by_ptr(const precomp_record *r);
size_t precomp_record_sizeof(void);
size_t precomp_record_alignof(void);
size_t precomp_offset_i(void);
size_t precomp_offset_u(void);
size_t precomp_offset_d(void);
size_t precomp_offset_p(void);
size_t precomp_offset_n(void);

#ifdef __cplusplus
}
#endif

#endif /* PRECOMP_SHARED_H */
