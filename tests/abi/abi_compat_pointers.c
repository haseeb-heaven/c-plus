/* Test 1: ordinary C pointer size and representation equality.
 *
 * Built and run with the REAL C compiler, never by source inspection:
 *   /usr/bin/clang -O1 -std=c99 -Wall -Wextra -pedantic \
 *     tests/abi/abi_compat_pointers.c -o build/abi/compat/pointers
 *
 * What is proven here:
 *   - data pointers and function pointers are ordinary flat 8-byte values on
 *     this target (no fat pointers, capabilities or compiler-added tags);
 *   - copying the bytes of a pointer into uintptr_t reproduces its numeric
 *     address, and copying them into a same-sized void * and back preserves
 *     the exact bit pattern AND pointer equality/callability;
 *   - the same round-trip works for a function pointer, so the two pointer
 *     classes have interchangeable *representations* (we never rely on a C
 *     cast between them - memcpy does not convert anything).
 *
 * The golden constants are for arm64-apple-darwin (LP64).  They are captured
 * from the toolchain, not invented; see ABI_COMPAT_NOTES.md.
 */
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define ABI_EXPECT_PTR_BYTES 8u

static int g_checks;
static int g_failed;

static void ck(const char *name, int ok)
{
    ++g_checks;
    if (!ok)
        ++g_failed;
    printf("%-46s %s\n", name, ok ? "OK" : "MISMATCH");
}

static int add_one(int x) { return x + 1; }

int main(void)
{
    int object = 7;
    void *data = (void *)&object;
    int (*fn)(int) = &add_one;
    void (*fn0)(void) = (void (*)(void))0; /* representation only, never called */

    uintptr_t data_bits = 0;
    uintptr_t fn_bits = 0;
    void *data_roundtrip = NULL;
    void *fn_as_void = NULL;
    int (*fn_roundtrip)(int) = NULL;
    unsigned char data_raw[sizeof(void *)];
    unsigned char int_raw[sizeof(uintptr_t)];
    unsigned char fn_raw[sizeof(int (*)(int))];

    printf("abi_compat_pointers: sizeof(void*)=%u sizeof(fn*)=%u "
           "sizeof(uintptr_t)=%u (arm64 LP64 baseline)\n",
           (unsigned)sizeof(void *), (unsigned)sizeof(int (*)(int)),
           (unsigned)sizeof(uintptr_t));

    /* --- sizes / alignment ------------------------------------------------ */
    ck("sizeof(void *) == 8", sizeof(void *) == ABI_EXPECT_PTR_BYTES);
    ck("sizeof(char *) == sizeof(void *)", sizeof(char *) == sizeof(void *));
    ck("sizeof(int *) == sizeof(void *)", sizeof(int *) == sizeof(void *));
    ck("sizeof(int (*)(int)) == 8", sizeof(int (*)(int)) == ABI_EXPECT_PTR_BYTES);
    ck("sizeof(void (*)(void)) == sizeof(fn*)",
       sizeof(void (*)(void)) == sizeof(int (*)(int)));
    ck("sizeof(uintptr_t) == sizeof(void *)",
       sizeof(uintptr_t) == sizeof(void *));
    ck("sizeof(size_t) == sizeof(void *)", sizeof(size_t) == sizeof(void *));
    ck("alignof(void *) == 8", __alignof__(void *) == ABI_EXPECT_PTR_BYTES);
    ck("alignof(fn*) == alignof(void *)",
       __alignof__(int (*)(int)) == __alignof__(void *));

    /* --- data pointer representation ------------------------------------- */
    memcpy(&data_bits, &data, sizeof data_bits);
    memcpy(&data_raw, &data, sizeof data_raw);
    memcpy(&int_raw, &data_bits, sizeof int_raw);
    ck("data pointer bytes == (uintptr_t) address",
       data_bits == (uintptr_t)data);
    ck("data pointer bytes bit-identical to uintptr_t bytes",
       memcmp(data_raw, int_raw, sizeof data_raw) == 0);

    memcpy(&data_roundtrip, &data_bits, sizeof data_roundtrip);
    ck("data pointer memcpy round-trip preserves equality",
       data_roundtrip == data);
    ck("dereference through round-tripped data pointer",
       *(int *)data_roundtrip == 7);

    /* --- function pointer representation --------------------------------- */
    memcpy(&fn_bits, &fn, sizeof fn_bits);
    memcpy(&fn_raw, &fn, sizeof fn_raw);
    ck("function pointer representation is non-null", fn_bits != 0);
    ck("function pointer fits uintptr_t without truncation",
       sizeof fn == sizeof fn_bits);

    memcpy(&fn_as_void, &fn, sizeof fn_as_void);
    memcpy(&fn_roundtrip, &fn_as_void, sizeof fn_roundtrip);
    ck("fn* -> 8-byte void* -> fn* preserves equality", fn_roundtrip == fn);
    ck("round-tripped function pointer is callable", fn_roundtrip(41) == 42);

    {
        unsigned char via_void[sizeof fn];
        memcpy(via_void, &fn_as_void, sizeof via_void);
        ck("function pointer bytes unchanged by void* staging",
           memcmp(via_void, fn_raw, sizeof fn_raw) == 0);
    }

    /* --- null / arithmetic-neutral representation ------------------------ */
    {
        uintptr_t null_bits = (uintptr_t)123;
        void *nullp = data;
        memcpy(&null_bits, &fn0, sizeof null_bits);
        ck("null function pointer representation is all-zero bits",
           null_bits == 0);
        memset(&nullp, 0, sizeof nullp);
        ck("all-zero bytes form a null data pointer", nullp == NULL);
    }

    printf("abi_compat_pointers: %d/%d pointer ABI checks match\n",
           g_checks - g_failed, g_checks);
    return g_failed == 0 ? 0 : 1;
}
