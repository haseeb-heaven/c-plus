/* F-014: checked cplus_strncpy_s with dmax/slen variation and truncation.
 * Config: seed=0xF100000E, iterations=1200, dmax in [1,512], slen in [0,dmax].
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE.
 * The valid loop always leaves room for the terminator. A fixed constraint
 * phase covers NULL, zero dmax, overflow slen and a real truncation refusal.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_string.h"

#define SEED UINT32_C(0xF100000E)
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
    init_str(&dst, 0u);
    silence_handler_diagnostics();
    assert(cplus_strncpy_s(NULL, 64u, src.data, 4u) != 0);
    assert(cplus_strncpy_s(dst.data, 64u, NULL, 4u) != 0);
    assert(cplus_strncpy_s(dst.data, 0u, src.data, 4u) != 0);
    assert(cplus_strncpy_s(dst.data, 64u, src.data, (rsize_t)-1) != 0);
    assert(cplus_strncpy_s(dst.data, 8u, src.data, 8u) != 0); /* truncation */
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
        char snap[CAP + 1u];
        size_t dmax, sl, slen, i, copied;
        errno_t e;
        init_str(&src, (size_t)(rnd() % (CAP + 1u)));
        init_str(&dst, 0u);
        memcpy(snap, src.data, sizeof snap);
        dmax = (it == 0) ? 1u : ((it == 1) ? CAP : (1u + (size_t)(rnd() % CAP)));
        sl = strlen(snap);
        slen = (sl < dmax) ? (size_t)(rnd() % (dmax + 1u)) : (size_t)(rnd() % dmax);
        e = cplus_strncpy_s(dst.data, dmax, src.data, slen);
        assert(e == 0);
        check_canary(&src);
        check_canary(&dst);
        copied = (sl < slen) ? sl : slen;
        for (i = 0; i < copied; ++i) assert(dst.data[i] == snap[i]);
        for (i = 0; i < dmax; ++i) {
            if (dst.data[i] == '\0') break;
        }
        assert(i < dmax);
        hash_mem("C", 1u);
        hash_u64((uint64_t)dmax);
        hash_u64((uint64_t)slen);
        hash_mem(&dst, sizeof dst);
    }
    constraint_phase();
    printf("F-014 f100_str_strncpy_s: digest=%016llx cases=%d constraints=5 ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
