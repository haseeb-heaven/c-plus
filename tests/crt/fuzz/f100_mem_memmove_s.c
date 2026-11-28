/* F-007: checked cplus_memmove_s with dmax/smax variation and valid overlap.
 * Config: seed=0xF1000007, iterations=1200, dmax in [1,512], smax in [0,dmax].
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE.
 * Every 17th iteration uses overlapping src/dst inside one guarded object
 * (offsets 24/32, length 1..64) to exercise the forward/backward copy paths.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_memory.h"

#define SEED UINT32_C(0xF1000007)
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

static int g_saved_out = -1;

static void silence_handler_diagnostics(void) {
    int dn;
    (void)fflush(stdout);
    g_saved_out = dup(STDOUT_FILENO);
    dn = open("/dev/null", O_WRONLY);
    if (dn >= 0) {
        (void)dup2(dn, STDOUT_FILENO);
        (void)close(dn);
    }
}

static void restore_handler_diagnostics(void) {
    if (g_saved_out >= 0) {
        (void)fflush(stdout);
        (void)dup2(g_saved_out, STDOUT_FILENO);
        (void)close(g_saved_out);
        g_saved_out = -1;
    }
}

static void constraint_phase(void) {
    byte_obj dst;
    init_obj(&dst);
    silence_handler_diagnostics();
    assert(cplus_memmove_s(NULL, 64u, dst.data, 8u) != 0);
    assert(cplus_memmove_s(dst.data, 64u, NULL, 8u) != 0);
    assert(cplus_memmove_s(dst.data, 0u, dst.data + 1u, 8u) != 0);
    assert(cplus_memmove_s(dst.data, 64u, dst.data, (rsize_t)-1) != 0);
    assert(cplus_memmove_s(dst.data, 16u, dst.data + 8u, 32u) != 0);
    check_canary(&dst);
    restore_handler_diagnostics();
    hash_mem("X", 1u);
    hash_u64(5u);
}

int main(void) {
    int it;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (it = 0; it < ITERS; ++it) {
        errno_t e;
        if (it % 17 == 2) {
            byte_obj o;
            unsigned char before[CAP], slice[CAP];
            size_t m, soff = 24u, doff = 32u, dmax, i;
            init_obj(&o);
            memcpy(before, o.data, CAP);
            m = 1u + (size_t)(rnd() % 64u);
            dmax = doff + m;
            assert(dmax <= CAP);
            memcpy(slice, o.data + soff, m);
            e = cplus_memmove_s(o.data + doff, dmax, o.data + soff, (rsize_t)m);
            assert(e == 0);
            check_canary(&o);
            assert(memcmp(o.data + doff, slice, m) == 0);
            for (i = 0; i < soff; ++i) assert(o.data[i] == before[i]);
            for (i = doff + m; i < CAP; ++i) assert(o.data[i] == before[i]);
            hash_mem("W", 1u);
            hash_u64((uint64_t)m);
            hash_u64((uint64_t)dmax);
            hash_mem(&o, sizeof o);
        } else {
            byte_obj src, dst;
            unsigned char snap[CAP], tail[CAP];
            size_t dmax, smax, i;
            init_obj(&src);
            init_obj(&dst);
            memcpy(snap, src.data, CAP);
            memcpy(tail, dst.data, CAP);
            dmax = (it == 0) ? 1u : ((it == 1) ? CAP : (1u + (size_t)(rnd() % CAP)));
            smax = (it == 0) ? 0u : (size_t)(rnd() % (dmax + 1u));
            e = cplus_memmove_s(dst.data, dmax, src.data, smax);
            assert(e == 0);
            check_canary(&src);
            check_canary(&dst);
            for (i = 0; i < smax; ++i) assert(dst.data[i] == snap[i]);
            for (i = smax; i < CAP; ++i) assert(dst.data[i] == tail[i]);
            hash_mem("M", 1u);
            hash_u64((uint64_t)dmax);
            hash_u64((uint64_t)smax);
            hash_mem(&dst, sizeof dst);
        }
    }
    constraint_phase();
    printf("F-007 f100_mem_memmove_s: digest=%016llx cases=%d constraints=5 ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
