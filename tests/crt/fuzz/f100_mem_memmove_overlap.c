/* F-003: overlapping-but-valid cplus_memmove, both copy directions.
 * Config: seed=0xF1000003, iterations=1200, overlap length 1..464 within a
 * 512-byte payload at offsets 32/48. Archive: build/libc/libcplus_hardened.a.
 * The model snapshots the whole payload, copies the source slice, and verifies
 * the slice plus every byte outside [min(soff,doff), max(soff,doff)+m).
 */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cplus_libc.h"

#define SEED UINT32_C(0xF1000003)
#define ITERS 1200
#define CAP 512u
#define CLEN 8u
#define CBYTE 0xA5u
#define OFF_A 32u
#define OFF_B 48u

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
        byte_obj o;
        unsigned char before[CAP], slice[CAP];
        size_t m, soff, doff, lo, hi, i;
        void *r;
        init_obj(&o);
        memcpy(before, o.data, CAP);
        m = (it == 2) ? (CAP - OFF_B) : (1u + (size_t)(rnd() % 200u));
        soff = ((it & 1) != 0) ? OFF_B : OFF_A;
        doff = ((it & 1) != 0) ? OFF_A : OFF_B;
        assert(soff + m <= CAP && doff + m <= CAP);
        memcpy(slice, o.data + soff, m);
        r = cplus_memmove(o.data + doff, o.data + soff, m);
        assert(r == o.data + doff);
        check_canary(&o);
        assert(memcmp(o.data + doff, slice, m) == 0);
        lo = (soff < doff) ? soff : doff;
        hi = ((soff > doff) ? soff : doff) + m;
        for (i = 0; i < lo; ++i) assert(o.data[i] == before[i]);
        for (i = hi; i < CAP; ++i) assert(o.data[i] == before[i]);
        hash_mem("O", 1u);
        hash_u64((uint64_t)m);
        hash_u64((uint64_t)soff);
        hash_u64((uint64_t)doff);
        hash_mem(&o, sizeof o);
    }
    printf("F-003 f100_mem_memmove_overlap: digest=%016llx cases=%d ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
