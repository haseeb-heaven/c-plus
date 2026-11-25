/* F-018: checked cplus_strnlen_s with smax variation.
 * Config: seed=0xF1000012, iterations=1200, smax and string length in [0,512].
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE.
 * smax=0 is a defined constraint violation returning 0; the ignore handler's
 * stdout diagnostics are silenced for the whole stream and restored before
 * the summary line. Expected value is 0 for smax=0, else min(smax,strlen).
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_string.h"

#define SEED UINT32_C(0xF1000012)
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

int main(void) {
    int it;
    size_t zeros = 0u;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    silence_handler_diagnostics();
    for (it = 0; it < ITERS; ++it) {
        str_obj s;
        size_t smax, sl, got, want;
        smax = (it == 0) ? 0u : ((it == 1) ? CAP : (size_t)(rnd() % (CAP + 1u)));
        sl = (size_t)(rnd() % (CAP + 1u));
        init_str(&s, sl);
        got = cplus_strnlen_s(s.data, smax);
        want = (smax == 0u) ? 0u : ((smax < sl) ? smax : sl);
        assert(got == want);
        if (smax == 0u) ++zeros;
        check_canary(&s);
        hash_mem("L", 1u);
        hash_u64((uint64_t)smax);
        hash_u64((uint64_t)got);
        hash_mem(&s, sizeof s);
    }
    assert(cplus_strnlen_s(NULL, 64u) == 0u);
    assert(cplus_strnlen_s(NULL, 0u) == 0u);
    assert(cplus_strnlen_s("abc", (rsize_t)-1) == 0u);
    restore_handler_diagnostics();
    hash_u64((uint64_t)zeros);
    printf("F-018 f100_str_strnlen_s: digest=%016llx cases=%d zero_smax=%zu ok\n",
           (unsigned long long)g_dg, ITERS, zeros);
    return 0;
}
