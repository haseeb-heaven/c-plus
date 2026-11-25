/* F-012: bounded deterministic fuzz of cplus_strnlen (plain hardened libc).
 * Config: seed=0xF100000C, iterations=1200, n and string length in [0,512].
 * Archive: build/libc/libcplus_hardened.a. API family: plain (unchecked).
 * Expected result is min(n, strlen(s)); n=0 is forced at iteration 0,
 * exact-fit n=512 at iteration 1. No scan past the NUL inside the payload.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_libc.h"

#define SEED UINT32_C(0xF100000C)
#define ITERS 1200
#define CAP 512u
#define CLEN 8u
#define CBYTE 0xA5u

typedef struct {
    unsigned char pre[CLEN];
    char data[CAP + 1u];
    unsigned char post[CLEN];
} str_obj;

static uint32_t st = SEED;

static uint32_t rnd(void) {
    st = st * UINT32_C(1664525) + UINT32_C(1013904223);
    return st;
}

static size_t pick_len(int it) {
    if (it == 0) return 0u;
    if (it == 1) return CAP;
    if (it == 2) return CAP - 1u;
    return (size_t)(rnd() % (CAP + 1u));
}

static void init_str(str_obj *o, size_t sl) {
    size_t i;
    assert(sl <= CAP);
    memset(o->pre, CBYTE, sizeof o->pre);
    memset(o->post, CBYTE, sizeof o->post);
    memset(o->data, 0x5A, sizeof o->data);
    for (i = 0; i < sl; ++i) o->data[i] = (char)('a' + (int)(rnd() % 26u));
    o->data[sl] = '\0';
}

static void check_canary(const str_obj *o) {
    size_t i;
    for (i = 0; i < CLEN; ++i) {
        assert(o->pre[i] == CBYTE);
        assert(o->post[i] == CBYTE);
    }
}

static uint64_t g_dg = UINT64_C(0xcbf29ce484222325);

static void hash_mem(const void *p, size_t n) {
    const unsigned char *q = (const unsigned char *)p;
    while (n != 0u) {
        g_dg ^= (uint64_t)*q++;
        g_dg *= UINT64_C(0x100000001b3);
        --n;
    }
}

static void hash_u64(uint64_t v) {
    unsigned i;
    for (i = 0; i < 8u; ++i) {
        unsigned char b = (unsigned char)(v >> (i * 8u));
        hash_mem(&b, 1u);
    }
}

int main(void) {
    int it;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    for (it = 0; it < ITERS; ++it) {
        str_obj s;
        size_t n = pick_len(it), sl = (size_t)(rnd() % (CAP + 1u));
        size_t got, want;
        init_str(&s, sl);
        want = (n < sl) ? n : sl;
        got = cplus_strnlen(s.data, n);
        assert(got == want);
        check_canary(&s);
        hash_mem("N", 1u);
        hash_u64((uint64_t)n);
        hash_u64((uint64_t)got);
        hash_mem(&s, sizeof s);
    }
    printf("F-012 f100_str_strnlen_bounded: digest=%016llx cases=%d ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
