/* F-017: checked cplus_strcmp_s with dmax variation and all orderings.
 * Config: seed=0xF1000011, iterations=1200, dmax in [1,512], strings 0..128.
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE.
 * Relation is it%3: equal, a<b, a>b. The indicator sign is asserted and both
 * guarded objects are checked. Constraint phase covers NULL and bad dmax.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_string.h"

#define SEED UINT32_C(0xF1000011)
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
    str_obj a, b;
    int ind = 0;
    init_str(&a, 4u);
    init_str(&b, 4u);
    silence_handler_diagnostics();
    assert(cplus_strcmp_s(NULL, 64u, b.data, &ind) != 0);
    assert(cplus_strcmp_s(a.data, 64u, NULL, &ind) != 0);
    assert(cplus_strcmp_s(a.data, 64u, b.data, NULL) != 0);
    assert(cplus_strcmp_s(a.data, 0u, b.data, &ind) != 0);
    assert(cplus_strcmp_s(a.data, (rsize_t)-1, b.data, &ind) != 0);
    check_canary(&a);
    check_canary(&b);
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
        str_obj a, b;
        size_t dmax, l1, l2;
        int relation = it % 3, ind = 99, want, got;
        errno_t e;
        dmax = (it == 0) ? 2u : ((it == 1) ? CAP : (2u + (size_t)(rnd() % (CAP - 1u))));
        l1 = 1u + (size_t)(rnd() % 128u);
        if (l1 + 1u > dmax) l1 = dmax - 1u;
        l2 = (relation == 0) ? l1 : (1u + (size_t)(rnd() % 128u));
        if (l2 + 1u > dmax) l2 = dmax - 1u;
        init_str(&a, l1);
        init_str(&b, l2);
        if (relation == 0) {
            memcpy(b.data, a.data, CAP + 1u);
        } else {
            /* Index 0 is the first differing byte, so later bytes cannot
             * change the ordering. 'A' < 'a' < 'z' under plain char compare. */
            a.data[0] = 'a';
            b.data[0] = (relation == 1) ? 'z' : 'A';
        }
        e = cplus_strcmp_s(a.data, dmax, b.data, &ind);
        assert(e == 0);
        got = sign_of(ind);
        want = (relation == 0) ? 0 : ((relation == 1) ? -1 : 1);
        check_canary(&a);
        check_canary(&b);
        assert(got == want);
        hash_mem("Q", 1u);
        hash_u64((uint64_t)dmax);
        hash_u64((uint64_t)(int64_t)got);
        hash_mem(&a, sizeof a);
        hash_mem(&b, sizeof b);
    }
    constraint_phase();
    printf("F-017 f100_str_strcmp_s: digest=%016llx cases=%d constraints=5 ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
