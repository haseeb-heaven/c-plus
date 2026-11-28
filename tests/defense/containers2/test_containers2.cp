/* CONTAINERS2 -- deterministic container/memory hardening harness.
 *
 * Scope: stdlib/vector.hp, string.hp, collections.hp, memory.hp.
 *
 * METHOD
 * ------
 * Fixed-seed xorshift64* PRNG reseeded from the scenario id.  N scenarios
 * (default 1200), family = id % 8 so each family gets exactly 150 cases.
 * Every scenario executes in its own fork()ed child with an alarm() watchdog;
 * a global 64-byte sentinel plus two 64-byte guard allocations are armed before
 * the workload and verified afterwards.  Child exit code encodes:
 *   bit 0 : verdict ok (1) / logic failure (0)
 *   bit 1 : sentinel intact
 *   bit 2 : guard blocks intact
 * Parent requires all three bits, so a crash, hang, logic error or canary
 * corruption is reported as a failure for that scenario.
 *
 * Each family checks a model or an invariant:
 *   0 vector model (push/insert/erase/pop/reserve/shrink/clear)
 *   1 vector failure-atomicity under injected allocation failures
 *   2 string model (append, self-append, clear, reserve, copy_from)
 *   3 string failure-atomicity + null-termination/capacity accounting
 *   4 intmap model (put/remove/clear/get/contains, tombstones)
 *   5 intset + strmap model (owned-key rebuild/removal)
 *   6 deque/queue/stack/list ring wraparound and LIFO/FIFO order
 *   7 memory bounded queries, stale-generation tokens, LARGE element counts
 *
 * Family 7 includes the regression for the confirmed large-count query defect:
 * stdlib_mem_count() on a 70000..119999 byte block with elem_size 1 must return
 * the exact byte count.  Before the fix it saturates at 65535.
 *
 * OUTPUT
 *   S <id> <family> <verdict> <sentinel> <guard>
 *   FAMILY <family> <total> <passed> <failed>
 *   CONTAINERS2 <total> <passed> <failed>
 * This is EVIDENCE, not a proof: fixed deterministic inputs cannot establish
 * the absence of all defects.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/collections.hp"
#include "stdlib/memory.hp"

#define SCEN_N 1200
#define FAMILY_N 8
#define WATCHDOG_SEC 10

/* ---- deterministic PRNG (xorshift64*) ---------------------------------- */
static unsigned long long g_rng;

static unsigned long long mix64(unsigned long long x) {
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ULL;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBULL;
    x ^= x >> 31;
    return x;
}

static unsigned long long rnext(void) {
    unsigned long long x = g_rng;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_rng = x;
    return x * 2685821657736338717ULL;
}

/* Uniform [0,n) via multiply-shift (no variable division). n > 0. */
static size_t rbelow(unsigned long long r, size_t n) {
    unsigned long long hi = r >> 32;
    return (size_t)((hi * (unsigned long long)n) >> 32);
}

/* ---- canaries ----------------------------------------------------------- */
static volatile unsigned char g_sentinel[64];
static void *g_guard_lo;
static void *g_guard_hi;

static void canary_arm(void) {
    size_t i;
    for (i = 0; i < 64; i++)
        g_sentinel[i] = (unsigned char)(0xA5u ^ (unsigned char)i);
    g_guard_lo = stdlib_alloc(64);
    g_guard_hi = stdlib_alloc(64);
    if (g_guard_lo != (void *)0)
        memset(g_guard_lo, 0x5A, 64);
    if (g_guard_hi != (void *)0)
        memset(g_guard_hi, 0x3C, 64);
}

static int canary_ok(void) {
    size_t i;
    for (i = 0; i < 64; i++)
        if (g_sentinel[i] != (unsigned char)(0xA5u ^ (unsigned char)i))
            return 0;
    if (g_guard_lo != (void *)0) {
        for (i = 0; i < 64; i++)
            if (((unsigned char *)g_guard_lo)[i] != 0x5A)
                return 0;
    }
    if (g_guard_hi != (void *)0) {
        for (i = 0; i < 64; i++)
            if (((unsigned char *)g_guard_hi)[i] != 0x3C)
                return 0;
    }
    return 1;
}

/* ---- family 0: vector model -------------------------------------------- */
static int fam_vector_model(void) {
    vector v = vector();
    int v0ref[128];
    size_t n = 0;
    size_t step;
    size_t i;
    int val;

    for (step = 0; step < 32; step++) {
        unsigned long long r = rnext();
        size_t op = rbelow(r, 8);
        size_t idx;
        if (op == 0 || op == 7) {
            val = (int)(r >> 40) & 0x7FFF;
            if (v.push_back(val) != 0)
                return 0;
            if (n < 128) v0ref[n] = val;
            n++;
        } else if (op == 1 && n > 0) {
            val = (int)(r >> 40) & 0x7FFF;
            idx = rbelow(r >> 8, n);
            if (v.insert(idx, val) != 0)
                return 0;
            i = n;
            while (i > idx) { v0ref[i] = v0ref[i - 1]; i--; }
            v0ref[idx] = val;
            n++;
        } else if (op == 2 && n > 0) {
            idx = rbelow(r >> 8, n);
            if (v.erase(idx, (int *)0) != 0)
                return 0;
            for (i = idx; i + 1 < n; i++) v0ref[i] = v0ref[i + 1];
            n--;
        } else if (op == 3 && n > 0) {
            int got = 0;
            if (v.pop_back(&got) != 0 || got != v0ref[n - 1])
                return 0;
            n--;
        } else if (op == 4) {
            size_t want = v.capacity() + rbelow(r >> 16, 8) + 1;
            if (v.reserve(want) != 0)
                return 0;
        } else if (op == 5) {
            if (v.shrink_to_fit() != 0)
                return 0;
        } else if (op == 6) {
            v.clear();
            n = 0;
        }
        if (v.size() != n || v.capacity() < v.size())
            return 0;
        for (i = 0; i < n; i++) {
            int got = 0;
            if (v.at_checked(i, &got) != 0 || got != v0ref[i])
                return 0;
        }
        {
            int got = 0;
            if (v.at_checked(n, &got) != -1)
                return 0;
        }
        if (!canary_ok())
            return 0;
    }
    {
        slice snap = v.view();
        int got = 0;
        if (snap.size() != v.size())
            return 0;
        if (snap.valid() && snap.at_checked(0, &got) != 0)
            return 0;
        /* growing storage invalidates the borrow */
        if (v.reserve(v.capacity() + 8) != 0)
            return 0;
        if (!snap.stale() || snap.valid())
            return 0;
        if (snap.at_checked(0, &got) != -1)
            return 0;
    }
    v.drop();
    v.drop(); /* idempotent */
    return canary_ok() ? 1 : 0;
}

/* ---- family 1: vector failure atomicity -------------------------------- */
static int fam_vector_atomic(void) {
    vector v = vector();
    int k;

    for (k = 0; k < 20; k++) {
        if (v.push_back(k * 7 + 1) != 0)
            return 0;
    }
    /* Snapshot then inject a failure at each early allocation point.  If the
     * call refuses, size/capacity/contents must be byte-for-byte unchanged. */
    for (k = 1; k <= 3; k++) {
        size_t before_size = v.size();
        size_t before_cap = v.capacity();
        int before_vals[64];
        size_t j;
        int rc;
        for (j = 0; j < before_size; j++)
            if (v.at_checked(j, &before_vals[j]) != 0)
                return 0;
        stdlib_alloc_fail_at(k);
        if (k == 1)
            rc = v.reserve(v.capacity() + 32);
        else if (k == 2)
            rc = v.insert(before_size, 4242);
        else {
            vector vsrc = vector();
            if (vsrc.push_back(77) != 0)
                return 0;
            rc = v.copy_from(&vsrc);
            vsrc.drop();
        }
        stdlib_alloc_fail_reset();
        if (rc == -1) {
            if (v.size() != before_size || v.capacity() != before_cap)
                return 0;
            for (j = 0; j < before_size; j++) {
                int got = 0;
                if (v.at_checked(j, &got) != 0 || got != before_vals[j])
                    return 0;
            }
        }
        if (!canary_ok())
            return 0;
    }
    v.drop();
    return canary_ok() ? 1 : 0;
}

/* ---- family 2: string model -------------------------------------------- */
static const char *const STR_KEYS[] = {
    "alpha", "beta", "gamma", "delta", "eps", "zeta", "eta", "theta"
};

static int fam_string_model(void) {
    string s = string("seed");
    char s2ref[4096];
    size_t n = 4;
    size_t step;
    size_t i;
    size_t alloc_bytes;

    memcpy(s2ref, "seed", 4);
    for (step = 0; step < 28; step++) {
        unsigned long long r = rnext();
        size_t op = rbelow(r, 8);
        if (op == 0 || op == 1 || op == 2) {
            const char *text = STR_KEYS[rbelow(r >> 8, 8)];
            size_t extra = strlen(text);
            if (s.append(text) != 0)
                return 0;
            memcpy(s2ref + n, text, extra);
            n += extra;
        } else if (op == 3 && n > 1 && n < 2048) {
            /* self-append a suffix: s += s.c_str()+off */
            size_t off = rbelow(r >> 8, n - 1);
            const char *self = s.c_str() + off;
            size_t extra = n - off;
            if (s.append(self) != 0)
                return 0;
            memcpy(s2ref + n, s2ref + off, extra);
            n += extra;
        } else if (op == 4) {
            s.clear();
            n = 0;
        } else if (op == 5) {
            if (s.reserve(s.capacity() + rbelow(r >> 8, 16) + 1) != 0)
                return 0;
        } else if (op == 6) {
            string other = string("copy-source");
            if (s.copy_from(&other) != 0)
                return 0;
            memcpy(s2ref, "copy-source", 11);
            n = 11;
            other.drop();
        }
        if (s.size() != n)
            return 0;
        if (memcmp(s.c_str(), s2ref, n) != 0)
            return 0;
        if (s.c_str()[n] != '\0')
            return 0;
        if (s.valid()) {
            alloc_bytes = stdlib_alloc_size((const void *)s.c_str());
            if (alloc_bytes != s.capacity())
                return 0;
        }
        for (i = 0; i < n; i++)
            if (s.at(i) != s2ref[i])
                return 0;
        if (!canary_ok())
            return 0;
    }
    {
        char out[64];
        if (s.count_fields(',') == 0)
            return 0;
        if (s.substr(0, 5, out, 64) != 0)
            return 0;
        if (s.substr(n, 4, out, 64) != 0 || out[0] != '\0')
            return 0;
        if (s.substr(n + 1, 4, out, 64) != -1)
            return 0;
        if (s.field(',', 0, out, 64) != 0)
            return 0;
    }
    s.drop();
    s.drop();
    return canary_ok() ? 1 : 0;
}

/* ---- family 3: string failure atomicity -------------------------------- */
static int fam_string_atomic(void) {
    string s = string("abcdefghij");
    char s3ref[256];
    size_t n = 10;
    size_t i;
    int k;
    memcpy(s3ref, "abcdefghij", 10);

    for (k = 1; k <= 2; k++) {
        size_t before_size = s.size();
        char before[64];
        int rc;
        memcpy(before, s.c_str(), before_size + 1);
        stdlib_alloc_fail_at(k);
        if (k == 1)
            rc = s.append("LONG-APPEND-STRING-0123456789");
        else
            rc = s.reserve(s.capacity() + 64);
        stdlib_alloc_fail_reset();
        if (rc == -1) {
            if (s.size() != before_size)
                return 0;
            if (memcmp(s.c_str(), before, before_size + 1) != 0)
                return 0;
        }
        if (!canary_ok())
            return 0;
    }
    /* Self-append both in-place and with reallocation. */
    if (s.reserve(128) != 0)
        return 0;
    if (s.append(s.c_str() + 2) != 0)
        return 0;
    memcpy(s3ref + n, s3ref + 2, n - 2);
    n += n - 2;
    if (s.size() != n || memcmp(s.c_str(), s3ref, n) != 0 || s.c_str()[n] != '\0')
        return 0;
    for (i = 0; i < 40; i++) {
        if (s.append("z") != 0)
            return 0;
        n++;
    }
    if (s.size() != n || s.c_str()[n] != '\0')
        return 0;
    s.drop();
    return canary_ok() ? 1 : 0;
}

/* ---- family 4: intmap model -------------------------------------------- */
static int fam_intmap(void) {
    intmap m = intmap();
    int m4ref[64];
    unsigned char present[64];
    size_t i;
    size_t step;
    memset(present, 0, sizeof(present));
    memset(m4ref, 0, sizeof(m4ref));

    for (step = 0; step < 40; step++) {
        unsigned long long r = rnext();
        size_t key = rbelow(r, 64);
        size_t op = rbelow(r >> 16, 8);
        if (op < 5) {
            int value = (int)(r >> 32) & 0x7FFF;
            if (m.put((int)key, value) != 0)
                return 0;
            m4ref[key] = value;
            present[key] = 1;
        } else if (op == 5 || op == 6) {
            if (present[key]) {
                if (m.remove((int)key) != 0)
                    return 0;
                present[key] = 0;
            } else {
                int out = 0;
                if (m.get((int)key, &out) != -1)
                    return 0;
            }
        } else {
            m.clear();
            memset(present, 0, sizeof(present));
        }
        {
            size_t expect = 0;
            for (i = 0; i < 64; i++) {
                int out = 0;
                if (present[i]) {
                    expect++;
                    if (m.get((int)i, &out) != 0 || out != m4ref[i])
                        return 0;
                    if (m.contains((int)i) != 1)
                        return 0;
                } else {
                    if (m.contains((int)i) != 0)
                        return 0;
                }
            }
            if (m.size() != expect)
                return 0;
        }
        if (!canary_ok())
            return 0;
    }
    m.drop();
    m.drop();
    return canary_ok() ? 1 : 0;
}

/* ---- family 5: intset + strmap ----------------------------------------- */
static int fam_intset_strmap(void) {
    intset set = intset();
    strmap sm = strmap();
    unsigned char present[64];
    unsigned char smpresent[8];
    int sval[64];
    size_t i;
    size_t step;
    memset(present, 0, sizeof(present));
    memset(smpresent, 0, sizeof(smpresent));
    memset(sval, 0, sizeof(sval));
    /* Seed the owned-key map with a known value for every key it will report. */
    for (i = 0; i < 8; i++) {
        if (sm.put(STR_KEYS[i], 0) != 0)
            return 0;
        smpresent[i] = 1;
    }

    for (step = 0; step < 36; step++) {
        unsigned long long r = rnext();
        size_t key = rbelow(r, 64);
        size_t op = rbelow(r >> 16, 8);
        if (op < 4) {
            if (set.insert((int)key) != 0)
                return 0;
            present[key] = 1;
        } else if (op == 4) {
            if (present[key]) {
                if (set.remove((int)key) != 0)
                    return 0;
                present[key] = 0;
            }
        } else if (op == 5 || op == 6) {
            const char *k = STR_KEYS[key & 7u];
            int value = (int)(r >> 32) & 0x7FFF;
            if (sm.put(k, value) != 0)
                return 0;
            sval[key & 7u] = value;
            smpresent[key & 7u] = 1;
        } else if (op == 7) {
            if (((r >> 40) & 1ULL) != 0) {
                set.clear();
                sm.clear();
                memset(present, 0, sizeof(present));
                memset(smpresent, 0, sizeof(smpresent));
                memset(sval, 0, sizeof(sval));
            } else {
                const char *k = STR_KEYS[key & 7u];
                if (smpresent[key & 7u]) {
                    if (sm.remove(k) != 0)
                        return 0;
                    smpresent[key & 7u] = 0;
                } else {
                    int out = 0;
                    if (sm.get(k, &out) != -1)
                        return 0;
                }
            }
        } else {
            set.clear();
            sm.clear();
            memset(present, 0, sizeof(present));
            memset(smpresent, 0, sizeof(smpresent));
            memset(sval, 0, sizeof(sval));
        }
        {
            size_t expect = 0;
            for (i = 0; i < 64; i++) {
                if (present[i] != (unsigned char)(set.contains((int)i) ? 1 : 0))
                    return 0;
                if (present[i]) expect++;
            }
            if (set.size() != expect)
                return 0;
            for (i = 0; i < 8; i++) {
                int out = 0;
                const char *k = STR_KEYS[i];
                if (smpresent[i]) {
                    if (sm.get(k, &out) != 0 || out != sval[i])
                        return 0;
                } else {
                    if (sm.get(k, &out) != -1)
                        return 0;
                }
            }
        }
        if (!canary_ok())
            return 0;
    }
    set.drop();
    sm.drop();
    return canary_ok() ? 1 : 0;
}

/* ---- family 6: deque/queue/stack/list ---------------------------------- */
static int fam_ring(void) {
    deque dq = deque();
    queue q = queue();
    stack st = stack();
    list li = list();
    size_t i;
    int got = 0;

    /* Force several wraparounds via interleaved front/back growth. */
    for (i = 0; i < 64; i++) {
        if ((i & 1u) == 0) {
            if (dq.push_back((int)i) != 0) return 0;
        } else {
            if (dq.push_front((int)i) != 0) return 0;
        }
        if (q.push((int)i) != 0) return 0;
        if (st.push((int)i) != 0) return 0;
        if (li.push_back((int)i) != 0) return 0;
    }
    if (dq.size() != 64 || q.size() != 64 || st.size() != 64 || li.size() != 64)
        return 0;
    /* Deque: front half is descending odd, back half ascending even. */
    for (i = 0; i < 32; i++) {
        if (dq.at_checked(i, &got) != 0 || got != (int)(63 - i * 2))
            return 0;
        if (q.front_checked(&got) != 0 || got != (int)i)
            return 0;
        if (q.pop(&got) != 0 || got != (int)i)
            return 0;
        if (li.at_checked(i, &got) != 0 || got != (int)i)
            return 0;
    }
    for (i = 0; i < 32; i++) {
        if (dq.at_checked(32 + i, &got) != 0 || got != (int)(i * 2))
            return 0;
    }
    /* Refill queue after wrap, then drain in FIFO order. */
    for (i = 64; i < 96; i++)
        if (q.push((int)i) != 0) return 0;
    for (i = 32; i < 96; i++)
        if (q.pop(&got) != 0 || got != (int)i) return 0;
    if (q.size() != 0)
        return 0;
    /* Stack LIFO, list FIFO. */
    for (i = 0; i < 64; i++) {
        if (st.pop(&got) != 0 || got != (int)(63 - i)) return 0;
        if (li.pop_front(&got) != 0 || got != (int)i) return 0;
    }
    for (i = 0; i < 64; i++) {
        if (dq.pop_front(&got) != 0) return 0;
    }
    if (dq.size() != 0 || st.size() != 0 || li.size() != 0)
        return 0;
    dq.drop(); q.drop(); st.drop(); li.drop();
    return canary_ok() ? 1 : 0;
}

/* ---- family 7: memory queries + LARGE counts --------------------------- */
static int fam_memory(void) {
    size_t bytes = 70000 + rbelow(g_rng, 50000);
    void *p = stdlib_alloc(bytes);
    unsigned long long tok;
    const void *base = (const void *)0;
    size_t size = 0;
    size_t cnt = 12345;
    size_t off;
    error err = error();
    void *fresh;

    if (p == (void *)0)
        return 0;
    tok = stdlib_mem_token(p);
    if (tok == 0)
        return 0;
    if (stdlib_mem_capacity(p) != bytes)
        return 0;
    if (stdlib_mem_bounds(p, &base, &size) != 0)
        return 0;
    if (base != p || size != bytes)
        return 0;
    if (!stdlib_mem_span_ok(base, size, 0, bytes))
        return 0;
    if (stdlib_mem_span_ok(base, size, bytes, 1))
        return 0;
    off = bytes - 8;
    if (!stdlib_mem_span_contains(base, size, (const char *)base + off, 8))
        return 0;
    if (stdlib_mem_span_contains(base, size, (const char *)base + off, 9))
        return 0;
    /* THE REGRESSION: exact element count for a large tracked block. */
    if (stdlib_mem_count(p, 1, &cnt) != 0 || cnt != bytes)
        return 0;
    /* elem_size 8 keeps the quotient below 2^16 (passes before and after). */
    if (stdlib_mem_count(p, 8, &cnt) != 0 || cnt != bytes / 8u)
        return 0;
    /* A small block must remain exact (guards against over-correction). */
    fresh = stdlib_alloc(4096);
    if (fresh == (void *)0)
        return 0;
    if (stdlib_mem_count(fresh, 1, &cnt) != 0 || cnt != 4096)
        return 0;
    stdlib_release(fresh);
    if (stdlib_mem_query(p, off, 8, tok, &err) != STDLIB_OK)
        return 0;
    if (stdlib_mem_query(p, bytes, 1, tok, &err) != STDLIB_FAIL)
        return 0;
    if (stdlib_mem_query(p, 0, bytes + 1, tok, &err) != STDLIB_FAIL)
        return 0;
    /* Stale token detection. */
    if (stdlib_mem_check(p, tok, &err) != STDLIB_OK)
        return 0;
    stdlib_release(p);
    if (stdlib_mem_key_current(p, tok) != 0)
        return 0;
    if (stdlib_mem_check(p, tok, &err) != STDLIB_FAIL)
        return 0;
    if (stdlib_mem_count(p, 1, &cnt) != -1 || cnt != 0)
        return 0;
    {
        void *src = stdlib_alloc(128);
        void *dst = (void *)0;
        void *alias;
        unsigned long long old_token;
        if (src == (void *)0)
            return 0;
        old_token = stdlib_mem_token(src);
        if (stdlib_mem_transfer(&dst, &src, &err) != STDLIB_OK)
            return 0;
        if (src != (void *)0 || dst == (void *)0)
            return 0;
        if (stdlib_mem_key_current(dst, old_token) != 0)
            return 0;
        if (stdlib_mem_check(dst, old_token, &err) != STDLIB_FAIL)
            return 0;
        /* a stale alias whose address is still tracked must be refused */
        alias = dst;
        if (stdlib_mem_release_checked(&dst, 0, &err) != STDLIB_OK)
            return 0;
        if (dst != (void *)0)
            return 0;
        if (stdlib_mem_release_checked(&alias, 0, &err) != STDLIB_FAIL)
            return 0;
    }
    return canary_ok() ? 1 : 0;
}

/* ---- dispatcher --------------------------------------------------------- */
static int run_family(int family) {
    switch (family) {
    case 0: return fam_vector_model();
    case 1: return fam_vector_atomic();
    case 2: return fam_string_model();
    case 3: return fam_string_atomic();
    case 4: return fam_intmap();
    case 5: return fam_intset_strmap();
    case 6: return fam_ring();
    default: return fam_memory();
    }
}

static void child_run(int family) {
    int verdict;
    alarm(WATCHDOG_SEC);
    canary_arm();
    if (!canary_ok())
        _exit(0);
    verdict = run_family(family);
    _exit((verdict != 0 ? 1 : 0) | (canary_ok() ? 2 : 0));
}

int main(void) {
    int total = SCEN_N;
    int id;
    int passed = 0;
    int failed = 0;
    int fam_total[FAMILY_N];
    int fam_pass[FAMILY_N];
    int fam_fail[FAMILY_N];
    int f;

    for (f = 0; f < FAMILY_N; f++) {
        fam_total[f] = 0;
        fam_pass[f] = 0;
        fam_fail[f] = 0;
    }

    for (id = 0; id < total; id++) {
        int family = id % FAMILY_N;
        pid_t pid;
        int status = 0;
        int ok = 0;
        g_rng = mix64(0xC0FFEE0DF02A5EEDULL ^
                      ((unsigned long long)id + 1ULL) * 0x9E3779B97F4A7C15ULL);
        fflush(NULL);
        pid = fork();
        if (pid == 0) {
            child_run(family);
            _exit(0);
        }
        if (pid < 0) {
            ok = 0;
        } else {
            while (waitpid(pid, &status, 0) < 0) { /* retry */ }
            if (WIFEXITED(status) && WEXITSTATUS(status) == 3)
                ok = 1;
        }
        fam_total[family]++;
        if (ok) {
            passed++;
            fam_pass[family]++;
        } else {
            failed++;
            fam_fail[family]++;
        }
        printf("S %d %d %d %d\n", id, family, ok ? 1 : 0, status);
    }
    for (f = 0; f < FAMILY_N; f++)
        printf("FAMILY %d %d %d %d\n", f, fam_total[f], fam_pass[f], fam_fail[f]);
    printf("CONTAINERS2 %d %d %d\n", total, passed, failed);
    return failed == 0 ? 0 : 1;
}
