/* Test 3b + 4b: an ordinary C object/archive compiled with /usr/bin/clang
 * and consumed by a C+ program.  No C+ headers or extensions are used here.
 */
#include <stddef.h>

#include "abi_compat_shared.h"

int abi_c_mul(int a, int b)
{
    return a * b;
}

static int record_ok(const abi_compat_record *r)
{
    if (r->i != -12345) return 1;
    if (r->u != 0xabcdefu) return 2;
    if (r->d != 3.5) return 3;
    if (r->p == (void *)0) return 4;
    if (r->n != 7u) return 5;
    return 0;
}

int abi_c_record_by_value(abi_compat_record r)
{
    abi_compat_record copy = r;
    if (copy.i != r.i || copy.u != r.u || copy.d != r.d ||
        copy.p != r.p || copy.n != r.n)
        return 6;
    return record_ok(&copy);
}

int abi_c_record_by_ptr(const abi_compat_record *r)
{
    if (r == (const abi_compat_record *)0)
        return 7;
    return record_ok(r);
}
