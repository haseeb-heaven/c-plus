/* F-005: bounded deterministic fuzz of cplus_memcmp with all three orderings.
 * Config: seed=0xF1000005, iterations=1200, length domain [0,512].
 * Archive: build/libc/libcplus_hardened.a. API family: plain (unchecked).
 * Iteration 0 forces n=0 (result must be 0), 1 forces exact-fit n=512.
 * The relation is chosen by it%3 (equal / a<b / a>b) and checked by sign.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_libc.h"

#define SEED UINT32_C(0xF1000005)
#define ITERS 1200
#define CAP 512u
#define CLEN 8u
#define CBYTE 0xA5u

typedef struct {
    unsigned char pre[CLEN];
    unsigned char data[CAP];
    unsigned char post[CLEN];
} byte_obj;

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

static void init_obj(byte_obj *o, unsigned char fill) {
    size_t i;
    memset(o->pre, CBYTE, sizeof o->pre);
    memset(o->post, CBYTE, sizeof o->post);
    for (i = 0; i < CAP; ++i) o->data[i] = (unsigned char)(fill + (unsigned char)(rnd() & 3u));
}

static void check_canary(const byte_obj *o) {
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

static int sign_of(int v) { return (v > 0) - (v < 0); }

int main(void) {
    int it;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    for (it = 0; it < ITERS; ++it) {
        byte_obj a, b;
        size_t n = pick_len(it), k;
        int relation = it % 3, r, sr;
        init_obj(&a, 0x40u);
        init_obj(&b, 0x40u);
        if (relation == 0) {
            memcpy(b.data, a.data, CAP);
        } else if (n > 0u) {
            k = (size_t)(rnd() % n);
            b.data[k] = (unsigned char)(a.data[k] + ((relation == 1) ? 8 : 200));
        }
        r = cplus_memcmp(a.data, b.data, n);
        sr = sign_of(r);
        check_canary(&a);
        check_canary(&b);
        if (n == 0u || relation == 0) assert(r == 0);
        else if (relation == 1) assert(sr < 0);
        else assert(sr > 0);
        hash_mem("P", 1u);
        hash_u64((uint64_t)n);
        hash_u64((uint64_t)(int64_t)sr);
    }
    printf("F-005 f100_mem_memcmp_bounded: digest=%016llx cases=%d ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
