/* Differential tests: compare cplus_* memory, string, and numeric
 * primitives against host C library behaviour on randomized valid inputs. */
#include <assert.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "install-include/cplus_libc.h"

static uint32_t rng = 0x13572468u;
static uint32_t next_u32(void) {
    rng = rng * 1664525u + 1013904223u;
    return rng;
}

static int sgn(int x) { return (x > 0) - (x < 0); }

int main(void) {
    unsigned char buf_a[128], buf_b[128], src[128];

    /* 1. Memory operations vs host libc */
    for (int round = 0; round < 300; ++round) {
        size_t len = next_u32() % 64u;
        size_t off = next_u32() % 32u;
        int fill = (int)(next_u32() & 0xffu);
        for (size_t i = 0; i < sizeof(src); ++i)
            src[i] = (unsigned char)(1u + (next_u32() % 250u));

        memset(buf_a, 0xA5, sizeof(buf_a));
        memset(buf_b, 0xA5, sizeof(buf_b));

        memcpy(buf_a, src, len);
        cplus_memcpy(buf_b, src, len);
        assert(memcmp(buf_a, buf_b, sizeof(buf_a)) == 0);

        memmove(buf_a + off, buf_a, len);
        cplus_memmove(buf_b + off, buf_b, len);
        assert(memcmp(buf_a, buf_b, sizeof(buf_a)) == 0);

        memset(buf_a, fill, len);
        cplus_memset(buf_b, fill, len);
        assert(memcmp(buf_a, buf_b, sizeof(buf_a)) == 0);

        int r_host = memcmp(buf_a, src, len);
        int r_cplus = cplus_memcmp(buf_a, src, len);
        assert(sgn(r_host) == sgn(r_cplus));

        void *hchr = memchr(src, (int)src[len / 2], sizeof(src));
        void *cchr = cplus_memchr(src, (int)src[len / 2], sizeof(src));
        assert(hchr == cchr);
    }

    /* 2. String operations vs host libc */
    for (int round = 0; round < 300; ++round) {
        char s1[64], s2[64], ha[128], hb[128];
        size_t n1 = 1u + (next_u32() % 28u);
        size_t n2 = 1u + (next_u32() % 28u);
        for (size_t i = 0; i < n1; ++i) s1[i] = (char)('a' + (next_u32() % 12u));
        s1[n1] = '\0';
        for (size_t i = 0; i < n2; ++i) s2[i] = (char)('a' + (next_u32() % 12u));
        s2[n2] = '\0';

        assert(cplus_strlen(s1) == strlen(s1));
        size_t bound = next_u32() % 40u;
        size_t host_sn = strnlen(s1, bound);
        assert(cplus_strnlen(s1, bound) == host_sn);

        memset(ha, '#', sizeof(ha));
        memset(hb, '#', sizeof(hb));
        strcpy(ha, s1);
        cplus_strcpy(hb, s1);
        assert(strcmp(ha, hb) == 0);

        strcat(ha, s2);
        cplus_strcat(hb, s2);
        assert(strcmp(ha, hb) == 0);

        assert(sgn(strcmp(s1, s2)) == sgn(cplus_strcmp(s1, s2)));
        assert(sgn(strncmp(s1, s2, bound)) == sgn(cplus_strncmp(s1, s2, bound)));

        int target = (int)s1[next_u32() % n1];
        assert(cplus_strchr(s1, target) == strchr(s1, target));
        assert(cplus_strrchr(s1, target) == strrchr(s1, target));

        char needle[4] = { s1[0], n1 > 1 ? s1[1] : '\0', '\0', '\0' };
        assert(cplus_strstr(s1, needle) == strstr(s1, needle));
        assert(cplus_strspn(s1, "abcde") == strspn(s1, "abcde"));
        assert(cplus_strcspn(s1, "xyzdef") == strcspn(s1, "xyzdef"));
        assert(cplus_strpbrk(s1, "def") == strpbrk(s1, "def"));

        char ca[64], cb[64];
        memset(ca, 0x55, sizeof(ca));
        memset(cb, 0x55, sizeof(cb));
        strncpy(ca, s1, bound < 64u ? bound : 64u);
        cplus_strncpy(cb, s1, bound < 64u ? bound : 64u);
        assert(memcmp(ca, cb, sizeof(ca)) == 0);
    }

    /* 3. memrchr / memmem / numeric helpers */
    {
        const char hay[] = "abracadabra";
        assert(cplus_memrchr(hay, 'a', strlen(hay)) == hay + 10);
        assert(cplus_memrchr(hay, 'z', strlen(hay)) == NULL);
        assert(cplus_memmem(hay, strlen(hay), "cad", 3) == hay + 4);
        assert(cplus_memmem(hay, strlen(hay), "", 0) == hay);

        div_t d1 = div(-17, 5), d2 = cplus_div(-17, 5);
        assert(d1.quot == d2.quot && d1.rem == d2.rem);
        ldiv_t l1 = ldiv(100003L, -7L), l2 = cplus_ldiv(100003L, -7L);
        assert(l1.quot == l2.quot && l1.rem == l2.rem);
        lldiv_t ll1 = lldiv(-9000000007LL, 13LL), ll2 = cplus_lldiv(-9000000007LL, 13LL);
        assert(ll1.quot == ll2.quot && ll1.rem == ll2.rem);
        assert(cplus_llabs(-42LL) == llabs(-42LL));
        assert(cplus_atoll("-9876543210") == atoll("-9876543210"));
    }

    puts("test_differential_libc: ok");
    return 0;
}
