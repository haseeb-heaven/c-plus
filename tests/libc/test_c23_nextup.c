/*
 * test_c23_nextup.c -- deterministic bit-pattern tests for the six bare ISO
 * C23 nextup/nextdown APIs declared in cplus_c23_nextup.h and defined in
 * libc/src/math/c23_nextup.c.
 *
 * Every expected result is a hardcoded bit pattern (IEEE-754 binary64 /
 * binary32, little-endian host order) compared with the produced value via
 * memcpy. Cases cover +/-0, 1.0 and -1.0, least/largest subnormals, the
 * subnormal/normal boundary, +/-TYPE_MAX, +/-INFINITY and a quiet NaN for
 * all three types. Six supplementary checks confirm the nextafter-based
 * delegation on a non-anchor value.
 *
 * Output: "c23_nextup: <checks> <failures>"; exit status is nonzero on any
 * failure. Exact compile/run commands and captured output are recorded in
 * docs/libc/C23_NEXTUP.md.
 */

#include <float.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_c23_nextup.h"

static int g_checks = 0;
static int g_failures = 0;

#define CHECK_BITS(label, got, want)                                            \
    do {                                                                        \
        ++g_checks;                                                             \
        if ((got) != (want)) {                                                  \
            ++g_failures;                                                       \
            fprintf(stderr, "FAIL line %d: %s got=0x%llx want=0x%llx\n",       \
                    __LINE__, (label), (unsigned long long)(got),               \
                    (unsigned long long)(want));                                \
        }                                                                       \
    } while (0)

static uint64_t bits_d(double v) {
    uint64_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

static uint32_t bits_f(float v) {
    uint32_t u;
    memcpy(&u, &v, sizeof u);
    return u;
}

/* Host probe: sizeof(long double) == 8 and LDBL_MANT_DIG == 53, i.e. the
 * long double ABI is identical to double here, so the binary64 patterns below
 * apply to nextupl/nextdownl too. main() guards these checks on that fact. */
static uint64_t bits_l(long double v) {
    uint64_t u = 0;
    memcpy(&u, &v, (sizeof v < sizeof u) ? sizeof v : sizeof u);
    return u;
}

static double d_from_bits(uint64_t b) {
    double v;
    memcpy(&v, &b, sizeof v);
    return v;
}

static float f_from_bits(uint32_t b) {
    float v;
    memcpy(&v, &b, sizeof v);
    return v;
}

typedef struct {
    const char *name;
    uint64_t in_bits; /* stored as bits, never as a converted integer */
    uint64_t up;
    uint64_t down;
} DoubleCase;

static const DoubleCase d_cases[] = {
    /* name        input bits          nextup bits         nextdown bits      */
    {"+0",         0x0000000000000000ULL, 0x0000000000000001ULL, 0x8000000000000001ULL},
    {"-0",         0x8000000000000000ULL, 0x0000000000000001ULL, 0x8000000000000001ULL},
    {"1.0",        0x3ff0000000000000ULL, 0x3ff0000000000001ULL, 0x3fefffffffffffffULL},
    {"-1.0",       0xbff0000000000000ULL, 0xbfefffffffffffffULL, 0xbff0000000000001ULL},
    {"+minsub",    0x0000000000000001ULL, 0x0000000000000002ULL, 0x0000000000000000ULL},
    {"-minsub",    0x8000000000000001ULL, 0x8000000000000000ULL, 0x8000000000000002ULL},
    {"+maxsub",    0x000fffffffffffffULL, 0x0010000000000000ULL, 0x000ffffffffffffeULL},
    {"DBL_MIN",    0x0010000000000000ULL, 0x0010000000000001ULL, 0x000fffffffffffffULL},
    {"+DBL_MAX",   0x7fefffffffffffffULL, 0x7ff0000000000000ULL, 0x7feffffffffffffeULL},
    {"-DBL_MAX",   0xffefffffffffffffULL, 0xffeffffffffffffeULL, 0xfff0000000000000ULL},
    {"+INFINITY",  0x7ff0000000000000ULL, 0x7ff0000000000000ULL, 0x7fefffffffffffffULL},
    {"-INFINITY",  0xfff0000000000000ULL, 0xffefffffffffffffULL, 0xfff0000000000000ULL},
    {"quiet NaN",  0x7ff8000000000000ULL, 0x7ff8000000000000ULL, 0x7ff8000000000000ULL}
};

typedef struct {
    const char *name;
    uint32_t in_bits; /* stored as bits, never as a converted integer */
    uint32_t up;
    uint32_t down;
} FloatCase;

static const FloatCase f_cases[] = {
    /* name        input bits  nextup bits nextdown bits                       */
    {"+0",         0x00000000UL, 0x00000001UL, 0x80000001UL},
    {"-0",         0x80000000UL, 0x00000001UL, 0x80000001UL},
    {"1.0",        0x3f800000UL, 0x3f800001UL, 0x3f7fffffUL},
    {"-1.0",       0xbf800000UL, 0xbf7fffffUL, 0xbf800001UL},
    {"+minsub",    0x00000001UL, 0x00000002UL, 0x00000000UL},
    {"-minsub",    0x80000001UL, 0x80000000UL, 0x80000002UL},
    {"+maxsub",    0x007fffffUL, 0x00800000UL, 0x007ffffeUL},
    {"FLT_MIN",    0x00800000UL, 0x00800001UL, 0x007fffffUL},
    {"+FLT_MAX",   0x7f7fffffUL, 0x7f800000UL, 0x7f7ffffeUL},
    {"-FLT_MAX",   0xff7fffffUL, 0xff7ffffeUL, 0xff800000UL},
    {"+INFINITY",  0x7f800000UL, 0x7f800000UL, 0x7f7fffffUL},
    {"-INFINITY",  0xff800000UL, 0xff7fffffUL, 0xff800000UL},
    {"quiet NaN",  0x7fc00000UL, 0x7fc00000UL, 0x7fc00000UL}
};

static void test_double(void) {
    size_t i;
    char label[80];
    for (i = 0; i < sizeof d_cases / sizeof d_cases[0]; ++i) {
        double x = d_from_bits(d_cases[i].in_bits);
        snprintf(label, sizeof label, "nextup(double %s)", d_cases[i].name);
        CHECK_BITS(label, bits_d(nextup(x)), d_cases[i].up);
        snprintf(label, sizeof label, "nextdown(double %s)", d_cases[i].name);
        CHECK_BITS(label, bits_d(nextdown(x)), d_cases[i].down);
    }
    CHECK_BITS("nextup(0.1) == nextafter(0.1,+inf)",
               bits_d(nextup(0.1)), bits_d(nextafter(0.1, INFINITY)));
    CHECK_BITS("nextdown(-0.1) == nextafter(-0.1,-inf)",
               bits_d(nextdown(-0.1)), bits_d(nextafter(-0.1, -INFINITY)));
}

static void test_float(void) {
    size_t i;
    char label[80];
    for (i = 0; i < sizeof f_cases / sizeof f_cases[0]; ++i) {
        float x = f_from_bits(f_cases[i].in_bits);
        snprintf(label, sizeof label, "nextupf(%s)", f_cases[i].name);
        CHECK_BITS(label, bits_f(nextupf(x)), f_cases[i].up);
        snprintf(label, sizeof label, "nextdownf(%s)", f_cases[i].name);
        CHECK_BITS(label, bits_f(nextdownf(x)), f_cases[i].down);
    }
    CHECK_BITS("nextupf(0.1f) == nextafterf(0.1f,+inf)",
               bits_f(nextupf(0.1f)), bits_f(nextafterf(0.1f, INFINITY)));
    CHECK_BITS("nextdownf(-0.1f) == nextafterf(-0.1f,-inf)",
               bits_f(nextdownf(-0.1f)), bits_f(nextafterf(-0.1f, -INFINITY)));
}

static void test_ldouble(void) {
    size_t i;
    char label[80];
    for (i = 0; i < sizeof d_cases / sizeof d_cases[0]; ++i) {
        long double x = (long double)d_from_bits(d_cases[i].in_bits);
        snprintf(label, sizeof label, "nextupl(%s)", d_cases[i].name);
        CHECK_BITS(label, bits_l(nextupl(x)), d_cases[i].up);
        snprintf(label, sizeof label, "nextdownl(%s)", d_cases[i].name);
        CHECK_BITS(label, bits_l(nextdownl(x)), d_cases[i].down);
    }
    CHECK_BITS("nextupl(0.1L) == nextafterl(0.1L,+inf)",
               bits_l(nextupl((long double)0.1)),
               bits_l(nextafterl((long double)0.1, INFINITY)));
    CHECK_BITS("nextdownl(-0.1L) == nextafterl(-0.1L,-inf)",
               bits_l(nextdownl((long double)-0.1)),
               bits_l(nextafterl((long double)-0.1, -INFINITY)));
}

int main(void) {
    (void)DBL_EPSILON; /* document that binary64 is the expected format */
    test_double();
    test_float();
    if (sizeof(long double) == sizeof(uint64_t) && LDBL_MANT_DIG == DBL_MANT_DIG) {
        test_ldouble();
    } else {
        fprintf(stderr, "SKIP long-double bit checks: non-binary64 long double\n");
    }

    printf("c23_nextup: %d %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
