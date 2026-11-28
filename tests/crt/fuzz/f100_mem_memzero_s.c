/* F-010: checked cplus_memzero_s with dmax variation.
 * Config: seed=0xF100000A, iterations=1200, dmax in [1,512].
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE.
 * Forced cases: iteration 0 dmax=1, iteration 1 exact-fit dmax=512.
 * Verifies the dmax-sized prefix is zeroed, the tail is untouched and both
 * canaries survive. Constraint phase covers NULL, zero and overflow dmax.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_memory.h"

#define SEED UINT32_C(0xF100000A)
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
    assert(cplus_memzero_s(NULL, 64u) != 0);
    assert(cplus_memzero_s(dst.data, 0u) != 0);
    assert(cplus_memzero_s(dst.data, (rsize_t)-1) != 0);
    check_canary(&dst);
    restore_handler_diagnostics();
    hash_mem("X", 1u);
    hash_u64(3u);
}

int main(void) {
    int it;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    for (it = 0; it < ITERS; ++it) {
        byte_obj dst;
        unsigned char tail[CAP];
        size_t dmax, i;
        errno_t e;
        init_obj(&dst);
        memcpy(tail, dst.data, CAP);
        dmax = (it == 0) ? 1u : ((it == 1) ? CAP : (1u + (size_t)(rnd() % CAP)));
        e = cplus_memzero_s(dst.data, dmax);
        assert(e == 0);
        check_canary(&dst);
        for (i = 0; i < dmax; ++i) assert(dst.data[i] == 0u);
        for (i = dmax; i < CAP; ++i) assert(dst.data[i] == tail[i]);
        hash_mem("Z", 1u);
        hash_u64((uint64_t)dmax);
        hash_mem(&dst, sizeof dst);
    }
    constraint_phase();
    printf("F-010 f100_mem_memzero_s: digest=%016llx cases=%d constraints=3 ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
