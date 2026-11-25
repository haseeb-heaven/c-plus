/* Test 3a + 4a: a REAL C compiler calls functions emitted by the C+ driver.
 *
 * Link line (see run_abi_compat.sh):
 *   /usr/bin/clang ... abi_compat_c_driver.c \
 *     build/abi/compat/abi_compat_cplus_lib.a \
 *     build/libc/libcplus_hardened.a -lm -pthread -o ...
 *
 * Every symbol below is declared with an ordinary C signature in the shared
 * header.  There are no C+ keywords, no mangled names and no compiler-specific
 * attributes in this translation unit.
 */
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "abi_compat_shared.h"
#include "cplus_libc.h"

static int g_checks;
static int g_failed;

static void ck(const char *name, int ok)
{
    ++g_checks;
    if (!ok)
        ++g_failed;
    printf("%-52s %s\n", name, ok ? "OK" : "MISMATCH");
}

int main(void)
{
    abi_compat_record r;
    abi_compat_record bad;
    unsigned char before[sizeof r];
    unsigned char after[sizeof r];
    cplus_alloc_meta meta;

    memset(&meta, 0, sizeof meta);
    memset(&r, 0, sizeof r);
    r.i = -12345;
    r.u = 0xabcdefu;
    r.d = 3.5;
    r.p = (void *)&r;
    r.n = 7u;

    printf("abi_compat_c_driver: sizeof(record)=%llu align=%llu "
           "sizeof(cplus_alloc_meta)=%llu\n",
           (unsigned long long)sizeof(abi_compat_record),
           (unsigned long long)__alignof__(abi_compat_record),
           (unsigned long long)sizeof(cplus_alloc_meta));

    /* ordinary C signature into C+-lowered code */
    ck("C -> C+ : abi_cplus_add(20,22) == 42", abi_cplus_add(20, 22) == 42);
    ck("C -> C+ : abi_cplus_add(-5,5) == 0", abi_cplus_add(-5, 5) == 0);

    /* struct by value */
    memcpy(before, &r, sizeof before);
    ck("C -> C+ : struct BY VALUE accepted", abi_cplus_record_by_value(r) == 0);
    memcpy(after, &r, sizeof after);
    ck("C -> C+ : caller's struct bytes unchanged by by-value call",
       memcmp(before, after, sizeof before) == 0);

    /* struct by pointer */
    ck("C -> C+ : struct BY POINTER accepted", abi_cplus_record_by_ptr(&r) == 0);
    ck("C -> C+ : NULL struct pointer refused", abi_cplus_record_by_ptr(NULL) == 7);

    bad = r;
    bad.d = 3.6;
    ck("C -> C+ : bad by-value field detected",
       abi_cplus_record_by_value(bad) == 3);
    ck("C -> C+ : bad by-pointer field detected",
       abi_cplus_record_by_ptr(&bad) == 3);

    /* public C struct written by C+ through a plain void pointer */
    ck("C -> C+ : metadata struct written through void*",
       abi_cplus_meta_write(&meta, sizeof meta) == 0);
    ck("C side sees metadata byte_size == 64", meta.byte_size == 64u);
    ck("C side sees metadata elem_count == 64", meta.elem_count == 64u);
    ck("C side sees metadata is_live == 1", meta.is_live == 1);
    ck("C side sees metadata generation != 0", meta.generation != 0ull);

    printf("abi_compat_c_driver: %d/%d C->C+ boundary checks match\n",
           g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
