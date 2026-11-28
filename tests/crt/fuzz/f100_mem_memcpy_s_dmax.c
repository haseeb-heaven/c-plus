/* F-006: checked cplus_memcpy_s with dmax/smax variation + constraint probes.
 * Config: seed=0xF1000006, iterations=1200, dmax in [1,512], smax in [0,dmax].
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE (explicit).
 * Valid stream checks copy content, untouched tail and canaries. A fixed
 * constraint phase covers NULL, zero dmax, overflow sizes and truncation
 * refusal; the ignore handler's stdout diagnostics are silenced there.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_memory.h"

#define SEED UINT32_C(0xF1000006)
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
    byte_obj dst, src;
    init_obj(&dst);
    init_obj(&src);
    silence_handler_diagnostics();
    assert(cplus_memcpy_s(NULL, 64u, src.data, 8u) != 0);
    assert(cplus_memcpy_s(dst.data, 64u, NULL, 8u) != 0);
    assert(cplus_memcpy_s(dst.data, 0u, src.data, 8u) != 0);
    assert(cplus_memcpy_s(dst.data, 64u, src.data, (rsize_t)-1) != 0);
    assert(cplus_memcpy_s(dst.data, (rsize_t)-1, src.data, 8u) != 0);
    assert(cplus_memcpy_s(dst.data, 16u, src.data, 32u) != 0); /* truncation */
    check_canary(&dst);
    check_canary(&src);
    restore_handler_diagnostics();
    hash_mem("X", 1u);
    hash_u64(6u); /* six asserted constraint violations */
}

int main(void) {
    int it;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (it = 0; it < ITERS; ++it) {
        byte_obj src, dst;
        unsigned char snap[CAP], tail[CAP];
        size_t dmax, smax, i;
        errno_t e;
        init_obj(&src);
        init_obj(&dst);
        memcpy(snap, src.data, CAP);
        memcpy(tail, dst.data, CAP);
        dmax = (it == 0) ? 1u : ((it == 1) ? CAP : (1u + (size_t)(rnd() % CAP)));
        smax = (it == 0) ? 0u : (size_t)(rnd() % (dmax + 1u));
        e = cplus_memcpy_s(dst.data, dmax, src.data, smax);
        assert(e == 0);
        check_canary(&src);
        check_canary(&dst);
        for (i = 0; i < smax; ++i) assert(dst.data[i] == snap[i]);
        for (i = smax; i < CAP; ++i) assert(dst.data[i] == tail[i]);
        hash_mem("C", 1u);
        hash_u64((uint64_t)dmax);
        hash_u64((uint64_t)smax);
        hash_mem(&dst, sizeof dst);
    }
    constraint_phase();
    printf("F-006 f100_mem_memcpy_s_dmax: digest=%016llx cases=%d constraints=6 ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
