/* F-009: checked cplus_memcmp_s with dmax/smax variation and diff indicator.
 * Config: seed=0xF1000009, iterations=1200, dmax in [1,512], smax in [1,dmax].
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE.
 * Relation chosen by it%3 (equal / a<b / a>b); diff sign is asserted.
 * Constraint phase covers NULL dest/src, NULL diff, zero/overflow/truncation.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_memory.h"

#define SEED UINT32_C(0xF1000009)
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
    byte_obj a, b;
    int diff = 0;
    init_obj(&a, 0x40u);
    init_obj(&b, 0x40u);
    silence_handler_diagnostics();
    assert(cplus_memcmp_s(NULL, 64u, b.data, 8u, &diff) != 0);
    assert(cplus_memcmp_s(a.data, 64u, NULL, 8u, &diff) != 0);
    assert(cplus_memcmp_s(a.data, 64u, b.data, 8u, NULL) != 0);
    assert(cplus_memcmp_s(a.data, 0u, b.data, 8u, &diff) != 0);
    assert(cplus_memcmp_s(a.data, 64u, b.data, 0u, &diff) != 0);
    assert(cplus_memcmp_s(a.data, 64u, b.data, (rsize_t)-1, &diff) != 0);
    assert(cplus_memcmp_s(a.data, 16u, b.data, 32u, &diff) != 0);
    check_canary(&a);
    check_canary(&b);
    restore_handler_diagnostics();
    hash_mem("X", 1u);
    hash_u64(7u);
}

int main(void) {
    int it;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (it = 0; it < ITERS; ++it) {
        byte_obj a, b;
        size_t dmax, smax, k;
        int relation = it % 3, diff = 99, r, sr;
        errno_t e;
        init_obj(&a, 0x40u);
        init_obj(&b, 0x40u);
        dmax = (it == 0) ? 1u : ((it == 1) ? CAP : (1u + (size_t)(rnd() % CAP)));
        smax = (it == 0) ? 1u : (1u + (size_t)(rnd() % dmax));
        if (relation == 0) {
            memcpy(b.data, a.data, CAP);
        } else {
            k = (size_t)(rnd() % smax);
            b.data[k] = (unsigned char)(a.data[k] + ((relation == 1) ? 8u : 200u));
        }
        e = cplus_memcmp_s(a.data, dmax, b.data, smax, &diff);
        assert(e == 0);
        r = sign_of(diff);
        sr = (relation == 0) ? 0 : ((relation == 1) ? -1 : 1);
        check_canary(&a);
        check_canary(&b);
        assert(r == sr);
        hash_mem("D", 1u);
        hash_u64((uint64_t)dmax);
        hash_u64((uint64_t)smax);
        hash_u64((uint64_t)(int64_t)r);
    }
    constraint_phase();
    printf("F-009 f100_mem_memcmp_s: digest=%016llx cases=%d constraints=7 ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
