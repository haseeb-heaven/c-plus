/* BLUE20 containers safety proof.
 *
 * 2400 fixed-seed, fork-per-case checks over stdlib/string.hp,
 * stdlib/vector.hp and stdlib/collections.hp (intmap and friends).
 *
 * METHOD
 * ------
 * One case per forked child.  The parent bounds each child to 60 s and marks a
 * timeout / crash / signal as FAIL, so a single fatal (double free, bounds
 * abort, hang) cannot corrupt the aggregate.  A fixed-seed xorshift64* PRNG is
 * reseeded from the case id, so every case is reproducible in isolation.
 * STDLIB_ALLOC_LIMIT is pinned to 4096 in THIS translation unit (the documented
 * allocator override) so overflow branches are reached with small, fast input.
 *
 * Every case checks a 96-byte seed-derived global canary (and, for the
 * caller-buffer families, a pre/post stack guard) before it reports PASS.
 *
 * OUTPUT
 * ------
 *   BLK <id> <family> <case> <PASS|FAIL> <canary_ok>   (one per child)
 *   BLUE20 containers <cases> <passed> <failed>           (parent summary)
 * "BLUE20 containers ... 0 failed" and exit 0 means all cases passed.
 */
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/collections.hp"

#define NCASES 2400
#define TIMEOUT_SEC 60
#define CANARY_LEN 96

/* ---- deterministic PRNG (xorshift64*) ---------------------------------- */
static unsigned long long g_rs;

static void rs_seed(int id) {
    g_rs = 0x9E3779B97F4A7C15ULL ^
           ((unsigned long long)(id + 1) * 0xBF58476D1CE4E5B9ULL);
}

static unsigned long long rn(void) {
    unsigned long long x = g_rs;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rs = x;
    return x * 2685821657736338717ULL;
}

/* Uniform in [0, n); n > 0.  Multiply-shift of the high 32 bits. */
static unsigned long long rbelow(unsigned long long n) {
    return ((rn() >> 32) * n) >> 32;
}

/* ---- canaries ---------------------------------------------------------- */
static unsigned char g_canary[CANARY_LEN];
static const char *g_fam = "none";
static const char *g_case = "none";
static const char *g_fail_case = "none";
static int g_canary_ok = 1;

static unsigned char canary_byte(unsigned long long seed, unsigned long long i) {
    return (unsigned char)((seed * 131ULL + i * 37ULL + 0x5AULL) & 0xFFULL);
}

static void canary_fill(unsigned long long seed) {
    unsigned long long i;
    for (i = 0; i < CANARY_LEN; i++)
        g_canary[i] = canary_byte(seed, i);
}

static void canary_check(unsigned long long seed) {
    unsigned long long i;
    for (i = 0; i < CANARY_LEN; i++)
        if (g_canary[i] != canary_byte(seed, i))
            g_canary_ok = 0;
}

/* Guarded caller buffer: substr/field/at_checked must not touch pre/post. */
typedef struct {
    unsigned char pre[32];
    char buf[160];
    unsigned char post[32];
} guarded;

static void guarded_init(guarded *g) {
    unsigned long long i;
    for (i = 0; i < 32; i++) {
        g->pre[i] = (unsigned char)(0xA1ULL + i);
        g->post[i] = (unsigned char)(0xD1ULL + i);
    }
    memset(g->buf, 0xCC, sizeof g->buf);
}

static int guarded_ok(const guarded *g) {
    unsigned long long i;
    for (i = 0; i < 32; i++) {
        if (g->pre[i] != (unsigned char)(0xA1ULL + i))
            return 0;
        if (g->post[i] != (unsigned char)(0xD1ULL + i))
            return 0;
    }
    return 1;
}

#define CK(c) do { if (!(c)) { ok = 0; g_fail_case = g_case; } } while (0)

/* ---- family 0: string self-append / alias ------------------------------ */
static int fam_self_append(void) {
    int ok = 1;
    unsigned long long len = 1 + rbelow(40);
    unsigned long long i, n, off;
    char sbuf[64];
    const char *lower = "abcdefghijklmnopqrstuvwxyz";
    string s;
    g_fam = "self_append";

    for (i = 0; i < len; i++)
        sbuf[i] = lower[(unsigned)(i % 26)];
    sbuf[len] = '\0';

    g_case = "full_self_append";
    s = string(sbuf);
    CK(s.valid() == 1);
    CK(s.size() == len);
    CK(s.append(s.c_str()) == 0);
    CK(s.size() == 2 * len);
    for (i = 0; i < len; i++) {
        CK(s.at(i) == sbuf[i]);
        CK(s.at(len + i) == sbuf[i]);
    }
    CK(s.at(2 * len) == '\0');
    s.drop();

    g_case = "alias_suffix_append";
    s = string(sbuf);
    off = (len > 1) ? (1 + rbelow(len - 1)) : 1;
    if (off > len)
        off = len;
    {
        char expect[160];
        n = len - off;
        for (i = 0; i < len; i++)
            expect[i] = sbuf[i];
        for (i = 0; i < n; i++)
            expect[len + i] = sbuf[off + i];
        expect[len + n] = '\0';
        CK(s.append(s.c_str() + off) == 0);
        CK(s.size() == len + n);
        for (i = 0; i < len + n; i++)
            CK(s.at(i) == expect[i]);
        CK(s.at(len + n) == '\0');
    }
    s.drop();

    g_case = "inplace_self_append";
    s = string(sbuf);
    CK(s.reserve(128) == 0);
    CK(s.capacity() == 128);
    CK(s.append(s.c_str()) == 0);
    CK(s.size() == 2 * len);
    CK(s.capacity() == 128);
    for (i = 0; i < len; i++)
        CK(s.at(len + i) == sbuf[i]);
    s.drop();
    return ok;
}

/* ---- family 1: capacity overflow refusal ------------------------------- */
static int fam_overflow(void) {
    int ok = 1;
    size_t bytes;
    size_t max_count = STDLIB_ALLOC_LIMIT / sizeof(int);
    vector v;
    string s;
    intmap m;
    g_fam = "overflow_refusal";
    v = vector();
    s = string("abc");
    m = intmap();

    g_case = "vector_bytes";
    bytes = 12345;
    CK(vector_allocation_bytes(max_count + 1, &bytes) == -1);
    CK(bytes == 12345);
    CK(vector_allocation_bytes(max_count, &bytes) == 0);
    CK(bytes == STDLIB_ALLOC_LIMIT);
    CK(vector_calculate_growth(max_count, max_count + 1) == max_count);

    g_case = "vector_reserve";
    CK(v.reserve(SIZE_MAX) == -1);
    CK(v.reserve(max_count + 1) == -1);
    CK(v.size() == 0 && v.capacity() == 0);
    CK(v.push_back(1) == 0 && v.at(0) == 1);

    g_case = "string_growth";
    CK(stdlib_string_calculate_growth(STDLIB_ALLOC_LIMIT + 1, 4) == 0);
    CK(stdlib_string_calculate_growth(STDLIB_ALLOC_LIMIT, 4) == STDLIB_ALLOC_LIMIT);

    g_case = "string_reserve";
    CK(s.reserve(STDLIB_ALLOC_LIMIT + 1) == -1);
    CK(s.reserve(SIZE_MAX) == -1);
    CK(s.size() == 3 && s.capacity() == 4);
    CK(strcmp(s.c_str(), "abc") == 0);

    g_case = "intmap_grow";
    CK(m.grow(SIZE_MAX) == -1);
    CK(m.size() == 0);

    v.drop();
    s.drop();
    m.drop();
    return ok;
}

/* ---- family 2: at() / bounds ------------------------------------------- */
static int fam_at_bounds(void) {
    int ok = 1;
    vector v;
    string s;
    slice sl;
    guarded gd;
    int out = 0;
    size_t i, n;
    const char *hw = "hello,world";
    g_fam = "at_bounds";
    n = 3 + rbelow(20);
    v = vector();
    s = string(hw);
    for (i = 0; i < n; i++)
        CK(v.push_back((int)(i * 3 + 1)) == 0);

    g_case = "vector_at_checked";
    for (i = 0; i < n; i++) {
        out = -99;
        CK(v.at_checked(i, &out) == 0);
        CK(out == (int)(i * 3 + 1));
    }
    out = -99;
    CK(v.at_checked(n, &out) == -1);
    CK(out == -99);
    CK(v.at_checked(n + 1, &out) == -1);
    CK(v.at_checked(SIZE_MAX, &out) == -1);
    CK(v.at_checked(0, (int *)0) == -1);

    g_case = "vector_at";
    for (i = 0; i < n; i++)
        CK(v.at(i) == (int)(i * 3 + 1));
    CK(v.at(n) == 0);
    CK(v.at(SIZE_MAX) == 0);

    g_case = "slice_at";
    sl = v.view();
    CK(sl.valid() == 1);
    CK(sl.size() == n);
    for (i = 0; i < n; i++) {
        out = -99;
        CK(sl.at_checked(i, &out) == 0);
        CK(out == (int)(i * 3 + 1));
    }
    out = -99;
    CK(sl.at_checked(n, &out) == -1);
    CK(sl.at_checked(SIZE_MAX, &out) == -1);
    CK(sl.at_checked(0, (int *)0) == -1);
    {
        slice sub = sl.sub(1, n);
        CK(sub.valid() == 1);
        CK(sub.size() == n - 1);
        out = -99;
        CK(sub.at_checked(0, &out) == 0);
        CK(out == (int)(1 * 3 + 1));
        CK(sub.at_checked(n - 1, &out) == -1);
        slice empty = sl.sub(n, 4);
        CK(empty.valid() == 0);
        CK(empty.size() == 0);
        CK(empty.at_checked(0, &out) == -1);
    }

    g_case = "string_at_substr";
    for (i = 0; i < 11; i++)
        CK(s.at(i) == hw[i]);
    CK(s.at(11) == '\0');
    CK(s.at(SIZE_MAX) == '\0');
    guarded_init(&gd);
    CK(s.substr(SIZE_MAX, 4, gd.buf, sizeof gd.buf) == -1);
    CK(s.substr(12, 4, gd.buf, sizeof gd.buf) == -1);
    CK(s.substr(11, 4, gd.buf, sizeof gd.buf) == 0);
    CK(gd.buf[0] == '\0');
    CK(s.substr(0, 4, gd.buf, sizeof gd.buf) == 0);
    CK(strcmp(gd.buf, "hell") == 0);
    CK(s.substr(0, 4, gd.buf, 3) == 0);
    CK(strcmp(gd.buf, "he") == 0);
    CK(s.substr(0, 4, (char *)0, sizeof gd.buf) == -1);
    CK(s.substr(0, 4, gd.buf, 0) == -1);
    CK(guarded_ok(&gd) == 1);

    g_case = "string_field";
    CK(s.field(',', 0, gd.buf, sizeof gd.buf) == 0);
    CK(strcmp(gd.buf, "hello") == 0);
    CK(s.field(',', 1, gd.buf, sizeof gd.buf) == 0);
    CK(strcmp(gd.buf, "world") == 0);
    CK(s.field(',', 2, gd.buf, sizeof gd.buf) == -1);
    CK(s.field('|', 0, gd.buf, sizeof gd.buf) == 0);
    CK(strcmp(gd.buf, hw) == 0);
    CK(s.field(',', 0, (char *)0, sizeof gd.buf) == -1);
    CK(guarded_ok(&gd) == 1);

    v.drop();
    s.drop();
    return ok;
}

/* ---- family 3: vector insert / erase edge cases ------------------------ */
static int fam_insert_erase(void) {
    int ok = 1;
    vector v;
    int ref[80];
    size_t i, n, m, idx, k;
    int out = 0;
    int val;
    g_fam = "insert_erase";
    n = 1 + rbelow(24);
    v = vector();
    for (i = 0; i < n; i++) {
        ref[i] = (int)(i * 2 + 1);
        CK(v.push_back(ref[i]) == 0);
    }
    m = n;

    g_case = "insert_random";
    idx = rbelow(m + 2);
    val = (int)(rbelow(100000) % 50000) - 25000;
    if (idx <= m) {
        CK(v.insert(idx, val) == 0);
        for (k = m; k > idx; k--)
            ref[k] = ref[k - 1];
        ref[idx] = val;
        m++;
        CK(v.size() == m);
        for (i = 0; i < m; i++)
            CK(v.at(i) == ref[i]);
    } else {
        CK(v.insert(idx, val) == -1);
        CK(v.size() == m);
        for (i = 0; i < m; i++)
            CK(v.at(i) == ref[i]);
    }

    g_case = "insert_extremes";
    CK(v.insert(0, -7) == 0);
    for (k = m; k > 0; k--)
        ref[k] = ref[k - 1];
    ref[0] = -7;
    m++;
    CK(v.insert(m, -8) == 0);
    ref[m] = -8;
    m++;
    CK(v.insert(SIZE_MAX, -9) == -1);
    CK(v.insert(m + 1, -9) == -1);
    CK(v.size() == m);
    for (i = 0; i < m; i++)
        CK(v.at(i) == ref[i]);

    g_case = "erase_random";
    idx = rbelow(m + 2);
    if (idx < m) {
        out = -12345;
        CK(v.erase(idx, &out) == 0);
        CK(out == ref[idx]);
        for (k = idx; k + 1 < m; k++)
            ref[k] = ref[k + 1];
        m--;
        CK(v.size() == m);
        for (i = 0; i < m; i++)
            CK(v.at(i) == ref[i]);
    } else {
        out = -12345;
        CK(v.erase(idx, &out) == -1);
        CK(out == -12345);
        CK(v.size() == m);
    }

    g_case = "erase_extremes";
    if (m > 0) {
        CK(v.erase(0, &out) == 0);
        for (k = 0; k + 1 < m; k++)
            ref[k] = ref[k + 1];
        m--;
        if (m > 0) {
            CK(v.erase(m - 1, &out) == 0);
            m--;
        }
        CK(v.erase(m, &out) == -1);
        CK(v.erase(SIZE_MAX, &out) == -1);
        CK(v.size() == m);
        for (i = 0; i < m; i++)
            CK(v.at(i) == ref[i]);
    }

    g_case = "pop_back";
    while (m > 0) {
        out = -777;
        CK(v.pop_back(&out) == 0);
        CK(out == ref[m - 1]);
        m--;
    }
    CK(v.size() == 0);
    CK(v.pop_back(&out) == -1);
    CK(v.pop_back((int *)0) == -1);
    v.drop();
    CK(v.size() == 0);
    CK(v.at(0) == 0);
    return ok;
}

/* ---- family 4: double-drop refusal ------------------------------------- */
static int fam_double_drop(void) {
    int ok = 1;
    string s;
    vector v;
    intmap im;
    strmap sm;
    intset is;
    deque dq;
    list ls;
    stack st;
    queue qu;
    g_fam = "double_drop";

    g_case = "string";
    s = string("double-drop-me");
    CK(s.valid() == 1);
    s.drop();
    s.drop();
    CK(s.valid() == 0 && s.size() == 0);
    CK(strcmp(s.c_str(), "") == 0);

    g_case = "vector";
    v = vector();
    CK(v.push_back(1) == 0 && v.push_back(2) == 0);
    v.drop();
    v.drop();
    CK(v.size() == 0 && v.capacity() == 0);

    g_case = "intmap";
    im = intmap();
    CK(im.put(3, 4) == 0 && im.size() == 1);
    im.drop();
    im.drop();
    CK(im.size() == 0 && im.valid() == 0);

    g_case = "strmap";
    sm = strmap();
    CK(sm.put("k", 5) == 0);
    sm.drop();
    sm.drop();
    CK(sm.size() == 0 && sm.valid() == 0);

    g_case = "intset";
    is = intset();
    CK(is.insert(7) == 0);
    is.drop();
    is.drop();
    CK(is.size() == 0 && is.valid() == 0);

    g_case = "deque";
    dq = deque();
    CK(dq.push_back(1) == 0);
    dq.drop();
    dq.drop();
    CK(dq.size() == 0);

    g_case = "list";
    ls = list();
    CK(ls.push_back(1) == 0);
    ls.drop();
    ls.drop();
    CK(ls.size() == 0);

    g_case = "stack";
    st = stack();
    CK(st.push(1) == 0);
    st.drop();
    st.drop();
    CK(st.size() == 0);

    g_case = "queue";
    qu = queue();
    CK(qu.push(1) == 0);
    qu.drop();
    qu.drop();
    CK(qu.size() == 0);

    g_case = "zero_init_drop";
    {
        string z = string((const char *)0);
        vector zv = vector();
        intmap zm = intmap();
        z.drop();
        z.drop();
        zv.drop();
        zv.drop();
        zm.drop();
        zm.drop();
        CK(z.size() == 0 && zv.size() == 0 && zm.size() == 0);
    }
    return ok;
}

/* ---- family 5: intmap collision handling ------------------------------- */
static int fam_intmap_collision(void) {
    int ok = 1;
    intmap m;
    unsigned long long k;
    int found[16];
    int nf = 0;
    int slot_target;
    int out = 0;
    size_t i;
    g_fam = "intmap_collision";
    m = intmap();
    CK(m.grow(64) == 0);
    slot_target = (int)rbelow(64);

    for (k = 1; k < 4000 && nf < 6; k++) {
        if ((int)(stdlib_map_hash_int((long long)k) & 63ULL) == slot_target)
            found[nf++] = (int)k;
    }
    CK(nf == 6);

    g_case = "collide_put_get";
    for (i = 0; i < 6; i++)
        CK(m.put(found[i], 100 + (int)i) == 0);
    CK(m.size() == 6);
    for (i = 0; i < 6; i++) {
        CK(m.get(found[i], &out) == 0);
        CK(out == 100 + (int)i);
        CK(m.contains(found[i]) == 1);
    }
    CK(m.get(123456789, &out) == -1);

    g_case = "collide_remove_tombstone";
    CK(m.remove(found[2]) == 0);
    CK(m.size() == 5);
    CK(m.get(found[2], &out) == -1);
    for (i = 0; i < 6; i++) {
        if (i != 2) {
            CK(m.get(found[i], &out) == 0);
            CK(out == 100 + (int)i);
        }
    }

    g_case = "collide_reput_overwrite";
    CK(m.put(found[2], 999) == 0);
    CK(m.size() == 6);
    CK(m.get(found[2], &out) == 0 && out == 999);
    CK(m.put(found[0], 777) == 0);
    CK(m.size() == 6);
    CK(m.get(found[0], &out) == 0 && out == 777);

    g_case = "collide_clear";
    m.clear();
    CK(m.size() == 0);
    for (i = 0; i < 6; i++)
        CK(m.contains(found[i]) == 0);
    m.drop();
    CK(m.valid() == 0);
    return ok;
}

/* ---- family 6: allocation-failure atomicity ---------------------------- */
static int fam_alloc_atomic(void) {
    int ok = 1;
    string s;
    string t;
    vector v;
    vector w;
    intmap m;
    size_t i, fp;
    int out = 0;
    g_fam = "alloc_atomic";

    g_case = "string_append_growth_fail";
    s = string("abcdef");
    CK(s.size() == 6 && s.capacity() == 7);
    stdlib_alloc_fail_at(1);
    CK(s.append("ghijklmnopqrstuvwxyz") == -1);
    stdlib_alloc_fail_reset();
    CK(s.size() == 6 && s.capacity() == 7);
    CK(strcmp(s.c_str(), "abcdef") == 0);
    s.drop();

    g_case = "string_append_inplace_noalloc";
    s = string("abcdef");
    CK(s.reserve(64) == 0);
    {
        const char *before = s.c_str();
        stdlib_alloc_fail_at(1);
        CK(s.append("xyz") == 0);
        stdlib_alloc_fail_reset();
        CK(s.c_str() == before);
        CK(s.size() == 9);
    }
    s.drop();

    g_case = "string_reserve_fail";
    s = string("abc");
    stdlib_alloc_fail_at(1);
    CK(s.reserve(64) == -1);
    stdlib_alloc_fail_reset();
    CK(s.size() == 3 && s.capacity() == 4);
    CK(strcmp(s.c_str(), "abc") == 0);
    s.drop();

    g_case = "string_copy_from_fail";
    s = string("abc");
    t = string("0123456789");
    stdlib_alloc_fail_at(1);
    CK(s.copy_from(&t) == -1);
    stdlib_alloc_fail_reset();
    CK(s.size() == 3 && strcmp(s.c_str(), "abc") == 0);
    s.drop();
    t.drop();

    g_case = "vector_push_growth_fail";
    v = vector();
    CK(v.reserve(4) == 0);
    for (i = 0; i < 4; i++)
        CK(v.push_back((int)(i + 1)) == 0);
    CK(v.capacity() == 4);
    stdlib_alloc_fail_at(1);
    CK(v.push_back(5) == -1);
    stdlib_alloc_fail_reset();
    CK(v.size() == 4 && v.capacity() == 4);
    for (i = 0; i < 4; i++)
        CK(v.at(i) == (int)(i + 1));
    v.drop();

    g_case = "vector_insert_growth_fail";
    v = vector();
    CK(v.reserve(4) == 0);
    for (i = 0; i < 4; i++)
        CK(v.push_back((int)i) == 0);
    {
        slice sl = v.view();
        CK(sl.stale() == 0);
        stdlib_alloc_fail_at(1);
        CK(v.insert(2, 99) == -1);
        stdlib_alloc_fail_reset();
        CK(sl.stale() == 0);
        CK(v.size() == 4);
        for (i = 0; i < 4; i++)
            CK(v.at(i) == (int)i);
    }
    v.drop();

    g_case = "vector_copy_from_fail";
    v = vector();
    w = vector();
    CK(w.push_back(11) == 0 && w.push_back(22) == 0);
    stdlib_alloc_fail_at(1);
    CK(v.copy_from(&w) == -1);
    stdlib_alloc_fail_reset();
    CK(v.size() == 0 && v.capacity() == 0);
    v.drop();
    w.drop();

    g_case = "vector_shrink_fail";
    v = vector();
    CK(v.reserve(16) == 0);
    CK(v.push_back(1) == 0 && v.push_back(2) == 0);
    stdlib_alloc_fail_at(1);
    CK(v.shrink_to_fit() == -1);
    stdlib_alloc_fail_reset();
    CK(v.size() == 2 && v.capacity() == 16);
    CK(v.at(0) == 1 && v.at(1) == 2);
    v.drop();

    /* intmap rebuild allocates 3 buffers; fail each one in turn. */
    for (fp = 1; fp <= 3; fp++) {
        g_case = "intmap_grow_fail";
        m = intmap();
        CK(m.grow(8) == 0);
        CK(m.put(1, 10) == 0 && m.put(2, 20) == 0);
        stdlib_alloc_fail_at((long)fp);
        CK(m.grow(64) == -1);
        stdlib_alloc_fail_reset();
        CK(m.size() == 2 && m.valid() == 1);
        CK(m.get(1, &out) == 0 && out == 10);
        CK(m.get(2, &out) == 0 && out == 20);
        CK(m.contains(3) == 0);
        m.drop();
    }

    /* put-triggered rebuild: 5 keys fill an 8-slot table; the 6th rebuilds. */
    for (fp = 1; fp <= 3; fp++) {
        g_case = "intmap_put_rebuild_fail";
        m = intmap();
        CK(m.grow(8) == 0);
        for (i = 1; i <= 5; i++)
            CK(m.put((int)i, (int)(i * 10)) == 0);
        CK(m.size() == 5);
        stdlib_alloc_fail_at((long)fp);
        CK(m.put(6, 60) == -1);
        stdlib_alloc_fail_reset();
        CK(m.size() == 5);
        for (i = 1; i <= 5; i++) {
            CK(m.get((int)i, &out) == 0);
            CK(out == (int)(i * 10));
        }
        CK(m.contains(6) == 0);
        m.drop();
    }
    return ok;
}

/* ---- family 7: randomized mixed stress -------------------------------- */
static int fam_mixed(void) {
    int ok = 1;
    vector v;
    string s;
    intmap m;
    size_t step, idx;
    int out = 0;
    int val;
    int had;
    int mcount;
    char mbuf[8];
    const char *upper = "ABCDEFGHIJKLMNOPQRSTUVWXYZ";
    const char *lower = "abcdefghijklmnopqrstuvwxyz";
    const char *digits = "0123456789";
    g_fam = "mixed_stress";
    g_case = "random_ops";
    v = vector();
    s = string("start");
    m = intmap();
    mcount = 0;

    for (step = 0; step < 40; step++) {
        unsigned long long op = rbelow(6);
        if (op == 0) {
            val = (int)rbelow(1000);
            CK(v.push_back(val) == 0);
        } else if (op == 1) {
            if (v.size() > 0) {
                idx = rbelow(v.size());
                out = -777;
                CK(v.at_checked(idx, &out) == 0);
                CK(v.erase(idx, &out) == 0);
            }
        } else if (op == 2) {
            val = (int)rbelow(1000);
            had = m.contains(val);
            CK(m.put(val, val * 2) == 0);
            if (had == 0)
                mcount++;
        } else if (op == 3) {
            val = (int)rbelow(1000);
            had = m.contains(val);
            if (had != 0) {
                CK(m.remove(val) == 0);
                mcount--;
            } else {
                CK(m.remove(val) == -1);
            }
        } else if (op == 4) {
            val = (int)rbelow(1000);
            out = -777;
            had = m.get(val, &out);
            if (had == 0)
                CK(out == val * 2);
            else
                CK(out == -777);
        } else {
            mbuf[0] = upper[(unsigned)rbelow(26)];
            mbuf[1] = lower[(unsigned)rbelow(26)];
            mbuf[2] = digits[(unsigned)rbelow(10)];
            mbuf[3] = '\0';
            CK(s.append(mbuf) == 0);
        }
        /* invariants after every step */
        CK(v.size() <= v.capacity());
        CK(v.at(v.size()) == 0);
        out = -777;
        CK(v.at_checked(v.size(), &out) == -1);
        CK(s.valid() == 1);
        CK(s.size() == strlen(s.c_str()));
        CK(s.at(s.size()) == '\0');
        CK(m.size() == (size_t)mcount);
    }
    v.drop();
    s.drop();
    m.drop();
    CK(v.size() == 0 && s.size() == 0 && m.size() == 0);
    return ok;
}

/* ---- worker / fork harness --------------------------------------------- */
static int worker(int id) {
    unsigned long long seed = (unsigned long long)id * 2654435761ULL + 0xABCDEFULL;
    int pass;
    rs_seed(id);
    canary_fill(seed);
    g_fam = "none";
    g_case = "none";
    g_fail_case = "none";
    g_canary_ok = 1;

    if ((unsigned)id % 8u == 0)
        pass = fam_self_append();
    else if ((unsigned)id % 8u == 1)
        pass = fam_overflow();
    else if ((unsigned)id % 8u == 2)
        pass = fam_at_bounds();
    else if ((unsigned)id % 8u == 3)
        pass = fam_insert_erase();
    else if ((unsigned)id % 8u == 4)
        pass = fam_double_drop();
    else if ((unsigned)id % 8u == 5)
        pass = fam_intmap_collision();
    else if ((unsigned)id % 8u == 6)
        pass = fam_alloc_atomic();
    else
        pass = fam_mixed();

    canary_check(seed);
    if (!g_canary_ok && pass)
        g_fail_case = "canary";
    printf("BLK %d %s %s %s %d\n", id, g_fam,
           (pass && g_canary_ok) ? g_case : g_fail_case,
           (pass && g_canary_ok) ? "PASS" : "FAIL", g_canary_ok);
    fflush(stdout);
    return (pass && g_canary_ok) ? 0 : 1;
}

/* Run one case in a forked child with a 60 s deadline.  Returns 1 on PASS. */
static int run_case(int id) {
    pid_t pid = fork();
    struct timespec deadline;
    struct timespec now;
    struct timespec nap;
    int status = 0;
    pid_t r;

    if (pid < 0)
        return 0;
    if (pid == 0) {
        int rc = worker(id);
        fflush(stdout);
        _exit(rc == 0 ? 0 : 1);
    }

    clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += TIMEOUT_SEC;
    nap.tv_sec = 0;
    nap.tv_nsec = 2000000; /* 2 ms */
    for (;;) {
        r = waitpid(pid, &status, WNOHANG);
        if (r == pid)
            return (WIFEXITED(status) && WEXITSTATUS(status) == 0) ? 1 : 0;
        if (r < 0)
            return 0;
        clock_gettime(CLOCK_MONOTONIC, &now);
        if (now.tv_sec > deadline.tv_sec ||
            (now.tv_sec == deadline.tv_sec && now.tv_nsec >= deadline.tv_nsec)) {
            kill(pid, SIGKILL);
            waitpid(pid, &status, 0);
            printf("BLK %d timeout TIMEOUT FAIL 1\n", id);
            return 0;
        }
        nanosleep(&nap, (struct timespec *)0);
    }
}

int main(int argc, char **argv) {
    int id;
    int passed = 0;
    int failed = 0;
    if (argc > 1) {
        /* Single-worker mode: handy for a direct `timeout 60 proof <id>`. */
        return worker(atoi(argv[1]));
    }
    for (id = 0; id < NCASES; id++) {
        if (run_case(id))
            passed++;
        else
            failed++;
    }
    printf("BLUE20 containers %d %d %d\n", NCASES, passed, failed);
    fflush(stdout);
    return failed == 0 ? 0 : 1;
}
