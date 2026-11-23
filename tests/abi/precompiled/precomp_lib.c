/* The precompiled C library.
 *
 * Built ONCE by /usr/bin/clang into a single object file; that object is then
 * used to make both the static archive (libprecomp.a) and the shared dylib
 * (libprecomp.dylib).  Nothing in this translation unit depends on the C+
 * stdlib, so it never needs rebuilding when stdlib sources change.
 *
 * Only plain ISO C plus compiler builtins are used.  Unique precomp_ prefix
 * avoids collisions with libc / C+ runtime internals.
 */
#include "precomp_shared.h"

#define PRECOMP_LIBRARY_VERSION 3

int precomp_lib_version(void)
{
    return PRECOMP_LIBRARY_VERSION;
}

int precomp_mul(int a, int b)
{
    return a * b;
}

int precomp_checksum(const unsigned char *data, size_t len)
{
    int precomp_sum = 0;
    size_t precomp_idx;

    if (data == (const unsigned char *)0)
        return -1;
    for (precomp_idx = (size_t)0; precomp_idx < len; ++precomp_idx)
        precomp_sum = precomp_sum + (int)data[precomp_idx];
    return precomp_sum;
}

static int precomp_record_ok(const precomp_record *r)
{
    if (r->i != -12345) return 1;
    if (r->u != 0xabcdefu) return 2;
    if (r->d != 3.5) return 3;
    if (r->p == (void *)0) return 4;
    if (r->n != (size_t)7) return 5;
    return 0;
}

void precomp_fill_record(precomp_record *out)
{
    if (out == (precomp_record *)0)
        return;
    out->i = -12345;
    out->u = 0xabcdefu;
    out->d = 3.5;
    out->p = (void *)out;
    out->n = (size_t)7;
}

int precomp_verify_record_by_value(precomp_record r)
{
    precomp_record precomp_copy;
    __builtin_memcpy(&precomp_copy, &r, sizeof precomp_copy);
    if (precomp_copy.i != r.i || precomp_copy.u != r.u ||
        precomp_copy.d != r.d || precomp_copy.p != r.p ||
        precomp_copy.n != r.n)
        return 6;
    return precomp_record_ok(&precomp_copy);
}

int precomp_verify_record_by_ptr(const precomp_record *r)
{
    if (r == (const precomp_record *)0)
        return 7;
    return precomp_record_ok(r);
}

size_t precomp_record_sizeof(void)
{
    return sizeof(precomp_record);
}

size_t precomp_record_alignof(void)
{
    return __alignof__(precomp_record);
}

size_t precomp_offset_i(void)
{
    return offsetof(precomp_record, i);
}

size_t precomp_offset_u(void)
{
    return offsetof(precomp_record, u);
}

size_t precomp_offset_d(void)
{
    return offsetof(precomp_record, d);
}

size_t precomp_offset_p(void)
{
    return offsetof(precomp_record, p);
}

size_t precomp_offset_n(void)
{
    return offsetof(precomp_record, n);
}
