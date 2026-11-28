/* lc01: primitive C99 scalar types -- sizes, ranges and arithmetic.
 * Standalone legacy C ABI probe: one main, one stable result line.
 */
#include <stdio.h>

int main(void)
{
    char c = 'A';
    signed char sc = -5;
    unsigned char uc = 250;
    short s = -1234;
    unsigned short us = 60000;
    int i = 123456;
    unsigned int ui = 4000000000u;
    long l = -1234567L;
    unsigned long ul = 4000000000UL;
    long long ll = -1234567890123LL;
    unsigned long long ull = 1234567890123ULL;
    float f = 1.5f;
    double d = 2.25;
    int ok = 1;

    ok &= (c == 'A');
    ok &= (sc == -5);
    ok &= (uc == 250);
    ok &= (s == -1234);
    ok &= (us == 60000);
    ok &= (i == 123456);
    ok &= (ui == 4000000000u);
    ok &= (l == -1234567L);
    ok &= (ul == 4000000000UL);
    ok &= (ll == -1234567890123LL);
    ok &= (ull == 1234567890123ULL);
    ok &= (f == 1.5f);
    ok &= (d == 2.25);

    printf("lc01_primitives: %s sizes=%lu/%lu/%lu/%lu/%lu calc=%lld\n",
           ok ? "OK" : "FAIL",
           (unsigned long)sizeof(char),
           (unsigned long)sizeof(short),
           (unsigned long)sizeof(int),
           (unsigned long)sizeof(long),
           (unsigned long)sizeof(long long),
           (long long)i + (long long)s);
    return ok ? 0 : 1;
}
