/* C+ implementation of functions called from an ORDINARY C translation unit.
 *
 * This really is a C+ source file (.cp) lowered by ./build/c+, not a .c file
 * renamed for show.  The AbiAdder class exercises C+ lowering (class -> struct
 * + ordinary function) while every exported entry point keeps a plain C
 * signature declared in abi_compat_shared.h.  The resulting object/archive is
 * linked into a /usr/bin/clang-compiled C program by the real linker.
 */
#include "abi_compat_shared.h"
#include "cplus_libc.h"

class AbiAdder {
    int base;
    int apply(int x) {
        return this->base + x;
    }
};

int abi_cplus_add(int a, int b)
{
    AbiAdder adder = {a};
    return adder.apply(b);
}

static int record_ok(const abi_compat_record *r)
{
    if (r->i != -12345)
        return 1;
    if (r->u != 0xabcdefu)
        return 2;
    if (r->d != 3.5)
        return 3;
    if (r->p == (void *)0)
        return 4;
    if (r->n != 7u)
        return 5;
    return 0;
}

int abi_cplus_record_by_value(abi_compat_record r)
{
    /* Passing by value must produce a byte-identical local copy. */
    abi_compat_record copy = r;
    if (copy.i != r.i || copy.u != r.u || copy.d != r.d ||
        copy.p != r.p || copy.n != r.n)
        return 6;
    return record_ok(&copy);
}

int abi_cplus_record_by_ptr(const abi_compat_record *r)
{
    if (r == (const abi_compat_record *)0)
        return 7;
    return record_ok(r);
}

int abi_cplus_meta_write(void *out, size_t out_size)
{
    cplus_alloc_meta *m;
    void *p;
    size_t remaining;
    int ok;

    if (out == (void *)0 || out_size < sizeof(cplus_alloc_meta))
        return 10;
    m = (cplus_alloc_meta *)out;
    p = cplus_malloc(64u);
    if (p == (void *)0)
        return 11;
    if (cplus_alloc_get_meta(p, m) != 0) {
        cplus_free(p);
        return 12;
    }
    ok = m->byte_size == 64u && m->elem_size == 1u &&
         m->elem_count == 64u && m->generation != 0ull && m->is_live;
    remaining = cplus_alloc_remaining_capacity(p);
    cplus_free(p);
    if (!ok)
        return 13;
    if (remaining != 64u)
        return 14;
    return 0;
}
