/* F-020: log ON/OFF parity across plain and checked memory/string APIs.
 * Config: seed=0xF1000014, iterations=1200 per pass, 2 passes (off, on).
 * Archives: build/libc/libcplus_hardened.a + build/safe/libcplus_safecrt.a.
 * Both loggers write to fd 2, which is redirected to /dev/null for BOTH passes
 * so the only observable difference is the flag itself. The FNV-1a digest is
 * computed from operation results and guarded objects only, never log text.
 * The logging-on pass also emits metadata-only cplus_safe_log records.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "cplus_libc.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_memory.h"
#include "safe_string.h"

#define SEED UINT32_C(0xF1000014)
#define ITERS 1200
#define CAP 512u
#define CLEN 8u
#define CBYTE 0xA5u

typedef struct {
    unsigned char pre[CLEN];
    unsigned char data[CAP];
    unsigned char post[CLEN];
} byte_obj;

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

static size_t pick_len(int it) {
    if (it == 0) return 0u;
    if (it == 1) return CAP;
    return (size_t)(rnd() % (CAP + 1u));
}

static void init_byte(byte_obj *o) {
    size_t i;
    memset(o->pre, CBYTE, sizeof o->pre);
    memset(o->post, CBYTE, sizeof o->post);
    for (i = 0; i < CAP; ++i) o->data[i] = (unsigned char)rnd();
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

static void check_byte(const byte_obj *o) {
    size_t i;
    for (i = 0; i < CLEN; ++i) {
        assert(o->pre[i] == CBYTE);
        assert(o->post[i] == CBYTE);
    }
}

static void check_str(const str_obj *o) {
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

static uint64_t run_stream(int log_on, cplus_safe_log_level level) {
    int it;
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    cplus_libc_log_set_enabled(log_on);
    cplus_safe_log_set_level(level);
    for (it = 0; it < ITERS; ++it) {
        byte_obj a, b;
        str_obj s, d;
        size_t n = pick_len(it), slen = (size_t)(rnd() % (CAP + 1u));
        size_t dmax = 1u + (size_t)(rnd() % CAP);
        size_t smax = (size_t)(rnd() % (dmax + 1u));
        size_t smax2 = 1u + (size_t)(rnd() % CAP);
        size_t got, want;
        errno_t e;
        init_byte(&a);
        init_byte(&b);
        init_str(&s, slen);
        init_str(&d, 0u);
        assert(cplus_memcpy(a.data, b.data, n) == a.data);
        assert(cplus_memset(b.data, (int)(rnd() & UINT32_C(0xFF)), n) == b.data);
        assert(cplus_strlen(s.data) == slen);
        want = (n < slen) ? n : slen;
        got = cplus_strnlen(s.data, n);
        assert(got == want);
        e = cplus_memcpy_s(a.data, dmax, b.data, smax);
        assert(e == CPLUS_SAFE_OK);
        got = cplus_strnlen_s(s.data, smax2);
        assert(got == ((smax2 < slen) ? smax2 : slen));
        cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "F-020", "parity");
        check_byte(&a);
        check_byte(&b);
        check_str(&s);
        check_str(&d);
        hash_mem("P", 1u);
        hash_u64((uint64_t)n);
        hash_u64((uint64_t)dmax);
        hash_u64((uint64_t)smax);
        hash_u64((uint64_t)got);
        hash_mem(&a, sizeof a);
        hash_mem(&s, sizeof s);
    }
    return g_dg;
}

static int g_saved_err = -1;

static void silence_stderr(void) {
    int dn;
    g_saved_err = dup(STDERR_FILENO);
    dn = open("/dev/null", O_WRONLY);
    if (dn >= 0) {
        (void)dup2(dn, STDERR_FILENO);
        (void)close(dn);
    }
}

static void restore_stderr(void) {
    if (g_saved_err >= 0) {
        (void)dup2(g_saved_err, STDERR_FILENO);
        (void)close(g_saved_err);
        g_saved_err = -1;
    }
}

int main(void) {
    uint64_t off_digest, on_digest;
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    silence_stderr();
    off_digest = run_stream(0, CPLUS_SAFE_LOG_OFF);
    on_digest = run_stream(1, CPLUS_SAFE_LOG_TRACE);
    restore_stderr();
    cplus_libc_log_set_enabled(0);
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    assert(off_digest == on_digest);
    printf("F-020 f100_str_log_parity: digest=%016llx cases=%d passes=2 ok\n",
           (unsigned long long)off_digest, ITERS * 2);
    return 0;
}
