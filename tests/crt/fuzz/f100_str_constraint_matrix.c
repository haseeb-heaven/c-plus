/* F-019: cross-API checked constraint matrix (memory + string _s variants).
 * Config: seed=0xF1000013, iterations=1200, ten scenarios cycled in order.
 * Archive: build/safe/libcplus_safecrt.a. Constraint mode: IGNORE.
 * Scenarios: NULL pointers, zero dmax/smax, overflow (SIZE_MAX) sizes,
 * truncation refusals, and one valid exact-fit success. The ignore handler's
 * stdout is silenced for the stream; return-code classes and canaries are
 * asserted on every case.
 */
#include <assert.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "safe_error.h"
#include "safe_memory.h"
#include "safe_string.h"

#define SEED UINT32_C(0xF1000013)
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
    st = SEED;
    g_dg = UINT64_C(0xcbf29ce484222325);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    assert(cplus_safe_get_constraint_mode() == CPLUS_SAFE_CONSTRAINT_IGNORE);
    silence_handler_diagnostics();
    for (it = 0; it < ITERS; ++it) {
        byte_obj b1, b2;
        str_obj s1, s2;
        errno_t e = CPLUS_SAFE_OK;
        size_t dmax = 1u + (size_t)(rnd() % CAP);
        size_t n = (size_t)(rnd() % (dmax + 1u));
        init_byte(&b1);
        init_byte(&b2);
        init_str(&s1, 8u);
        init_str(&s2, 64u);
        switch (it % 10) {
        case 0: e = cplus_memcpy_s(NULL, dmax, b2.data, n); break;
        case 1: e = cplus_memmove_s(b1.data, 0u, b2.data, n); break;
        case 2: e = cplus_memset_s(b1.data, dmax, 0x41, (rsize_t)-1); break;
        case 3: e = cplus_memcmp_s(b1.data, dmax, b2.data, n, NULL); break;
        case 4: e = cplus_memzero_s(NULL, dmax); break;
        case 5: e = cplus_strcpy_s(s1.data, 4u, s2.data); break; /* truncation */
        case 6: assert(cplus_strnlen_s(NULL, dmax) == 0u); break;
        case 7: e = cplus_strncpy_s(s1.data, dmax, s2.data, (rsize_t)-1); break;
        case 8: e = cplus_strcat_s(s1.data, 8u, s2.data); break; /* truncation */
        default:
            e = cplus_memcpy_s(b1.data, dmax, b2.data, n);
            assert(e == CPLUS_SAFE_OK);
            assert(memcmp(b1.data, b2.data, n) == 0);
            break;
        }
        if (it % 10 != 6) assert(e != CPLUS_SAFE_OK || it % 10 == 9);
        check_byte(&b1);
        check_byte(&b2);
        check_str(&s1);
        check_str(&s2);
        hash_u64((uint64_t)(unsigned)it % 10u);
        hash_u64((uint64_t)dmax);
        hash_u64((uint64_t)n);
        hash_u64((uint64_t)e);
        hash_mem(&b1, sizeof b1);
        hash_mem(&s1, sizeof s1);
    }
    restore_handler_diagnostics();
    printf("F-019 f100_str_constraint_matrix: digest=%016llx cases=%d scenarios=10 ok\n",
           (unsigned long long)g_dg, ITERS);
    return 0;
}
