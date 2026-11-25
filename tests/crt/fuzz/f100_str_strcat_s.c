/* F-015: checked cplus_strcat_s with dmax variation and truncation.
 * Config: seed=0xF100000F, iterations=1200, prefix/suffix lengths 0..128,
 * dmax = dl+sl+1 (exact fit) or larger (roomy), chosen by LCG.
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE.
 * The 1200-case stream is all valid; truncation/NULL/zero are in the fixed
 * constraint phase so the ignore handler never floods stdout.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_string.h"

#define SEED UINT32_C(0xF100000F)
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
    str_obj dst, src;
    init_str(&src, 8u);
    init_str(&dst, 4u);
    silence_handler_diagnostics();
    assert(cplus_strcat_s(NULL, 64u, src.data) != 0);
    assert(cplus_strcat_s(dst.data, 64u, NULL) != 0);
    assert(cplus_strcat_s(dst.data, 0u, src.data) != 0);
    assert(cplus_strcat_s(dst.data, 8u, src.data) != 0); /* 4+8+1 > 8 */
    assert(cplus_strcat_s(dst.data, (rsize_t)-1, src.data) != 0);
    check_canary(&dst);
    check_canary(&src);
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
        str_obj src, dst;
        char snap[CAP + 1u], dpres[CAP + 1u];
        size_t dl, sl, dmax, i;
        errno_t e;
        dl = (it == 0) ? 0u : (size_t)(rnd() % 129u);
        sl = (it == 1) ? 0u : (size_t)(rnd() % 129u);
        init_str(&src, sl);
        init_str(&dst, dl);
        memcpy(snap, src.data, sizeof snap);
        memcpy(dpres, dst.data, sizeof dpres);
        dmax = dl + sl + 1u + ((it % 2u) ? 0u : (size_t)(rnd() % 64u));
        assert(dmax <= CAP + 1u);
        e = cplus_strcat_s(dst.data, dmax, src.data);
        assert(e == 0);
        check_canary(&src);
        check_canary(&dst);
        for (i = 0; i < dl; ++i) assert(dst.data[i] == dpres[i]);
        assert(dst.data[dl + sl] == '\0');
        assert(memcmp(dst.data + dl, snap, sl) == 0);
        hash_mem("A", 1u);
        hash_u64((uint64_t)dl);
        hash_u64((uint64_t)sl);
        hash_u64((uint64_t)dmax);
        hash_mem(&dst, sizeof dst);
    }
    constraint_phase();
    printf("F-015 f100_str_strcat_s: digest=%016llx cases=%d constraints=5 ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
