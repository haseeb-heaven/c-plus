/* Shared, deliberately plain-C declarations for the ABI compatibility tests.
 *
 * This header is consumed by BOTH a real C compiler (/usr/bin/clang) and the
 * C+ driver (./build/c+).  It contains no C+-only syntax, no C++ constructs and
 * no C+ headers: every type is an ordinary ISO C type and every function uses
 * an ordinary C signature.  That is the whole point of the boundary test.
 *
 * Layout contract for abi_compat_record (arm64-apple-darwin / LP64):
 *   sizeof = 32, alignof = 8
 *   i @ 0, u @ 4, d @ 8, p @ 16, n @ 24
 * The authoritative values are printed/asserted by abi_compat_layouts.c and
 * recorded in ABI_COMPAT_NOTES.md.
 */
#ifndef ABI_COMPAT_SHARED_H
#define ABI_COMPAT_SHARED_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* A C-compatible record exchanged by value AND by pointer across the
 * C <-> C+ boundary.  No C+ ownership annotations, no RAII, no metadata. */
typedef struct abi_compat_record {
    int32_t  i;
    uint32_t u;
    double   d;
    void    *p;
    size_t   n;
} abi_compat_record;

/* ---- implemented in C+ (abi_compat_cplus_lib.cp), called from C --------- */
int abi_cplus_add(int a, int b);
int abi_cplus_record_by_value(abi_compat_record r);
int abi_cplus_record_by_ptr(const abi_compat_record *r);
/* Fills an ordinary public C struct (cplus_alloc_meta, declared in the
 * installed C header) through a plain pointer.  out must have room for
 * sizeof(cplus_alloc_meta) bytes.  Returns 0 on success. */
int abi_cplus_meta_write(void *out, size_t out_size);

/* ---- implemented in C (abi_compat_c_part.c), called from C+ ------------ */
int abi_c_mul(int a, int b);
int abi_c_record_by_value(abi_compat_record r);
int abi_c_record_by_ptr(const abi_compat_record *r);

#ifdef __cplusplus
}
#endif

#endif /* ABI_COMPAT_SHARED_H */
