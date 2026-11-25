/* RED-TEAM agent 2/20: containers adversary (string / vector / collections).
 *
 * Attacks stdlib/string.hp, stdlib/vector.hp and stdlib/collections.hp with a
 * fixed-seed, one-case-per-fork campaign.  Every case is a hostile or
 * boundary probe; the child reports a 12-byte verdict over a pipe.  The parent
 * classifies signals, fatal exits, short reads and malformed verdicts as CRASH,
 * canary/invariant violations as CORRUPT, contract-honouring outcomes as
 * REFUSED, and accepted hostilities as UNEXPECTED_OK.
 *
 * STDLIB_ALLOC_LIMIT is pinned to 4096 so growth/overflow boundaries are
 * reachable without any 1 TiB allocation attempt.
 *
 * Build (from repo root):
 *   ./build/c+ build tests/redblue20/red_containers/harness.cp \
 *     --default --sanitize=none -I. \
 *     -o tests/redblue20/red_containers/out/harness
 */
#define STDLIB_ALLOC_LIMIT 4096
#define LIMIT 4096

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

#define N_CASES 6000
#define RNG_SEED 0xC0FFEE123456789ULL
#define LOG_PATH "tests/redblue20/red_containers/scenarios.log"

/* child -> parent verdict codes */
#define CLS_REFUSED 0
#define CLS_UNEXPECTED_OK 1
#define CLS_CRASH 2
#define CLS_CORRUPT 3

struct case_result {
    int cls;
    int sub;
    int got;
};

struct case_spec {
    int family;
    unsigned long long a;
    unsigned long long b;
    unsigned long long c;
};

enum {
    F_STR_SELF_APPEND = 0,
    F_STR_ALIAS_CLEAR,
    F_STR_HUGE_RESERVE,
    F_STR_OOR_ACCESS,
    F_STR_DOUBLE_DROP,
    F_STR_UAF,
    F_VEC_INSERT_OOR,
    F_VEC_ERASE_EMPTY,
    F_VEC_HUGE_RESERVE,
    F_VEC_ITER_INVALID,
    F_VEC_DOUBLE_DROP,
    F_INTMAP_COLLIDE,
    F_INTMAP_GROW,
    F_INTMAP_LIFECYCLE,
    F_STRMAP_OVERSIZE,
    F_STRMAP_COLLIDE,
    F_DEQUE_WRAP,
    F_STACK_QUEUE_LIST,
    F_FAIL_ATOMIC,
    F_COUNT
};

/* ---- deterministic PRNG (splitmix64) ----------------------------------- */
static unsigned long long g_rng;
static unsigned char g_canary[128];
static const char g_lower[27] = "abcdefghijklmnopqrstuvwxyz";
static const char g_upper[27] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";

static unsigned long long next_rand(void) {
    unsigned long long z;
    z = (g_rng += 0x9E3779B97F4A7C15ULL);
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

static struct case_spec gen_spec(int index) {
    struct case_spec s;
    s.family = index % F_COUNT;
    s.a = next_rand();
    s.b = next_rand();
    s.c = next_rand();
    return s;
}

/* ---- verdict helpers ---------------------------------------------------- */
static void setres(struct case_result *r, int cls, int sub, int got) {
    r->cls = cls;
    r->sub = sub;
    r->got = got;
}

static void expect_refuse(struct case_result *r, int accepted, int sub,
                          int got) {
    if (accepted)
        setres(r, CLS_UNEXPECTED_OK, sub, got);
    else
        setres(r, CLS_REFUSED, sub, got);
}

static void expect_clean(struct case_result *r, int violated, int sub) {
    if (violated)
        setres(r, CLS_CORRUPT, sub, 0);
    else
        setres(r, CLS_REFUSED, sub, 0);
}

static int all_bytes(const void *p, size_t n, unsigned char value) {
    const unsigned char *q = (const unsigned char *)p;
    size_t i;
    for (i = 0; i < n; ++i)
        if (q[i] != value)
            return 0;
    return 1;
}

static void canary_init(void) {
    size_t i;
    for (i = 0; i < sizeof(g_canary); ++i)
        g_canary[i] = (unsigned char)0xA5;
}

static int canary_ok(void) {
    return all_bytes(g_canary, sizeof(g_canary), (unsigned char)0xA5);
}

/* ---- precomputed colliding-key corpora (computed once, inherited by fork) */
static int g_ic_n = 0;
static int g_ic[64];
static int g_sc_n = 0;
static char g_sc[48][16];

static void init_colliding_keys(void) {
    long long k = 0;
    int i;
    while (g_ic_n < 64 && k < 4000000LL) {
        if ((stdlib_map_hash_int(k) & 1023ULL) == 0ULL)
            g_ic[g_ic_n++] = (int)k;
        k++;
    }
    for (i = 0; i < 500000 && g_sc_n < 48; i++) {
        char buf[16];
        snprintf(buf, sizeof(buf), "s%d", i);
        if ((stdlib_map_hash_text(buf) & 63ULL) == 0ULL) {
            snprintf(g_sc[g_sc_n], 16, "%s", buf);
            g_sc_n++;
        }
    }
}

/* ======================================================================== */
/* string attacks                                                            */
/* ======================================================================== */

static void a_str_self_append(struct case_spec *s, struct case_result *r) {
    char buf[64];
    size_t n = 8u + (size_t)(s->b % 20ULL);
    size_t i, off, expect_len;
    int variant = (int)(s->a % 4ULL);
    int rc;
    int good;
    const char *p;

    for (i = 0; i < n; i++)
        buf[i] = g_lower[i % 26];
    buf[n] = '\0';
    string sval = string(buf);
    off = (variant & 1) ? 1u : 0u;
    if (variant >= 2)
        sval.reserve(2u * n + 2u); /* force the in-place overlap path */

    rc = sval.append(sval.c_str() + off);
    expect_len = 2u * n - off;
    good = (rc == 0) && (sval.size() == expect_len);
    if (good) {
        p = sval.c_str();
        for (i = 0; i < n; i++)
            if (p[i] != g_lower[i % 26])
                good = 0;
        for (i = 0; i < n - off; i++)
            if (p[n + i] != g_lower[(i + off) % 26])
                good = 0;
        if (p[expect_len] != '\0')
            good = 0;
    }
    expect_clean(r, !good, 0);
}

static void a_str_alias_clear(struct case_spec *s, struct case_result *r) {
    char buf[64];
    size_t n = 12u + (size_t)(s->b % 12ULL);
    size_t off, i, expect_len;
    const char *p;
    int rc;
    int good;

    for (i = 0; i < n; i++)
        buf[i] = g_upper[i % 26];
    buf[n] = '\0';
    string sval = string(buf);
    off = 1u + (size_t)(s->c % 11ULL);
    if (off >= n)
        off = n - 1u;
    p = sval.c_str() + off; /* interior pointer into the still-live buffer */
    sval.clear();           /* documented historical overlap risk */
    rc = sval.append(p);
    expect_len = n - off;
    good = (rc == 0) && (sval.size() == expect_len);
    if (good) {
        for (i = 0; i < expect_len; i++)
            if (sval.c_str()[i] != g_upper[(i + off) % 26])
                good = 0;
        if (sval.c_str()[expect_len] != '\0')
            good = 0;
    }
    expect_clean(r, !good, 0);
}

static void a_str_huge_reserve(struct case_spec *s, struct case_result *r) {
    string sval = string("abc");
    size_t cap0 = sval.capacity();
    int rc1 = sval.reserve(SIZE_MAX);
    int rc2 = sval.reserve((size_t)LIMIT + 1u);
    int rc3;
    int good;
    (void)s;
    good = (rc1 == -1) && (rc2 == -1) && (sval.size() == 3u) &&
           (sval.capacity() == cap0) && (strcmp(sval.c_str(), "abc") == 0);
    rc3 = sval.reserve((size_t)LIMIT);
    good = good && (rc3 == 0) && (sval.capacity() == (size_t)LIMIT) &&
           (strcmp(sval.c_str(), "abc") == 0);
    expect_clean(r, !good, 0);
}

static void a_str_oor_access(struct case_spec *s, struct case_result *r) {
    string sval = string("aa,bb,cc");
    unsigned char out[8];
    int rc;
    int variant = (int)(s->a % 5ULL);
    size_t i;
    for (i = 0; i < sizeof(out); i++)
        out[i] = (unsigned char)0x5A;
    if (variant == 0)
        rc = sval.substr(sval.size() + 1u, 1u, (char *)out, sizeof(out));
    else if (variant == 1)
        rc = sval.substr(SIZE_MAX, 1u, (char *)out, sizeof(out));
    else if (variant == 2)
        rc = sval.substr(0u, 1u, (char *)out, 0u);
    else if (variant == 3)
        rc = sval.substr(0u, 1u, NULL, sizeof(out));
    else
        rc = sval.field(',', 99u, (char *)out, sizeof(out));
    expect_refuse(r, rc == 0, 0, rc);
    if (r->cls == CLS_REFUSED && !all_bytes(out, sizeof(out), (unsigned char)0x5A))
        setres(r, CLS_CORRUPT, 1, 0);
}

static void a_str_double_drop(struct case_spec *s, struct case_result *r) {
    string sval = string("hello");
    (void)s;
    sval.drop();
    sval.drop();
    {
        char out[8];
        size_t i;
        for (i = 0; i < sizeof(out); i++)
            out[i] = (unsigned char)0x5A;
        {
            int good = (sval.size() == 0u) && (sval.capacity() == 0u) &&
                       (sval.valid() == 0) && (sval.c_str()[0] == '\0') &&
                       (sval.at(0) == '\0') && (sval.equals("") == 1) &&
                       (sval.compare("") == 0) && (sval.find("x") == -1L) &&
                       (sval.is_valid_utf8() == 1) &&
                       (sval.substr(0u, 1u, out, sizeof(out)) == 0) &&
                       (out[0] == '\0') && (sval.count_fields(',') == 1u) &&
                       (sval.field(',', 0u, out, sizeof(out)) == 0) &&
                       (out[0] == '\0');
            /* reuse after drop must re-initialise */
            sval.append("again");
            good = good && (sval.valid() == 1) && (sval.size() == 5u) &&
                   (strcmp(sval.c_str(), "again") == 0);
            expect_clean(r, !good, 0);
        }
    }
}

static void a_str_uaf(struct case_spec *s, struct case_result *r) {
    string sval = string("xyz");
    string sval2 = string("copy-source");
    int good;
    (void)s;
    sval.drop();
    /* all reads on an invalid object must be benign, never dereference NULL */
    good = (sval.equals(NULL) == 0) && (sval.compare(NULL) == 1) &&
           (sval.find(NULL) == -1L) &&
           (sval.copy_from(NULL) == -1) && (sval.valid() == 0) &&
           (sval2.copy_from(&sval) == 0) && (sval2.size() == 0u);
    expect_clean(r, !good, 0);
}

/* ======================================================================== */
/* vector attacks                                                            */
/* ======================================================================== */

static void a_vec_insert_oor(struct case_spec *s, struct case_result *r) {
    vector vec = vector();
    int i;
    size_t sz0, cap0;
    int rc;
    int good;
    for (i = 0; i < 6; i++)
        vec.push_back(i);
    sz0 = vec.size();
    cap0 = vec.capacity();
    if ((s->a % 3ULL) == 0ULL)
        rc = vec.insert(sz0 + 1u, 99);
    else if ((s->a % 3ULL) == 1ULL)
        rc = vec.insert(SIZE_MAX, 99);
    else
        rc = vec.insert(sz0 + 2u, 99);
    good = (rc == -1) && (vec.size() == sz0) && (vec.capacity() == cap0);
    for (i = 0; i < (int)sz0; i++)
        if (vec.at((size_t)i) != i)
            good = 0;
    expect_clean(r, !good, 0);
}

static void a_vec_erase_empty(struct case_spec *s, struct case_result *r) {
    vector vec = vector();
    int out = 0x1234;
    int rc1, rc2, rc3, rc4;
    int good;
    (void)s;
    rc1 = vec.erase(0u, &out);
    rc2 = vec.pop_back(&out);
    rc3 = vec.at_checked(0u, &out);
    rc4 = vec.pop_back(NULL);
    good = (rc1 == -1) && (rc2 == -1) && (rc3 == -1) && (rc4 == -1) &&
           (out == 0x1234) && (vec.size() == 0u);
    expect_clean(r, !good, 0);
}

static void a_vec_huge_reserve(struct case_spec *s, struct case_result *r) {
    vector vec = vector();
    int i;
    size_t cap0;
    int rc1, rc2, rc3;
    int good;
    (void)s;
    for (i = 0; i < 5; i++)
        vec.push_back(i * 7);
    cap0 = vec.capacity();
    rc1 = vec.reserve(SIZE_MAX);
    rc2 = vec.reserve((size_t)LIMIT / sizeof(int) + 1u);
    good = (rc1 == -1) && (rc2 == -1) && (vec.capacity() == cap0) &&
           (vec.size() == 5u);
    rc3 = vec.reserve((size_t)LIMIT / sizeof(int));
    good = good && (rc3 == 0) && (vec.capacity() == (size_t)LIMIT / sizeof(int));
    for (i = 0; i < 5; i++)
        if (vec.at((size_t)i) != i * 7)
            good = 0;
    expect_clean(r, !good, 0);
}

static void a_vec_iter_invalid(struct case_spec *s, struct case_result *r) {
    vector vec = vector();
    vector vec2 = vector();
    int i, x = 0, out = 0x5A5A;
    int variant = (int)(s->a % 9ULL);
    int rc;
    int good;
    vec.reserve(8u);
    for (i = 0; i < 4; i++)
        vec.push_back(i);
    {
        slice sl = vec.view();
        if (variant == 0) {
            vec.push_back(10); /* no reallocation: fresh slice stays valid */
        } else if (variant == 1) {
            vec.pop_back(&x);
        } else if (variant == 2) {
            vec.clear();
        } else if (variant == 3) {
            vec.insert(0u, 11);
        } else if (variant == 4) {
            vec.erase(0u, NULL);
        } else if (variant == 5) {
            vec.reserve(16u);
        } else if (variant == 6) {
            vec.shrink_to_fit();
        } else if (variant == 7) {
            vec2.push_back(77);
            vec.copy_from(&vec2);
        } else {
            vec.drop();
        }

        if (variant == 0) {
            good = (sl.stale() == 0) && (sl.valid() == 1);
            rc = sl.at_checked(0u, &out);
            good = good && (rc == 0) && (out == 0);
        } else {
            good = (sl.stale() == 1) && (sl.valid() == 0);
            rc = sl.at_checked(0u, &out);
            good = good && (rc == -1) && (out == 0x5A5A);
        }
        {
            /* slice::sub on a stale slice returns an EMPTY, non-stale slice
             * (generation_ref is NULL); on a fresh slice it inherits the
             * generation and is likewise non-stale.  Both must report 0. */
            slice sp = sl.sub(0u, 2u);
            good = good && (sp.stale() == 0);
        }
        expect_clean(r, !good, 0);
    }
}

static void a_vec_double_drop(struct case_spec *s, struct case_result *r) {
    vector vec = vector();
    int i, x = 0, out = 0x1234;
    int good;
    (void)s;
    for (i = 0; i < 7; i++)
        vec.push_back(i);
    vec.drop();
    vec.drop();
    {
        slice empty_sl = vec.view();
        good = (vec.size() == 0u) && (vec.capacity() == 0u) &&
               (vec.pop_back(&x) == -1) && (vec.at_checked(0u, &out) == -1) &&
               (out == 0x1234) && (empty_sl.valid() == 0);
    }
    vec.push_back(42);
    good = good && (vec.size() == 1u) && (vec.at(0u) == 42);
    expect_clean(r, !good, 0);
}

/* ======================================================================== */
/* intmap attacks                                                            */
/* ======================================================================== */

static void a_intmap_collide(struct case_spec *s, struct case_result *r) {
    intmap imap = intmap();
    int n = 16 + (int)(s->b % 24ULL);
    int i, v = 0;
    int good = 1;
    if (n > g_ic_n)
        n = g_ic_n;
    for (i = 0; i < n; i++)
        if (imap.put(g_ic[i], 100 + i) != 0)
            good = 0;
    for (i = 0; i < n; i++)
        if (imap.get(g_ic[i], &v) != 0 || v != 100 + i)
            good = 0;
    for (i = 0; i < n; i += 2)
        imap.remove(g_ic[i]);
    for (i = 0; i < n; i++)
        if (imap.contains(g_ic[i]) != (i % 2))
            good = 0;
    for (i = 0; i < n; i += 2)
        imap.put(g_ic[i], 200 + i);
    for (i = 0; i < n; i++) {
        int expect = (i % 2 == 0) ? 200 + i : 100 + i;
        if (imap.get(g_ic[i], &v) != 0 || v != expect)
            good = 0;
    }
    imap.drop();
    imap.drop();
    good = good && (imap.size() == 0u) && (imap.valid() == 0);
    expect_clean(r, !good, 0);
}

static void a_intmap_grow(struct case_spec *s, struct case_result *r) {
    intmap imap = intmap();
    int n = 20 + (int)(s->c % 60ULL);
    int i, v = 0;
    int good = 1;
    int rc;
    for (i = 0; i < n; i++) {
        int key = i * 2654435761u + 7;
        if (imap.put(key, i) != 0)
            good = 0;
    }
    for (i = 0; i < n; i++) {
        int key = i * 2654435761u + 7;
        if (imap.get(key, &v) != 0 || v != i)
            good = 0;
    }
    rc = imap.grow(SIZE_MAX);
    good = good && (rc == -1) && (imap.size() == (size_t)n);
    for (i = 0; i < n; i++) {
        int key = i * 2654435761u + 7;
        if (imap.get(key, &v) != 0 || v != i)
            good = 0;
    }
    imap.drop();
    expect_clean(r, !good, 0);
}

static void a_intmap_lifecycle(struct case_spec *s, struct case_result *r) {
    intmap imap = intmap();
    int i, v = 0;
    int good;
    (void)s;
    for (i = 0; i < 12; i++)
        imap.put(i, i * 3);
    imap.clear();
    good = (imap.size() == 0u) && (imap.get(0, &v) == -1);
    for (i = 0; i < 12; i++)
        imap.put(i, i * 5);
    for (i = 0; i < 12; i++)
        if (imap.get(i, &v) != 0 || v != i * 5)
            good = 0;
    imap.drop();
    imap.drop();
    good = good && (imap.size() == 0u) && (imap.valid() == 0) &&
           (imap.get(0, &v) == -1) && (imap.contains(0) == 0) &&
           (imap.remove(0) == -1) && (v != 0);
    for (i = 0; i < 12; i++)
        imap.put(i, i);
    good = good && (imap.size() == 12u);
    imap.drop();
    expect_clean(r, !good, 0);
}

/* ======================================================================== */
/* strmap attacks                                                            */
/* ======================================================================== */

static void a_strmap_oversize(struct case_spec *s, struct case_result *r) {
    strmap smap = strmap();
    char big[LIMIT + 32];
    char normal[16];
    size_t len = (size_t)LIMIT + (size_t)(s->b % 16ULL);
    size_t i;
    int v = 0;
    int good;
    for (i = 0; i < len; i++)
        big[i] = 'k';
    big[len] = '\0';
    snprintf(normal, sizeof(normal), "ok");
    smap.put(normal, 7);
    {
        int rc = smap.put(big, 1);
        good = (rc == -1) && (smap.size() == 1u) &&
               (smap.get(normal, &v) == 0) && (v == 7) &&
               (smap.get(big, &v) == -1);
    }
    /* an over-long lookup/probe must refuse, not read out of range */
    {
        int rc2 = smap.get(big, &v);
        good = good && (rc2 == -1);
    }
    smap.drop();
    smap.drop();
    good = good && (smap.size() == 0u) && (smap.valid() == 0);
    expect_clean(r, !good, 0);
}

static void a_strmap_collide(struct case_spec *s, struct case_result *r) {
    strmap smap = strmap();
    int n = 12 + (int)(s->c % 20ULL);
    int i, v = 0;
    int good = 1;
    if (n > g_sc_n)
        n = g_sc_n;
    for (i = 0; i < n; i++)
        if (smap.put(g_sc[i], 10 + i) != 0)
            good = 0;
    for (i = 0; i < n; i++)
        if (smap.get(g_sc[i], &v) != 0 || v != 10 + i)
            good = 0;
    for (i = 0; i < n; i += 2)
        smap.remove(g_sc[i]);
    for (i = 0; i < n; i++)
        if (smap.contains(g_sc[i]) != (i % 2))
            good = 0;
    for (i = 0; i < n; i += 2)
        smap.put(g_sc[i], 30 + i);
    for (i = 0; i < n; i++) {
        int expect = (i % 2 == 0) ? 30 + i : 10 + i;
        if (smap.get(g_sc[i], &v) != 0 || v != expect)
            good = 0;
    }
    smap.clear();
    good = good && (smap.size() == 0u);
    for (i = 0; i < n; i++)
        smap.put(g_sc[i], i);
    for (i = 0; i < n; i++)
        if (smap.get(g_sc[i], &v) != 0 || v != i)
            good = 0;
    smap.drop();
    smap.drop();
    good = good && (smap.size() == 0u) && (smap.valid() == 0);
    expect_clean(r, !good, 0);
}

/* ======================================================================== */
/* deque / stack / queue / list                                             */
/* ======================================================================== */

/* Expected logical value at index i after: push_back 0..n-1, then
 * push_front n..2n-1, then k pop_front and k pop_back. */
static int dq_expected(int i, int n, int k) {
    int first_len = n - k;
    if (i < first_len)
        return 2 * n - 1 - k - i;
    return i - first_len;
}

static void a_deque_wrap(struct case_spec *s, struct case_result *r) {
    deque dq = deque();
    int n = 10 + (int)(s->b % 15ULL);
    int k, i, x = 0;
    int good = 1;
    int remain;
    for (i = 0; i < n; i++)
        dq.push_back(i);
    for (i = 0; i < n; i++)
        dq.push_front(n + i); /* wraps head repeatedly */
    k = 1 + (int)(s->c % 5ULL);
    for (i = 0; i < k; i++) {
        dq.pop_front(&x);
        if (x != dq_expected(0, n, i))
            good = 0;
    }
    for (i = 0; i < k; i++) {
        dq.pop_back(&x);
        if (x != n - 1 - i)
            good = 0;
    }
    remain = 2 * n - 2 * k;
    if (dq.size() != (size_t)remain)
        good = 0;
    for (i = 0; i < remain; i++) {
        dq.at_checked((size_t)i, &x);
        if (x != dq_expected(i, n, k))
            good = 0;
    }
    {
        size_t cap0 = dq.capacity();
        int rc = dq.reserve((size_t)(remain * 4 + 32));
        good = good && (rc == 0) && (dq.capacity() > cap0);
    }
    for (i = 0; i < remain; i++) {
        dq.at_checked((size_t)i, &x);
        if (x != dq_expected(i, n, k))
            good = 0;
    }
    {
        int out = 0x1234;
        int rc = dq.at_checked((size_t)remain, &out);
        good = good && (rc == -1) && (out == 0x1234);
    }
    dq.clear();
    good = good && (dq.size() == 0u);
    dq.drop();
    dq.drop();
    good = good && (dq.size() == 0u) && (dq.capacity() == 0u);
    expect_clean(r, !good, 0);
}

/* ======================================================================== */
/* injected-allocation-failure atomicity                                     */
/* ======================================================================== */

static void a_stack_queue_list(struct case_spec *s, struct case_result *r) {
    stack stk = stack();
    queue que = queue();
    list lst = list();
    int n = 8 + (int)(s->b % 12ULL);
    int i, x = 0, out = 0x4321;
    int good = 1;
    for (i = 0; i < n; i++) {
        stk.push(i);
        que.push(i);
        lst.push_back(i);
    }
    for (i = 0; i < n; i++)
        lst.push_front(1000 + i);
    for (i = 0; i < n; i++) {
        stk.pop(&x);
        if (x != n - 1 - i)
            good = 0;
        que.pop(&x);
        if (x != i)
            good = 0;
    }
    good = good && (stk.pop(&x) == -1) && (que.pop(&x) == -1) &&
           (stk.top_checked(&x) == -1) && (que.front_checked(&x) == -1);
    for (i = 0; i < n; i++) {
        lst.pop_front(&x);
        if (x != 1000 + n - 1 - i)
            good = 0;
    }
    for (i = 0; i < n; i++) {
        lst.pop_back(&x);
        if (x != n - 1 - i)
            good = 0;
    }
    good = good && (lst.pop_front(&x) == -1) && (lst.pop_back(&x) == -1) &&
           (lst.at_checked(0u, &out) == -1) && (out == 0x4321);
    stk.drop();
    stk.drop();
    que.drop();
    que.drop();
    lst.drop();
    lst.drop();
    good = good && (stk.size() == 0u) && (que.size() == 0u) &&
           (lst.size() == 0u);
    stk.push(9);
    que.push(8);
    lst.push_back(7);
    good = good && (stk.size() == 1u) && (que.size() == 1u) &&
           (lst.size() == 1u);
    expect_clean(r, !good, 0);
}

static void a_fail_atomic(struct case_spec *s, struct case_result *r) {
    int variant = (int)(s->a % 6ULL);
    long k = 1 + (long)(s->b % 4ULL); /* 1..4 */
    int good = 1;
    int rc;
    long seq;

    if (variant == 0) {
        /* string growth append: one allocation */
        string fat_sval = string("abc");
        char before[8];
        snprintf(before, sizeof(before), "%s", fat_sval.c_str());
        stdlib_alloc_fail_at(k);
        rc = fat_sval.append("defgh");
        seq = stdlib_alloc_seq();
        stdlib_alloc_fail_reset();
        if (seq < k) {
            setres(r, CLS_REFUSED, 2, 0); /* injection never fired: vacuous */
        } else if (rc != -1) {
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
        } else if ((fat_sval.size() != 3u) ||
                   (strcmp(fat_sval.c_str(), before) != 0)) {
            setres(r, CLS_CORRUPT, 1, 0);
        } else {
            setres(r, CLS_REFUSED, 0, 0);
        }
        return;
    }

    if (variant == 1) {
        /* vector growth push_back: one allocation */
        vector fat_vec = vector();
        fat_vec.push_back(1);
        fat_vec.push_back(2);
        stdlib_alloc_fail_at(k);
        rc = fat_vec.push_back(3);
        seq = stdlib_alloc_seq();
        stdlib_alloc_fail_reset();
        if (seq < k) {
            setres(r, CLS_REFUSED, 2, 0);
        } else if (rc != -1) {
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
        } else if ((fat_vec.size() != 2u) || (fat_vec.capacity() != 4u) ||
                   (fat_vec.at(0u) != 1) || (fat_vec.at(1u) != 2)) {
            setres(r, CLS_CORRUPT, 1, 0);
        } else {
            setres(r, CLS_REFUSED, 0, 0);
        }
        return;
    }

    if (variant == 2) {
        /* intmap rebuild: three allocations, all-or-nothing */
        intmap fat_imap = intmap();
        int i, val = 0;
        for (i = 0; i < 5; i++)
            fat_imap.put(i, i);
        stdlib_alloc_fail_at(k);
        rc = fat_imap.put(5, 5); /* 6th entry crosses the 70% threshold */
        seq = stdlib_alloc_seq();
        stdlib_alloc_fail_reset();
        if (seq < k) {
            setres(r, CLS_REFUSED, 2, 0);
        } else if (rc != -1) {
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
        } else {
            for (i = 0; i < 5; i++)
                if (fat_imap.get(i, &val) != 0 || val != i)
                    good = 0;
            if (fat_imap.size() != 5u || !good)
                setres(r, CLS_CORRUPT, 1, 0);
            else
                setres(r, CLS_REFUSED, 0, 0);
        }
        return;
    }

    if (variant == 3) {
        /* strmap grow + key copy: up to four allocations */
        strmap fat_smap = strmap();
        int i, val = 0;
        char key[16];
        for (i = 0; i < 5; i++) {
            snprintf(key, sizeof(key), "k%d", i);
            fat_smap.put(key, i);
        }
        stdlib_alloc_fail_at(k);
        snprintf(key, sizeof(key), "k5");
        rc = fat_smap.put(key, 5);
        seq = stdlib_alloc_seq();
        stdlib_alloc_fail_reset();
        if (seq < k) {
            setres(r, CLS_REFUSED, 2, 0);
        } else if (rc != -1) {
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
        } else {
            for (i = 0; i < 5; i++) {
                snprintf(key, sizeof(key), "k%d", i);
                if (fat_smap.get(key, &val) != 0 || val != i)
                    good = 0;
            }
            if (fat_smap.size() != 5u || !good)
                setres(r, CLS_CORRUPT, 1, 0);
            else
                setres(r, CLS_REFUSED, 0, 0);
        }
        return;
    }

    if (variant == 4) {
        /* deque reserve linearisation: one allocation */
        deque fat_dq = deque();
        int i, x = 0;
        for (i = 0; i < 6; i++)
            fat_dq.push_back(i);
        fat_dq.push_front(99); /* wrap head */
        stdlib_alloc_fail_at(k);
        rc = fat_dq.reserve(32u);
        seq = stdlib_alloc_seq();
        stdlib_alloc_fail_reset();
        if (seq < k) {
            setres(r, CLS_REFUSED, 2, 0);
        } else if (rc != -1) {
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
        } else {
            if (fat_dq.at_checked(0u, &x) != 0 || x != 99)
                good = 0;
            for (i = 0; i < 6; i++)
                if (fat_dq.at_checked((size_t)(i + 1), &x) != 0 || x != i)
                    good = 0;
            if (fat_dq.size() != 7u || !good)
                setres(r, CLS_CORRUPT, 1, 0);
            else
                setres(r, CLS_REFUSED, 0, 0);
        }
        return;
    }

    /* variant 5: vector copy_from, one allocation */
    {
        vector fat_src = vector();
        vector fat_dst = vector();
        int i;
        for (i = 0; i < 5; i++)
            fat_src.push_back(i * 11);
        fat_dst.push_back(777);
        stdlib_alloc_fail_at(k);
        rc = fat_dst.copy_from(&fat_src);
        seq = stdlib_alloc_seq();
        stdlib_alloc_fail_reset();
        if (seq < k) {
            setres(r, CLS_REFUSED, 2, 0);
        } else if (rc != -1) {
            setres(r, CLS_UNEXPECTED_OK, 0, rc);
        } else if ((fat_dst.size() != 1u) || (fat_dst.at(0u) != 777)) {
            setres(r, CLS_CORRUPT, 1, 0);
        } else {
            setres(r, CLS_REFUSED, 0, 0);
        }
    }
}

/* ======================================================================== */
/* dispatch / classifier / main                                              */
/* ======================================================================== */

static void run_attack(struct case_spec *s, struct case_result *r) {
    canary_init();
    r->cls = CLS_REFUSED;
    r->sub = 0;
    r->got = 0;

    if (s->family == F_STR_SELF_APPEND)
        a_str_self_append(s, r);
    else if (s->family == F_STR_ALIAS_CLEAR)
        a_str_alias_clear(s, r);
    else if (s->family == F_STR_HUGE_RESERVE)
        a_str_huge_reserve(s, r);
    else if (s->family == F_STR_OOR_ACCESS)
        a_str_oor_access(s, r);
    else if (s->family == F_STR_DOUBLE_DROP)
        a_str_double_drop(s, r);
    else if (s->family == F_STR_UAF)
        a_str_uaf(s, r);
    else if (s->family == F_VEC_INSERT_OOR)
        a_vec_insert_oor(s, r);
    else if (s->family == F_VEC_ERASE_EMPTY)
        a_vec_erase_empty(s, r);
    else if (s->family == F_VEC_HUGE_RESERVE)
        a_vec_huge_reserve(s, r);
    else if (s->family == F_VEC_ITER_INVALID)
        a_vec_iter_invalid(s, r);
    else if (s->family == F_VEC_DOUBLE_DROP)
        a_vec_double_drop(s, r);
    else if (s->family == F_INTMAP_COLLIDE)
        a_intmap_collide(s, r);
    else if (s->family == F_INTMAP_GROW)
        a_intmap_grow(s, r);
    else if (s->family == F_INTMAP_LIFECYCLE)
        a_intmap_lifecycle(s, r);
    else if (s->family == F_STRMAP_OVERSIZE)
        a_strmap_oversize(s, r);
    else if (s->family == F_STRMAP_COLLIDE)
        a_strmap_collide(s, r);
    else if (s->family == F_DEQUE_WRAP)
        a_deque_wrap(s, r);
    else if (s->family == F_STACK_QUEUE_LIST)
        a_stack_queue_list(s, r);
    else if (s->family == F_FAIL_ATOMIC)
        a_fail_atomic(s, r);
    else
        setres(r, CLS_CRASH, 99, s->family);

    if (canary_ok() == 0)
        setres(r, CLS_CORRUPT, 999, 0);
}

static const char *class_name(int cls) {
    if (cls == CLS_CRASH)
        return "CRASH";
    if (cls == CLS_CORRUPT)
        return "CORRUPT";
    if (cls == CLS_UNEXPECTED_OK)
        return "UNEXPECTED_OK";
    return "REFUSED";
}

static int classify_child(int waited_ok, int status, ssize_t got,
                          struct case_result *res) {
    int cls;
    if (!waited_ok || WIFSIGNALED(status) || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || got != (ssize_t)sizeof(*res))
        return CLS_CRASH;
    cls = res->cls;
    if (cls < CLS_REFUSED || cls > CLS_CORRUPT)
        return CLS_CRASH;
    return cls;
}

static int selfcheck(void) {
    struct case_result r = {0, 0, 0};
    int status = 0;
    int passed = 0;
    r.cls = CLS_UNEXPECTED_OK;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_UNEXPECTED_OK)
        passed++;
    r.cls = CLS_REFUSED;
    if (classify_child(1, (7 << 8), (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    if (classify_child(1, 11, (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    r.cls = CLS_CORRUPT;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CORRUPT)
        passed++;
    r.cls = 99;
    if (classify_child(1, status, (ssize_t)sizeof(r), &r) == CLS_CRASH)
        passed++;
    printf("SELFCHECK %d 5\n", passed);
    return passed == 5 ? 0 : 1;
}

int main(void) {
    const char *selfcheck_env = getenv("REDTEAM_SELFCHECK");
    int logfd;
    int line_len;
    char line[512];
    int i;
    int counts[4];
    int fds[2];
    pid_t pid;
    int status = 0;
    ssize_t got;
    struct case_spec spec;
    struct case_result result;
    int cls;

    if (selfcheck_env != (const char *)0 && strcmp(selfcheck_env, "1") == 0)
        return selfcheck();

    for (i = 0; i < 4; ++i)
        counts[i] = 0;
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
    g_rng = RNG_SEED;
    init_colliding_keys();

    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) {
        fprintf(stderr, "cannot open %s\n", LOG_PATH);
        printf("RED20 containers 0 0 0 0 0\n");
        return 2;
    }
    line_len = snprintf(line, sizeof(line),
                        "# containers redteam seed=%016llx cases=%d families=%d limit=%d\n",
                        RNG_SEED, N_CASES, F_COUNT, LIMIT);
    if (line_len > 0)
        (void)write(logfd, line, (size_t)line_len);

    for (i = 0; i < N_CASES; ++i) {
        spec = gen_spec(i);
        if (pipe(fds) != 0) {
            fprintf(stderr, "pipe failed at case %d\n", i);
            break;
        }
        fflush(stdout);
        pid = fork();
        if (pid < 0) {
            fprintf(stderr, "fork failed at case %d\n", i);
            close(fds[0]);
            close(fds[1]);
            break;
        }
        if (pid == 0) {
            struct case_result child_result;
            close(fds[0]);
            run_attack(&spec, &child_result);
            (void)write(fds[1], &child_result, sizeof(child_result));
            close(fds[1]);
            _exit(0);
        }
        close(fds[1]);
        got = read(fds[0], &result, sizeof(result));
        close(fds[0]);
        cls = classify_child(waitpid(pid, &status, 0) == pid, status, got,
                             &result);
        counts[cls]++;
        line_len = snprintf(line, sizeof(line),
                            "%d %d %llu %llu %llu %s sub=%d got=%d cls=%d\n",
                            i, spec.family, spec.a, spec.b, spec.c,
                            class_name(cls),
                            cls == CLS_CRASH ? -1 : result.sub,
                            cls == CLS_CRASH ? -1 : result.got, cls);
        if (line_len > 0)
            (void)write(logfd, line, (size_t)line_len);
    }

    line_len = snprintf(line, sizeof(line),
                        "# SUMMARY cases=%d refused=%d crash=%d corrupt=%d unexpected_ok=%d\n",
                        N_CASES, counts[CLS_REFUSED], counts[CLS_CRASH],
                        counts[CLS_CORRUPT], counts[CLS_UNEXPECTED_OK]);
    if (line_len > 0)
        (void)write(logfd, line, (size_t)line_len);
    close(logfd);

    printf("RED20 containers %d %d %d %d %d\n", N_CASES, counts[CLS_CRASH],
           counts[CLS_CORRUPT], counts[CLS_UNEXPECTED_OK],
           counts[CLS_REFUSED]);
    return 0;
}
