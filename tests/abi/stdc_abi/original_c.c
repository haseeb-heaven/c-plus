/* original_c.c -- ONE ordinary ISO C translation unit used to prove that the
 * C+ "safe C" layer does not change the standard C runtime ABI.
 *
 * This same file is compiled three ways (see run.sh):
 *   1. PLAIN     : /usr/bin/clang -fno-builtin -O0 -std=c11 ...
 *   2. SAFE      : same plus -include stdlib/safe/safe_c.h + both archives
 *   3. DISABLED  : same plus -DCPLUS_SAFE_C_DISABLE (header is neutral)
 *
 * The functions below use ordinary, un-mangled C spellings and ordinary C
 * library calls.  In the SAFE build those library calls are remapped by
 * caller-side macros; the *definitions* of the abi_* functions, their names,
 * signatures and the layout of abi_sample must be byte-for-byte identical.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- the struct whose layout must be identical in every build ---------- */
typedef struct abi_sample {
    unsigned char tag;     /* offset  0 */
    int           count;   /* offset  4 */
    double        value;   /* offset  8 */
    void         *ptr;     /* offset 16 */
    char          name[8]; /* offset 24 */
    uint64_t      big;     /* offset 32 */
} abi_sample;

/* ---- compile-time ABI assertions (active in ALL three builds) --------- */
_Static_assert(sizeof(void *) == 8, "ABI: sizeof(void*) must be 8");
_Static_assert(sizeof(size_t) == 8, "ABI: sizeof(size_t) must be 8");
_Static_assert(sizeof(abi_sample) == 40, "ABI: sizeof(abi_sample) must be 40");
_Static_assert(_Alignof(abi_sample) == 8, "ABI: alignof(abi_sample) must be 8");
_Static_assert(offsetof(abi_sample, tag)   == 0,  "ABI: tag offset");
_Static_assert(offsetof(abi_sample, count) == 4,  "ABI: count offset");
_Static_assert(offsetof(abi_sample, value) == 8,  "ABI: value offset");
_Static_assert(offsetof(abi_sample, ptr)   == 16, "ABI: ptr offset");
_Static_assert(offsetof(abi_sample, name)  == 24, "ABI: name offset");
_Static_assert(offsetof(abi_sample, big)   == 32, "ABI: big offset");

/* ---- ordinary external C linkage + compile-time signature asserts ------ */
int    abi_add(int a, int b);
size_t abi_sum(const int *a, size_t n);
void   abi_fill(abi_sample *s, int c, double v, void *p);
int    abi_roundtrip(const char *src, size_t n);
extern int abi_glob;

_Static_assert(__builtin_types_compatible_p(__typeof__(&abi_add),
                                            int (*)(int, int)),
               "ABI: abi_add signature must be int(int,int)");
_Static_assert(__builtin_types_compatible_p(__typeof__(&abi_sum),
                                            size_t (*)(const int *, size_t)),
               "ABI: abi_sum signature must be size_t(const int*,size_t)");
_Static_assert(__builtin_types_compatible_p(__typeof__(&abi_fill),
                                            void (*)(abi_sample *, int, double, void *)),
               "ABI: abi_fill signature must be void(abi_sample*,int,double,void*)");
_Static_assert(__builtin_types_compatible_p(__typeof__(&abi_roundtrip),
                                            int (*)(const char *, size_t)),
               "ABI: abi_roundtrip signature must be int(const char*,size_t)");
_Static_assert(__builtin_types_compatible_p(__typeof__(&abi_glob), int *),
               "ABI: abi_glob must have type int");

/* ---- definitions: identical object code shape in all builds ------------ */
int abi_glob = 7;

int abi_add(int a, int b)
{
    return a + b;
}

size_t abi_sum(const int *a, size_t n)
{
    size_t s = 0;
    for (size_t i = 0; i < n; ++i) {
        s += (size_t)a[i];
    }
    return s;
}

void abi_fill(abi_sample *s, int c, double v, void *p)
{
    s->tag   = (unsigned char)c;
    s->count = c;
    s->value = v;
    s->ptr   = p;
}

/* Uses malloc/memset/memcpy/memcmp/free: these are exactly the identifiers
 * that safe_c.h remaps on the caller side.  The function's own name,
 * signature and calling convention stay untouched. */
int abi_roundtrip(const char *src, size_t n)
{
    unsigned char *buf = (unsigned char *)malloc(n);
    if (!buf) {
        return -1;
    }
    memset(buf, 0, n);
    size_t m = (n > 8) ? 8 : n;
    memcpy(buf, src, m);
    int r = memcmp(buf, src, m);
    free(buf);
    return r;
}

int main(void)
{
    int a[3] = {1, 2, 3};
    abi_sample s;
    abi_fill(&s, 7, 2.5, (void *)a);

    printf("sizeof(void*)=%zu\n", sizeof(void *));
    printf("sizeof(abi_sample)=%zu\n", sizeof(abi_sample));
    printf("offsets tag=%zu count=%zu value=%zu ptr=%zu name=%zu big=%zu\n",
           offsetof(abi_sample, tag),   offsetof(abi_sample, count),
           offsetof(abi_sample, value), offsetof(abi_sample, ptr),
           offsetof(abi_sample, name),  offsetof(abi_sample, big));
    printf("abi_add=%d abi_sum=%zu abi_glob=%d\n",
           abi_add(2, 3), abi_sum(a, 3), abi_glob);
    printf("abi_roundtrip=%d\n", abi_roundtrip("abcdefghij", 10));
    return 0;
}
