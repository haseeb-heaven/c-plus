/* F-002: bounded deterministic fuzz of cplus_memmove (distinct objects).
 * Config: seed=0xF1000002, iterations=1200, length domain [0,512].
 * Archive: build/libc/libcplus_hardened.a. API family: plain (unchecked).
 * Forced cases: zero length, exact-fit 512, 511. Overlap is intentionally
 * excluded here and covered by F-003 with explicit offset modeling.
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_libc.h"

#define SEED UINT32_C(0xF1000002)
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

static void init_obj(byte_obj *o) {
    size_t i;
    memset(o->pre, CBYTE, sizeof o->pre);
    memset(o->post, CBYTE, sizeof o->post);
    for (i = 0; i < CAP; ++i) o->data[i] = (unsigned char)rnd();
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

int main(void) {
    int it;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    for (it = 0; it < ITERS; ++it) {
        byte_obj src, dst;
        unsigned char snap[CAP], tail[CAP];
        size_t n = pick_len(it), i;
        init_obj(&src);
        init_obj(&dst);
        memcpy(snap, src.data, CAP);
        memcpy(tail, dst.data, CAP);
        assert(cplus_memmove(dst.data, src.data, n) == dst.data);
        check_canary(&src);
        check_canary(&dst);
        for (i = 0; i < n; ++i) assert(dst.data[i] == snap[i]);
        for (i = n; i < CAP; ++i) assert(dst.data[i] == tail[i]);
        hash_mem("V", 1u);
        hash_u64((uint64_t)n);
        hash_mem(&dst, sizeof dst);
    }
    printf("F-002 f100_mem_memmove_bounded: digest=%016llx cases=%d ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
