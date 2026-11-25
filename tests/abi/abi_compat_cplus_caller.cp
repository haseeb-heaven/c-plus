/* Test 3b + 4b: a C+ program calls into a precompiled C archive.
 *
 * Build and link (see run_abi_compat.sh):
 *   /usr/bin/clang -c abi_compat_c_part.c -o abi_compat_c_part.o
 *   /usr/bin/ar rcs libabi_compat_cpart.a abi_compat_c_part.o
 *   ./build/c+ build --sanitize=none abi_compat_cplus_caller.cp \
 *      -o build/abi/compat/cplus_caller -- libabi_compat_cpart.a
 *
 * The .a is a genuine object archive produced by the system C compiler; the
 * C+ driver's supported link path forwards it to the backend clang linker.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "abi_compat_shared.h"

class AbiMultiplier {
    int factor;
    int mul(int x) { return this->factor * x; }
};

class AbiRecordHolder {
    abi_compat_record value;
    int check() {
        if (this->value.i != -12345) return 1;
        if (this->value.u != 0xabcdefu) return 2;
        if (this->value.d != 3.5) return 3;
        if (this->value.p == (void *)0) return 4;
        if (this->value.n != 7u) return 5;
        return 0;
    }
};

static int g_checks;
static int g_failed;

static void ck(const char *name, int ok)
{
    ++g_checks;
    if (!ok)
        ++g_failed;
    printf("%-54s %s\n", name, ok ? "OK" : "MISMATCH");
}

int main(void)
{
    AbiMultiplier mul = {6};
    AbiRecordHolder holder;
    abi_compat_record r;
    abi_compat_record bad;

    memset(&r, 0, sizeof r);
    r.i = -12345;
    r.u = 0xabcdefu;
    r.d = 3.5;
    r.p = (void *)&r;
    r.n = 7u;

    holder.value = r;

    printf("abi_compat_cplus_caller: sizeof(record)=%llu align=%llu\n",
           (unsigned long long)sizeof(abi_compat_record),
           (unsigned long long)__alignof__(abi_compat_record));

    /* plain C function from the precompiled archive, called from C+ */
    ck("C+ -> C : precompiled .a symbol abi_c_mul reachable",
       abi_c_mul(20, 22) == 440);
    ck("C+ -> C : class method feeds plain C signature",
       abi_c_mul(mul.mul(7), 1) == 42);

    /* C+ class layout is still an ordinary C-compatible struct */
    ck("C+ class holding record keeps plain layout", holder.check() == 0);
    ck("C+ sees record sizeof == 32", sizeof(abi_compat_record) == 32u);
    ck("C+ sees record alignof == 8", __alignof__(abi_compat_record) == 8u);

    /* struct passed BY VALUE from C+ into C */
    ck("C+ -> C : struct BY VALUE accepted", abi_c_record_by_value(r) == 0);
    /* struct passed BY POINTER from C+ into C */
    ck("C+ -> C : struct BY POINTER accepted", abi_c_record_by_ptr(&r) == 0);
    ck("C+ -> C : NULL struct pointer refused", abi_c_record_by_ptr(NULL) == 7);

    bad = r;
    bad.u = 1u;
    ck("C+ -> C : bad by-value field detected",
       abi_c_record_by_value(bad) == 2);
    ck("C+ -> C : bad by-pointer field detected",
       abi_c_record_by_ptr(&bad) == 2);

    printf("abi_compat_cplus_caller: %d/%d C+->C boundary checks match\n",
           g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
