/*
 * The ONE original, ordinary ISO C source used by this ABI/source-compatibility
 * experiment.  It is compiled three times without a single character changing:
 *
 *   plain   : /usr/bin/clang, system headers, system libc
 *   safe    : /usr/bin/clang -include stdlib/safe/safe_c.h
 *             + build/safe/libcplus_safecrt.a + build/libc/libcplus_hardened.a
 *   disable : /usr/bin/clang -include stdlib/safe/safe_c.h
 *             -DCPLUS_SAFE_C_DISABLE   (declarations only, no macro rewriting)
 *
 * It deliberately contains NOTHING safe-layer-specific: no _s names, no
 * cplus_* names, no config macros, no #ifdef.  Every call is written in the
 * exact spelling a legacy C program would use.  If the checked ordinary-C layer
 * preserves the source-level ABI, all three builds emit byte-identical stdout.
 *
 * This proves SOURCE/ABI COMPATIBILITY ONLY.  Equality of output for these
 * bounded valid operations is NOT a claim that the layer is "safe", nor that it
 * rejects invalid input, nor that it provides pointer-provenance guarantees.
 * See README.md for the explicit non-claims.
 */
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* A plain user struct, plus plain libc ABI structs, are captured below so the
 * force-included header cannot silently perturb layouts. */
typedef struct {
    char c;
    int i;
    double d;
    void *p;
    short s;
    unsigned char tail[3];
} abi_sample;

static void print_bytes(const char *label, const unsigned char *p, size_t n)
{
    size_t i;
    printf("%s ", label);
    for (i = 0; i < n; ++i) {
        printf("%02x", (unsigned int)p[i]);
    }
    printf("\n");
}

/* memcmp's contract guarantees only the SIGN of the result; normalise it so
 * the two implementations (host libc vs safeclib) can be compared byte-for-byte
 * without depending on an unspecified magnitude. */
static int sign_of(int x)
{
    return (x < 0) ? -1 : (x > 0) ? 1 : 0;
}

int main(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : "abi_safe_layer_io.bin";

    /* ---- Section 1: sizes / layouts ------------------------------------ */
    printf("PTR_SIZE %zu\n", sizeof(void *));
    printf("LAYOUT void_ptr %zu %zu\n", sizeof(void *), (size_t)__alignof__(void *));
    printf("LAYOUT size_t %zu %zu\n", sizeof(size_t), (size_t)__alignof__(size_t));
    printf("LAYOUT FILE_ptr %zu %zu\n", sizeof(FILE *), (size_t)__alignof__(FILE *));
    printf("LAYOUT div_t %zu %zu\n", sizeof(div_t), (size_t)__alignof__(div_t));
    printf("LAYOUT time_t %zu %zu\n", sizeof(time_t), (size_t)__alignof__(time_t));
    printf("LAYOUT struct_tm %zu %zu\n", sizeof(struct tm), (size_t)__alignof__(struct tm));
    printf("LAYOUT abi_sample %zu %zu\n", sizeof(abi_sample), (size_t)__alignof__(abi_sample));
    printf("OFFSET abi_sample.c %zu\n", offsetof(abi_sample, c));
    printf("OFFSET abi_sample.i %zu\n", offsetof(abi_sample, i));
    printf("OFFSET abi_sample.d %zu\n", offsetof(abi_sample, d));
    printf("OFFSET abi_sample.p %zu\n", offsetof(abi_sample, p));
    printf("OFFSET abi_sample.s %zu\n", offsetof(abi_sample, s));
    printf("OFFSET abi_sample.tail %zu\n", offsetof(abi_sample, tail));

    /* ---- Section 2: malloc / calloc / realloc / free ------------------- */
    {
        unsigned char *a = (unsigned char *)malloc(16);
        unsigned char *b = (unsigned char *)calloc(16, 1);
        if (!a || !b) {
            printf("ALLOC_FAIL\n");
            return 1;
        }
        memset(a, 0x41, 16);
        print_bytes("MALLOC", a, 16);
        print_bytes("CALLOC", b, 16); /* must be all zeroes */

        memcpy(b, a, 8);
        print_bytes("MEMCPY", b, 16);

        a = (unsigned char *)realloc(a, 32);
        if (!a) {
            printf("REALLOC_GROW_FAIL\n");
            return 1;
        }
        memset(a + 16, 0x42, 16);
        print_bytes("REALLOC_GROW", a, 32);

        a = (unsigned char *)realloc(a, 8);
        if (!a) {
            printf("REALLOC_SHRINK_FAIL\n");
            return 1;
        }
        print_bytes("REALLOC_SHRINK", a, 8);

        memcpy(b, a, 8);
        memmove(b + 4, b, 8);
        print_bytes("MEMMOVE", b, 16);

        printf("MEMCMP_EQUAL %d\n", sign_of(memcmp(a, b, 8)));
        free(a);
        free(b);
    }

    /* ---- Section 3: stack memset + memcmp ------------------------------ */
    {
        unsigned char s1[12];
        unsigned char s2[12];
        memset(s1, 0x5a, 12);
        memset(s2, 0x5a, 12);
        s2[11] = 0x00;
        print_bytes("STACK_MEMSET", s1, 12);
        printf("STACK_MEMCMP_DIFF %d\n", sign_of(memcmp(s1, s2, 12)));
        printf("STACK_MEMCMP_SAME %d\n", sign_of(memcmp(s1, s1, 12)));
    }

    /* ---- Section 4: strcpy / strcat / strncpy / strncat / strlen/strnlen */
    {
        char d1[32] = {0};
        char d2[32] = {0};
        char d3[32] = {0};

        strcpy(d1, "alpha");
        printf("STRCPY %s len=%zu\n", d1, strlen(d1));
        strcat(d1, "-beta");
        printf("STRCAT %s len=%zu\n", d1, strlen(d1));

        /* n == strlen(src); src is shorter than the buffer.  Both the plain
         * strncpy and the checked strncpy_s wrapper yield "gamma" + NUL. */
        strncpy(d2, "gamma", 5);
        printf("STRNCPY %s len=%zu byte5=%02x\n",
               d2, strlen(d2), (unsigned int)(unsigned char)d2[5]);

        /* n == strlen(src); destination has ample capacity. */
        strncat(d2, "-delta", 6);
        printf("STRNCAT %s len=%zu\n", d2, strlen(d2));

        strcpy(d3, "hello");
        printf("STRLEN_STRLEN %zu\n", strlen(d3));
        printf("STRNLEN_BOUNDED %zu %zu %zu\n",
               strnlen(d3, 3), strnlen(d3, 5), strnlen(d3, 16));
    }

    /* ---- Section 5: fopen / fwrite / fread / fclose -------------------- */
    {
        const unsigned char payload[] = {0x00, 0x01, 'A', 'B', 'C', 0xff, 0x7f};
        unsigned char rb[sizeof(payload)];
        FILE *f = fopen(path, "wb");
        size_t nw, nr;
        int cw, cr;
        if (!f) {
            printf("FOPEN_W_FAIL\n");
            return 1;
        }
        nw = fwrite(payload, 1, sizeof(payload), f);
        printf("FWRITE %zu\n", nw);
        cw = fclose(f);
        printf("FCLOSE_W %d\n", cw);

        f = fopen(path, "rb");
        if (!f) {
            printf("FOPEN_R_FAIL\n");
            return 1;
        }
        nr = fread(rb, 1, sizeof(rb), f);
        printf("FREAD %zu\n", nr);
        cr = fclose(f);
        printf("FCLOSE_R %d\n", cr);
        print_bytes("FILE_PAYLOAD", rb, sizeof(rb));
    }

    printf("ABI_SAFE_LAYER_RESULT ok\n");
    return 0;
}
