/* FUZZ10K containers -- deterministic fuzz over the C+ container / memory
 * helpers (stdlib/vector.hp, string.hp, collections.hp, memory.hp).
 *
 * METHOD
 * ------
 * Exactly TOTAL scenarios (id 0..TOTAL-1, TOTAL > 10000) are executed.  A
 * fixed master seed is mixed with the scenario id by a xorshift64* PRNG, so
 * every scenario is reproducible in isolation and no runtime address ever
 * enters the log.  Family is id % 8.
 *
 * The parent process forks one child per BATCH scenarios.  Each child writes
 * one S line per scenario plus a BATCH summary to its own results/batch_<b>.log
 * (stdout is freopen'd there and flushed after every line).  The parent polls
 * each child with waitpid(WNOHANG) on a monotonic deadline: a child that dies
 * from a signal is a CRASH, a child that exceeds the per-batch deadline is
 * killed with SIGKILL and counted as a HANG.  This isolates faults per batch.
 *
 * Each scenario checks: bounds refusal or a correct result, a 64-byte global
 * sentinel plus (write family) an in-block/adjacent-block byte canary, and
 * invariants (size <= capacity, every element matches an independent model)
 * after every operation.  A failure of any of these is a CORRUPTION.
 *
 * OUTPUT (parent stdout):
 *   FUZZ10K containers <scenarios> <crashes> <hangs> <corruptions> <refusals>
 * Extra diagnostics go to stderr.  Exit is nonzero if any crash, hang,
 * corruption, missing batch or wait error was observed.
 *
 * This is EVIDENCE, not a proof: it exercises defined public entry points
 * through deterministic observations; it cannot cover every program state.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"
#include "stdlib/memory.hp"
#include "stdlib/collections.hp"

#define TOTAL 10010
#define BATCH 50
#define NBATCH 201
#define BATCH_TIMEOUT_SEC 12
#define MASTER_SEED 0x1B56C0DE5EED1234ULL
#define RESULTS_DIR "tests/fuzz10k/containers/results"

/* Dormant fault-injection for validating the crash/hang containment itself.
 * Set FUZZ10K_FAULT=crash or =hang to force one child (at id 137) to die by a
 * signal or to spin forever.  The default (unset) run never uses these. */
static int g_fault_crash = 0;
static int g_fault_hang = 0;

/* ---- deterministic PRNG (xorshift64*), no variable division -------------- */

static unsigned long long g_state;

static void rng_seed(int id) {
    g_state = MASTER_SEED ^
              ((unsigned long long)id * 0x9E3779B97F4A7C15ULL);
}

static unsigned long long rnext(void) {
    unsigned long long x = g_state;
    x ^= x >> 12;
    x ^= x << 25;
    x ^= x >> 27;
    g_state = x;
    return x * 2685821657736338717ULL;
}

/* Uniform value in [0, n) via multiply-shift of the high 32 bits.  n > 0. */
static size_t rbelow(unsigned long long r, size_t n) {
    unsigned long long hi = r >> 32;
    return (size_t)((hi * (unsigned long long)n) >> 32);
}

/* ---- scenario result channels (set by each family) ---------------------- */

static const char *g_api = "none";
static const char *g_case = "none";
static const char *g_result = "pass";
static int g_refused = 0;
static int g_canary_ok = 1;
static int g_failure = 0;

/* ---- global sentinel ---------------------------------------------------- */

#define SENT_LEN 64
static unsigned char g_sent[SENT_LEN];

static unsigned char sent_byte(int id, size_t i) {
    return (unsigned char)(((unsigned long long)id * 131ULL +
                            (unsigned long long)i * 37ULL + 0x5AULL) &
                           0xFFULL);
}

static void sent_fill(int id) {
    size_t i;
    for (i = 0; i < SENT_LEN; i++)
        g_sent[i] = sent_byte(id, i);
}

static int sent_check(int id) {
    size_t i;
    for (i = 0; i < SENT_LEN; i++)
        if (g_sent[i] != sent_byte(id, i))
            return 0;
    return 1;
}

/* ---- shared reference helpers ------------------------------------------- */

static int vec_val(size_t i) {
    return (int)(i * 3u + 1u);
}

/* Independent reference for the spatial predicate in memory.hp. */
static int span_exp(size_t cap, size_t off, size_t n) {
    if (cap == 0)
        return 0;
    if (off > cap)
        return 0;
    if (n > cap - off)
        return 0;
    return 1;
}

/* Adversarial index spectrum: -1(SIZE_MAX), 0, size-1, size, size+1, small,
 * half, random (may be OOB), one. */
static size_t pick_index(unsigned long long r, size_t size) {
    size_t pick = rbelow(r, 9);
    size_t idx;
    if (pick == 0) {
        idx = 0;
        g_case = "idx0";
    } else if (pick == 1) {
        idx = size;
        g_case = "size";
    } else if (pick == 2) {
        idx = size == 0 ? (size_t)-1 : size - 1;
        g_case = "last";
    } else if (pick == 3) {
        idx = size + 1;
        g_case = "size1";
    } else if (pick == 4) {
        idx = (size_t)-1;
        g_case = "max";
    } else if (pick == 5) {
        idx = 3;
        g_case = "small";
    } else if (pick == 6) {
        idx = size / 2;
        g_case = "half";
    } else if (pick == 7) {
        idx = rbelow(rnext(), size + 2);
        g_case = "rand";
    } else {
        idx = 1;
        g_case = "one";
    }
    return idx;
}

/* Adversarial length spectrum including 0, 1, SIZE_MAX, cap, random. */
static size_t pick_n(unsigned long long r, size_t cap) {
    size_t c = rbelow(r, 5);
    if (c == 0)
        return 0;
    if (c == 1)
        return 1;
    if (c == 2)
        return SIZE_MAX;
    if (c == 3)
        return cap;
    return rbelow(rnext(), cap + 2);
}

/* ======================================================================== */
/* Family 0 -- vector at / at_checked bounds                                 */
/* ======================================================================== */
static void fam_vector_at(int id) {
    size_t size, idx, i;
    int out, rc, expect_refuse, bad;
    vector v = vector();
    (void)id;
    g_api = "vec.at";
    bad = 0;
    size = rbelow(rnext(), 2049);
    for (i = 0; i < size; i++)
        if (v.push_back(vec_val(i)) != 0)
            bad = 1;
    idx = pick_index(rnext(), size);
    expect_refuse = idx >= size ? 1 : 0;

    out = -7777;
    rc = v.at_checked(idx, &out);
    if (expect_refuse) {
        g_refused = 1;
        if (rc != -1 || out != -7777)
            bad = 1;
        if (v.at(idx) != 0)
            bad = 1;
    } else {
        if (rc != 0 || out != vec_val(idx))
            bad = 1;
        if (v.at(idx) != vec_val(idx))
            bad = 1;
    }
    if (v.size() != size)
        bad = 1;
    if (v.size() > v.capacity())
        bad = 1;
    for (i = 0; i < size; i++) {
        int t = 0;
        if (v.at_checked(i, &t) != 0 || t != vec_val(i))
            bad = 1;
    }
    if (bad)
        g_failure = 1;
    v.drop();
}

/* ======================================================================== */
/* Family 1 -- vector push / insert / erase / reserve / shrink growth        */
/* ======================================================================== */
static void fam_vector_mut(int id) {
    size_t size, idx, i, want;
    int rc, bad, out;
    vector v = vector();
    (void)id;
    g_api = "vec.mut";
    bad = 0;
    size = rbelow(rnext(), 2049);
    for (i = 0; i < size; i++)
        v.push_back(vec_val(i));
    {
        size_t op = rbelow(rnext(), 7);

        if (op == 0) {
            g_case = "push";
            rc = v.push_back(4242);
            if (rc != 0 || v.size() != size + 1)
                bad = 1;
            {
                int t = 0;
                if (v.at_checked(size, &t) != 0 || t != 4242)
                    bad = 1;
            }
        } else if (op == 1) {
            idx = pick_index(rnext(), size);
            rc = v.insert(idx, 5555);
            if (idx > size) {
                g_refused = 1;
                if (rc != -1 || v.size() != size)
                    bad = 1;
            } else {
                int t = 0;
                if (rc != 0 || v.size() != size + 1)
                    bad = 1;
                for (i = 0; i < idx; i++)
                    if (v.at_checked(i, &t) != 0 || t != vec_val(i))
                        bad = 1;
                if (v.at_checked(idx, &t) != 0 || t != 5555)
                    bad = 1;
                for (i = idx + 1; i < size + 1; i++)
                    if (v.at_checked(i, &t) != 0 || t != vec_val(i - 1))
                        bad = 1;
            }
        } else if (op == 2) {
            out = 0;
            idx = pick_index(rnext(), size);
            rc = v.erase(idx, &out);
            if (idx >= size) {
                g_refused = 1;
                if (rc != -1 || v.size() != size)
                    bad = 1;
            } else {
                int t = 0;
                if (rc != 0 || v.size() != size - 1 || out != vec_val(idx))
                    bad = 1;
                for (i = 0; i < idx; i++)
                    if (v.at_checked(i, &t) != 0 || t != vec_val(i))
                        bad = 1;
                for (i = idx; i < size - 1; i++)
                    if (v.at_checked(i, &t) != 0 || t != vec_val(i + 1))
                        bad = 1;
            }
        } else if (op == 3) {
            g_case = "reserve_limit";
            want = (STDLIB_ALLOC_LIMIT / sizeof(int)) + 1;
            rc = v.reserve(want);
            g_refused = 1;
            if (rc != -1 || v.size() != size)
                bad = 1;
        } else if (op == 4) {
            g_case = "reserve_max";
            g_refused = 1;
            if (v.reserve(SIZE_MAX) != -1 || v.size() != size)
                bad = 1;
        } else if (op == 5) {
            g_case = "shrink";
            rc = v.shrink_to_fit();
            if (rc != 0 || v.capacity() != size)
                bad = 1;
        } else {
            g_case = "pop";
            if (size == 0) {
                g_refused = 1;
                if (v.pop_back(&out) != -1)
                    bad = 1;
            } else {
                out = 0;
                if (v.pop_back(&out) != 0 || out != vec_val(size - 1) ||
                    v.size() != size - 1)
                    bad = 1;
            }
        }
    }
    if (v.size() > v.capacity())
        bad = 1;
    for (i = 0; i < v.size(); i++) {
        int t = 0;
        if (v.at_checked(i, &t) != 0)
            bad = 1;
    }
    if (bad)
        g_failure = 1;
    v.drop();
}

/* ======================================================================== */
/* Family 2 -- random OPERATION SEQUENCES with an independent model and an
 * invariant check after every operation.                                     */
/* ======================================================================== */
#define MODEL_CAP 4096
static size_t pick_reserve(unsigned long long r, size_t size) {
    size_t c = rbelow(r, 5);
    if (c == 0)
        return SIZE_MAX;
    if (c == 1)
        return (STDLIB_ALLOC_LIMIT / sizeof(int)) + 1;
    if (c == 2)
        return size;
    if (c == 3)
        return size + rbelow(rnext(), 64);
    return rbelow(rnext(), 4096);
}

static void fam_vector_seq(int id) {
    int model[MODEL_CAP];
    size_t size, i, step, nops, want, idx;
    int rc, bad, val, out;
    vector v = vector();
    (void)id;
    g_api = "vec.seq";
    bad = 0;
    size = rbelow(rnext(), 2049);
    for (i = 0; i < size; i++) {
        model[i] = vec_val(i);
        v.push_back(model[i]);
    }
    nops = 8 + rbelow(rnext(), 9); /* 8..16 operations */
    val = 100;
    for (step = 0; step < nops; step++) {
        size_t op = rbelow(rnext(), 8);
        if (op == 0) {
            g_case = "push";
            val = (int)step * 100 + 7;
            rc = v.push_back(val);
            if (rc != 0 || v.size() != size + 1)
                bad = 1;
            else {
                model[size] = val;
                size++;
            }
        } else if (op == 1) {
            idx = pick_index(rnext(), size);
            g_case = "ins";
            rc = v.insert(idx, val);
            if (idx > size) {
                g_refused = 1;
                if (rc != -1 || v.size() != size)
                    bad = 1;
            } else if (rc != 0 || v.size() != size + 1) {
                bad = 1;
            } else {
                for (i = size; i > idx; i--)
                    model[i] = model[i - 1];
                model[idx] = val;
                size++;
            }
        } else if (op == 2) {
            idx = pick_index(rnext(), size);
            g_case = "erase";
            out = -999999;
            rc = v.erase(idx, &out);
            if (idx >= size) {
                g_refused = 1;
                if (rc != -1 || v.size() != size || out != -999999)
                    bad = 1;
            } else if (rc != 0 || out != model[idx] || v.size() != size - 1) {
                bad = 1;
            } else {
                for (i = idx; i + 1 < size; i++)
                    model[i] = model[i + 1];
                size--;
            }
        } else if (op == 3) {
            g_case = "pop";
            out = -999999;
            rc = v.pop_back(&out);
            if (size == 0) {
                g_refused = 1;
                if (rc != -1)
                    bad = 1;
            } else if (rc != 0 || out != model[size - 1] ||
                       v.size() != size - 1) {
                bad = 1;
            } else {
                size--;
            }
        } else if (op == 4) {
            g_case = "reserve";
            want = pick_reserve(rnext(), size);
            rc = v.reserve(want);
            if (want > (STDLIB_ALLOC_LIMIT / sizeof(int))) {
                g_refused = 1;
                if (rc != -1)
                    bad = 1;
            } else if (rc != 0) {
                bad = 1;
            }
            if (v.size() != size)
                bad = 1;
        } else if (op == 5) {
            g_case = "shrink";
            rc = v.shrink_to_fit();
            if (rc != 0 || v.capacity() != v.size() || v.size() != size)
                bad = 1;
        } else if (op == 6) {
            idx = pick_index(rnext(), size);
            g_case = "at";
            out = -999999;
            rc = v.at_checked(idx, &out);
            if (idx >= size) {
                g_refused = 1;
                if (rc != -1)
                    bad = 1;
            } else if (rc != 0 || out != model[idx]) {
                bad = 1;
            }
        } else {
            g_case = "limit";
            want = (STDLIB_ALLOC_LIMIT / sizeof(int)) + 1;
            g_refused = 1;
            if (v.reserve(want) != -1 || v.size() != size)
                bad = 1;
        }

        /* invariants after EVERY operation */
        if (v.size() > v.capacity())
            bad = 1;
        if (v.size() != size)
            bad = 1;
        if (bad)
            break;
        for (i = 0; i < size; i++) {
            int t = 0;
            if (v.at_checked(i, &t) != 0 || t != model[i]) {
                bad = 1;
                break;
            }
        }
        if (bad)
            break;
    }
    if (bad)
        g_failure = 1;
    v.drop();
}

/* ======================================================================== */
/* Family 3 -- string append (incl self), reserve, substr, at                */
/* ======================================================================== */
static void fam_string(int id) {
    char src[2050];
    char out[40];
    size_t size, i, start, n;
    int rc, bad, op;
    string s = string("");
    (void)id;
    g_api = "str";
    bad = 0;
    size = rbelow(rnext(), 2049);
    for (i = 0; i < size; i++)
        src[i] = (char)('a' + (int)(i % 26u));
    src[size] = '\0';
    s = string(src);
    op = (int)rbelow(rnext(), 7);

    if (op == 0) {
        g_case = "append";
        rc = s.append("XYZ");
        if (rc != 0 || s.size() != size + 3)
            bad = 1;
        for (i = 0; i < size; i++)
            if (s.at(i) != (char)('a' + (int)(i % 26u)))
                bad = 1;
        if (s.at(size) != 'X' || s.at(size + 1) != 'Y' || s.at(size + 2) != 'Z')
            bad = 1;
    } else if (op == 1) {
        g_case = "self_append";
        rc = s.append(s.c_str());
        if (rc != 0 || s.size() != size + size)
            bad = 1;
        for (i = 0; i < size + size; i++) {
            size_t j = i < size ? i : i - size;
            if (s.at(i) != (char)('a' + (int)(j % 26u)))
                bad = 1;
        }
    } else if (op == 2) {
        g_case = "append_null";
        g_refused = 1;
        if (s.append(NULL) != -1 || s.size() != size)
            bad = 1;
    } else if (op == 3) {
        g_case = "reserve_max";
        g_refused = 1;
        if (s.reserve(SIZE_MAX) != -1 || s.size() != size)
            bad = 1;
    } else if (op == 4) {
        size_t expect_n;
        g_case = "substr";
        start = rbelow(rnext(), size + 2);
        n = pick_n(rnext(), size);
        for (i = 0; i < sizeof(out); i++)
            out[i] = (char)0x7E;
        rc = s.substr(start, n, out, sizeof(out));
        if (start > size) {
            g_refused = 1;
            if (rc != -1)
                bad = 1;
        } else {
            if (rc != 0)
                bad = 1;
            expect_n = n;
            if (expect_n > size - start)
                expect_n = size - start;
            if (expect_n + 1 > sizeof(out))
                expect_n = sizeof(out) - 1;
            for (i = expect_n + 1; i < sizeof(out); i++)
                if (out[i] != (char)0x7E)
                    bad = 1;
            for (i = 0; i < expect_n; i++)
                if (out[i] != s.at(start + i))
                    bad = 1;
            if (out[expect_n] != '\0')
                bad = 1;
        }
    } else if (op == 5) {
        g_case = "at";
        {
            size_t idx = pick_index(rnext(), size);
            if (idx >= size) {
                g_refused = 1;
                if (s.at(idx) != '\0')
                    bad = 1;
            } else if (s.at(idx) != (char)('a' + (int)(idx % 26u))) {
                bad = 1;
            }
        }
    } else {
        g_case = "growth_limit";
        g_refused = 1;
        if (stdlib_string_calculate_growth(STDLIB_ALLOC_LIMIT + 1, 4) != 0)
            bad = 1;
        if (stdlib_string_calculate_growth(STDLIB_ALLOC_LIMIT,
                                           STDLIB_ALLOC_LIMIT) == 0)
            bad = 1;
    }
    if (s.size() > 8192 || !s.valid())
        bad = 1;
    if (bad)
        g_failure = 1;
    s.drop();
}

/* ======================================================================== */
/* Family 4 -- pure memory queries                                           */
/* ======================================================================== */
static void fam_mem_query(int id) {
    size_t cap, off, n, delta, i;
    int rc, bad, op;
    unsigned char pat;
    unsigned long long tok;
    void *p;
    error e = error();
    g_api = "mem.query";
    bad = 0;
    cap = 1 + rbelow(rnext(), 2048);
    p = stdlib_alloc(cap);
    if (p == NULL) {
        g_failure = 1;
        return;
    }
    pat = (unsigned char)(id & 0xFF);
    for (i = 0; i < cap; i++)
        ((unsigned char *)p)[i] = pat;
    tok = stdlib_mem_token(p);
    op = (int)rbelow(rnext(), 6);

    if (op == 0) {
        g_case = "span_ok";
        off = pick_index(rnext(), cap);
        n = pick_n(rnext(), cap);
        rc = stdlib_mem_span_ok(p, cap, off, n);
        if (rc != span_exp(cap, off, n))
            bad = 1;
        if (rc == 0)
            g_refused = 1;
    } else if (op == 1) {
        g_case = "contains";
        delta = rbelow(rnext(), cap + 2);
        n = pick_n(rnext(), cap);
        {
            const char *ip = (const char *)p + delta;
            rc = stdlib_mem_span_contains(p, cap, ip, n);
        }
        {
            int expect = (delta <= cap && n <= cap - delta) ? 1 : 0;
            if (rc != expect)
                bad = 1;
            if (rc == 0)
                g_refused = 1;
        }
    } else if (op == 2) {
        g_case = "bounds";
        {
            const void *base = NULL;
            size_t sz = 0;
            rc = stdlib_mem_bounds(p, &base, &sz);
            if (rc != 0 || base != p || sz != cap)
                bad = 1;
            base = NULL;
            sz = 99;
            rc = stdlib_mem_bounds((const char *)p + 1, &base, &sz);
            g_refused = 1; /* interior pointer is not an exact tracked base */
            if (rc != -1 || base != NULL || sz != 0)
                bad = 1;
        }
    } else if (op == 3) {
        g_case = "key";
        if (stdlib_mem_key_current(p, tok) != 1)
            bad = 1;
        if (stdlib_mem_key_current(p, tok + 1) != 0)
            bad = 1;
        if (stdlib_mem_key_current(p, 0) != 1)
            bad = 1;
        g_refused = 1; /* the stale key case was refused */
    } else if (op == 4) {
        g_case = "query";
        off = pick_index(rnext(), cap);
        n = pick_n(rnext(), cap);
        {
            unsigned long long key = (rbelow(rnext(), 2) == 0) ? tok : tok + 1;
            int expect = (span_exp(cap, off, n) && stdlib_mem_key_current(p, key))
                             ? STDLIB_OK
                             : STDLIB_FAIL;
            e.clear();
            rc = stdlib_mem_query(p, off, n, key, &e);
            if (rc != expect || (rc == STDLIB_OK) != (e.ok() != 0))
                bad = 1;
            if (rc == STDLIB_FAIL)
                g_refused = 1;
        }
    } else {
        g_case = "stale_check";
        e.clear();
        if (stdlib_mem_check_token(p, tok, &e) != STDLIB_OK || !e.ok())
            bad = 1;
        e.clear();
        if (stdlib_mem_check_token(p, tok + 1, &e) != STDLIB_FAIL || e.ok())
            bad = 1;
        g_refused = 1;
    }
    for (i = 0; i < cap; i++)
        if (((unsigned char *)p)[i] != pat)
            bad = 1;
    if (bad)
        g_failure = 1;
    stdlib_release(p);
}

/* ======================================================================== */
/* Family 5 -- guarded writes with adjacent guard block and unchanged-bytes
 * canary around the intended write range.                                   */
/* ======================================================================== */
#define CAN 16
static unsigned char db_byte(int id, size_t i) {
    return (unsigned char)(((unsigned long long)id * 13ULL +
                            (unsigned long long)i * 7ULL + 0x11ULL) &
                           0xFFULL);
}
static void fam_mem_write(int id) {
    size_t cap, i, off, n, src_off, delta, start, end;
    unsigned char cb;
    int rc, bad, op, wrote;
    unsigned long long tok, stale;
    void *buf;
    void *src;
    void *guard;
    error e = error();
    g_api = "mem.write";
    bad = 0;
    wrote = 0;
    start = 0;
    end = 0;
    cap = 1 + rbelow(rnext(), 2048);
    buf = stdlib_alloc(cap);
    src = stdlib_alloc(cap);
    guard = stdlib_alloc(CAN);
    if (buf == NULL || src == NULL || guard == NULL) {
        g_failure = 1;
        if (buf != NULL)
            stdlib_release(buf);
        if (src != NULL)
            stdlib_release(src);
        if (guard != NULL)
            stdlib_release(guard);
        return;
    }
    cb = (unsigned char)(((unsigned long long)id * 29ULL + 0xA5ULL) & 0xFFULL);
    for (i = 0; i < cap; i++) {
        ((unsigned char *)buf)[i] = db_byte(id, i);
        ((unsigned char *)src)[i] = (unsigned char)(i & 0xFFu);
    }
    for (i = 0; i < CAN; i++)
        ((unsigned char *)guard)[i] = cb;
    tok = stdlib_mem_token(buf);
    stale = tok + 1;
    op = (int)rbelow(rnext(), 6);

    if (op == 0) {
        g_case = "set";
        off = pick_index(rnext(), cap);
        n = pick_n(rnext(), cap);
        e.clear();
        rc = stdlib_mem_set(buf, off, 0x5A, n, tok, &e);
        if (rc == STDLIB_FAIL) {
            g_refused = 1;
            if (e.ok())
                bad = 1;
        } else {
            wrote = 1;
            start = off;
            end = off + n;
            for (i = off; i < end; i++)
                if (((unsigned char *)buf)[i] != 0x5A)
                    bad = 1;
        }
    } else if (op == 1) {
        g_case = "copy";
        off = pick_index(rnext(), cap);
        src_off = pick_index(rnext(), cap);
        n = pick_n(rnext(), cap);
        e.clear();
        rc = stdlib_mem_copy(buf, off, src, src_off, n, tok, &e);
        if (rc == STDLIB_FAIL) {
            g_refused = 1;
            if (e.ok())
                bad = 1;
        } else {
            wrote = 1;
            start = off;
            end = off + n;
            for (i = 0; i < n; i++)
                if (((unsigned char *)buf)[off + i] !=
                    ((unsigned char *)src)[src_off + i])
                    bad = 1;
        }
    } else if (op == 2) {
        g_case = "move";
        off = rbelow(rnext(), cap + 1);
        src_off = rbelow(rnext(), cap + 1);
        n = rbelow(rnext(), cap + 1);
        e.clear();
        rc = stdlib_mem_move(buf, off, buf, src_off, n, tok, &e);
        if (rc == STDLIB_FAIL) {
            g_refused = 1;
            if (e.ok())
                bad = 1;
        } else {
            wrote = 1;
            start = off;
            end = off + n;
        }
    } else if (op == 3) {
        g_case = "borrow_interior";
        delta = rbelow(rnext(), cap + 2);
        n = pick_n(rnext(), cap);
        e.clear();
        rc = stdlib_mem_borrow((const char *)buf + delta, 0, n, 0, &e);
        g_refused = 1; /* interior pointer has no exact-base metadata */
        if (rc != STDLIB_FAIL || e.ok())
            bad = 1;
        e.clear();
        rc = stdlib_mem_borrow(buf, delta, n, tok, &e);
        if ((rc == STDLIB_OK) != (span_exp(cap, delta, n) != 0))
            bad = 1;
        if (rc == STDLIB_FAIL)
            g_refused = 1;
    } else if (op == 4) {
        g_case = "stale_token";
        e.clear();
        rc = stdlib_mem_borrow(buf, 0, 1, stale, &e);
        g_refused = 1;
        if (rc != STDLIB_FAIL || e.ok() ||
            e.category() != STDLIB_CATEGORY_LIFECYCLE)
            bad = 1;
        e.clear();
        rc = stdlib_mem_copy(buf, 0, src, 0, 1, stale, &e);
        g_refused = 1;
        if (rc != STDLIB_FAIL || e.ok())
            bad = 1;
    } else {
        g_case = "check_null";
        e.clear();
        rc = stdlib_mem_check(NULL, 0, &e);
        g_refused = 1;
        if (rc != STDLIB_FAIL || e.ok())
            bad = 1;
        e.clear();
        rc = stdlib_mem_check(buf, tok, &e);
        if (rc != STDLIB_OK || !e.ok())
            bad = 1;
    }

    for (i = 0; i < CAN; i++)
        if (((unsigned char *)guard)[i] != cb)
            bad = 1;
    for (i = 0; i < cap; i++) {
        if (wrote && i >= start && i < end)
            continue;
        if (((unsigned char *)buf)[i] != db_byte(id, i))
            bad = 1;
    }
    if (bad)
        g_failure = 1;
    stdlib_release(buf);
    stdlib_release(src);
    stdlib_release(guard);
}

/* ======================================================================== */
/* Family 6 -- collections checked ops and growth refusal                    */
/* ======================================================================== */
static void fam_collections(int id) {
    size_t size, i, idx;
    int sub, rc, bad, o;
    (void)id;
    g_api = "coll";
    bad = 0;
    size = rbelow(rnext(), 2049);
    sub = (int)rbelow(rnext(), 7);

    if (sub == 0) {
        deque d = deque();
        g_api = "deque";
        for (i = 0; i < size; i++)
            d.push_back(vec_val(i));
        if (rbelow(rnext(), 2) == 0) {
            idx = pick_index(rnext(), size);
            o = -1;
            rc = d.at_checked(idx, &o);
            if (idx >= size) {
                g_refused = 1;
                if (rc != -1 || o != -1)
                    bad = 1;
            } else if (rc != 0 || o != vec_val(idx)) {
                bad = 1;
            }
        } else {
            g_case = "reserve_max";
            g_refused = 1;
            if (d.reserve(SIZE_MAX) != -1 || d.size() != size)
                bad = 1;
        }
        if (d.size() != size)
            bad = 1;
        d.drop();
    } else if (sub == 1) {
        list l = list();
        g_api = "list";
        for (i = 0; i < size; i++)
            l.push_back(vec_val(i));
        idx = pick_index(rnext(), size);
        o = -1;
        rc = l.at_checked(idx, &o);
        if (idx >= size) {
            g_refused = 1;
            if (rc != -1 || o != -1)
                bad = 1;
        } else if (rc != 0 || o != vec_val(idx)) {
            bad = 1;
        }
        if (l.size() != size)
            bad = 1;
        l.drop();
    } else if (sub == 2) {
        stack st = stack();
        g_api = "stack";
        for (i = 0; i < size; i++)
            st.push(vec_val(i));
        if (size == 0) {
            g_case = "top_empty";
            g_refused = 1;
            if (st.top_checked(&o) != -1)
                bad = 1;
        } else {
            g_case = "top";
            o = -1;
            if (st.top_checked(&o) != 0 || o != vec_val(size - 1))
                bad = 1;
        }
        if (st.size() != size)
            bad = 1;
        st.drop();
    } else if (sub == 3) {
        queue q = queue();
        g_api = "queue";
        for (i = 0; i < size; i++)
            q.push(vec_val(i));
        if (size == 0) {
            g_case = "front_empty";
            g_refused = 1;
            if (q.front_checked(&o) != -1)
                bad = 1;
        } else {
            g_case = "front";
            o = -1;
            if (q.front_checked(&o) != 0 || o != 1)
                bad = 1;
        }
        if (q.size() != size)
            bad = 1;
        q.drop();
    } else if (sub == 4) {
        intmap m = intmap();
        g_api = "intmap";
        for (i = 0; i < size; i++)
            m.put((int)i, vec_val(i));
        if (rbelow(rnext(), 2) == 0) {
            int key = (int)rbelow(rnext(), size + 2);
            o = -1;
            rc = m.get(key, &o);
            if ((size_t)key >= size) {
                g_refused = 1;
                if (rc != -1 || o != -1)
                    bad = 1;
            } else if (rc != 0 || o != vec_val((size_t)key)) {
                bad = 1;
            }
        } else {
            g_case = "grow_max";
            g_refused = 1;
            if (m.grow(SIZE_MAX) != -1 || m.size() != size)
                bad = 1;
            if (stdlib_map_next_pow2(SIZE_MAX) != 0)
                bad = 1;
        }
        if (m.size() != size)
            bad = 1;
        m.drop();
    } else if (sub == 5) {
        strmap sm = strmap();
        char key[24];
        g_api = "strmap";
        for (i = 0; i < size; i++) {
            snprintf(key, sizeof(key), "k%d", (int)i);
            sm.put(key, vec_val(i));
        }
        if (size > 0) {
            snprintf(key, sizeof(key), "k%d", (int)(size - 1));
            o = -1;
            if (sm.get(key, &o) != 0 || o != vec_val(size - 1))
                bad = 1;
        }
        g_case = "grow_max";
        g_refused = 1;
        if (sm.grow(SIZE_MAX) != -1 || sm.size() != size)
            bad = 1;
        if (sm.get(NULL, &o) != -1 || sm.contains(NULL) != 0)
            bad = 1;
        sm.drop();
    } else {
        intset is = intset();
        g_api = "intset";
        for (i = 0; i < size; i++)
            is.insert((int)i);
        {
            int key = (int)rbelow(rnext(), size + 2);
            if ((size_t)key >= size) {
                g_case = "absent";
                g_refused = 1;
                if (is.contains(key) != 0 || is.remove(key) != -1)
                    bad = 1;
            } else {
                g_case = "present";
                if (is.contains(key) == 0)
                    bad = 1;
                if (is.remove(key) != 0 || is.contains(key) != 0)
                    bad = 1;
            }
        }
        g_case = "grow_max";
        g_refused = 1;
        if (is.grow(SIZE_MAX) != -1)
            bad = 1;
        is.drop();
    }
    if (bad)
        g_failure = 1;
}

/* ======================================================================== */
/* Family 7 -- BREAKING: deliberate out-of-range / limit / stale / overflow   */
/* ======================================================================== */
static void fam_breaking(int id) {
    size_t i, max_count;
    int sub, out, bad;
    vector v = vector();
    string s = string("abc");
    (void)id;
    g_api = "break";
    bad = 0;
    sub = (int)rbelow(rnext(), 4);
    g_refused = 1; /* every breaking probe must refuse */

    if (sub == 0) {
        g_case = "oob_at";
        for (i = 0; i < 8; i++)
            v.push_back(vec_val(i));
        out = 0;
        if (v.at_checked(SIZE_MAX, &out) != -1 || out != 0)
            bad = 1;
        out = 0;
        if (v.at_checked(9, &out) != -1 || out != 0)
            bad = 1;
        if (v.at(SIZE_MAX) != 0 || v.at(9) != 0)
            bad = 1;
        for (i = 0; i < 8; i++) {
            int t = 0;
            if (v.at_checked(i, &t) != 0 || t != vec_val(i))
                bad = 1;
        }
    } else if (sub == 1) {
        g_case = "one_over_limit";
        max_count = STDLIB_ALLOC_LIMIT / sizeof(int);
        {
            size_t bytes = 12345;
            if (vector_allocation_bytes(max_count + 1, &bytes) != -1 ||
                bytes != 12345)
                bad = 1;
        }
        if (vector_calculate_growth(max_count, max_count + 1) != max_count)
            bad = 1;
        if (v.reserve(max_count + 1) != -1 || v.size() != 0)
            bad = 1;
        if (s.reserve(STDLIB_ALLOC_LIMIT + 1) != -1 || s.size() != 3)
            bad = 1;
        if (stdlib_string_calculate_growth(STDLIB_ALLOC_LIMIT + 1, 4) != 0)
            bad = 1;
    } else if (sub == 2) {
        g_case = "stale_borrow";
        {
            void *p = stdlib_alloc(64);
            void *p2;
            unsigned long long tok;
            error e = error();
            if (p == NULL) {
                g_failure = 1;
            } else {
                tok = stdlib_mem_token(p);
                p2 = stdlib_realloc(p, 128);
                if (p2 != NULL) {
                    e.clear();
                    if (stdlib_mem_borrow(p2, 0, 8, tok, &e) != STDLIB_FAIL ||
                        e.ok())
                        bad = 1;
                    stdlib_release(p2);
                }
                e.clear();
                if (stdlib_mem_borrow(p, 0, 8, 0, &e) != STDLIB_FAIL || e.ok())
                    bad = 1;
            }
        }
    } else {
        g_case = "overflow_growth";
        if (v.reserve(SIZE_MAX) != -1 || v.size() != 0)
            bad = 1;
        {
            deque d = deque();
            if (d.reserve(SIZE_MAX) != -1 || d.size() != 0)
                bad = 1;
            if (stdlib_ring_copy(NULL, 0, 0, 0, SIZE_MAX) != NULL)
                bad = 1;
            d.drop();
        }
        if (s.reserve(SIZE_MAX) != -1 || s.size() != 3)
            bad = 1;
        {
            intmap m = intmap();
            if (m.grow(SIZE_MAX) != -1 || m.size() != 0)
                bad = 1;
            m.drop();
        }
        {
            size_t bytes = 999;
            if (vector_allocation_bytes(SIZE_MAX, &bytes) != -1 || bytes != 999)
                bad = 1;
        }
    }
    if (bad)
        g_failure = 1;
    v.drop();
    s.drop();
}

/* ======================================================================== */
/* Child: run a contiguous batch of scenario ids into a per-batch log.       */
/* ======================================================================== */
static void run_child(int b, int start, int end) {
    char path[256];
    int id, fam;
    int count = 0, refused = 0, canary = 0, fail = 0;
    FILE *f;

    snprintf(path, sizeof(path), RESULTS_DIR "/batch_%d.log", b);
    f = freopen(path, "w", stdout);
    if (f == NULL) {
        fprintf(stderr, "freopen failed for batch %d\n", b);
        _exit(2);
    }

    for (id = start; id < end; id++) {
        if (g_fault_crash && id == 137)
            raise(SIGSEGV);
        if (g_fault_hang && id == 137) {
            volatile unsigned long long spin = 0;
            for (;;)
                spin++;
        }
        g_api = "none";
        g_case = "none";
        g_result = "pass";
        g_refused = 0;
        g_canary_ok = 1;
        g_failure = 0;
        sent_fill(id);
        rng_seed(id);
        fam = id % 8;

        if (fam == 0)
            fam_vector_at(id);
        else if (fam == 1)
            fam_vector_mut(id);
        else if (fam == 2)
            fam_vector_seq(id);
        else if (fam == 3)
            fam_string(id);
        else if (fam == 4)
            fam_mem_query(id);
        else if (fam == 5)
            fam_mem_write(id);
        else if (fam == 6)
            fam_collections(id);
        else
            fam_breaking(id);

        if (!sent_check(id)) {
            g_canary_ok = 0;
            g_failure = 1;
        }
        if (g_failure)
            g_result = "FAIL";
        else if (g_refused)
            g_result = "refuse";
        else
            g_result = "pass";

        print("S", id, g_api, g_case, g_result, g_canary_ok);
        fflush(stdout); /* flush each line so a crash leaves a precise trail */
        count++;
        refused += g_refused;
        canary += g_canary_ok;
        fail += g_failure;
    }

    print("BATCH", b, count, refused, canary, fail);
    fflush(stdout);
    fclose(stdout);
    _exit(fail ? 1 : 0);
}

/* ======================================================================== */
/* Parent: fork one child per batch, contain crashes/hangs, aggregate.       */
/* ======================================================================== */
static long long now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + (long long)ts.tv_nsec;
}

int main(void) {
    int b, start, end;
    int crashes = 0, hangs = 0, wait_errors = 0, spawn_errors = 0;
    int missing_batches = 0, child_fail_exits = 0;
    int s_lines = 0, corruptions = 0, refusals = 0, canary_total = 0;
    struct timespec nap;
    FILE *f;
    char path[256];
    char line[256];

    nap.tv_sec = 0;
    nap.tv_nsec = 2000000; /* 2 ms poll */
    {
        const char *fault = getenv("FUZZ10K_FAULT");
        if (fault != NULL && strcmp(fault, "crash") == 0)
            g_fault_crash = 1;
        else if (fault != NULL && strcmp(fault, "hang") == 0)
            g_fault_hang = 1;
    }

    for (b = 0; b < NBATCH; b++) {
        pid_t pid;
        start = b * BATCH;
        end = start + BATCH;
        if (end > TOTAL)
            end = TOTAL;

        pid = fork();
        if (pid == 0) {
            run_child(b, start, end);
            _exit(0);
        } else if (pid < 0) {
            spawn_errors++;
            continue;
        } else {
            long long deadline = now_ns() +
                                 (long long)BATCH_TIMEOUT_SEC * 1000000000LL;
            for (;;) {
                int status = 0;
                pid_t r = waitpid(pid, &status, WNOHANG);
                if (r == pid) {
                    if (WIFSIGNALED(status)) {
                        crashes++;
                        fprintf(stderr,
                                "CRASH batch=%d signal=%d\n", b,
                                WTERMSIG(status));
                    } else if (WIFEXITED(status)) {
                        if (WEXITSTATUS(status) != 0)
                            child_fail_exits++;
                    } else {
                        wait_errors++;
                    }
                    break;
                }
                if (r < 0) {
                    wait_errors++;
                    break;
                }
                if (now_ns() >= deadline) {
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    hangs++;
                    fprintf(stderr, "HANG batch=%d (killed)\n", b);
                    break;
                }
                nanosleep(&nap, NULL);
            }
        }
    }

    /* Aggregate every batch log. */
    for (b = 0; b < NBATCH; b++) {
        int bb = 0, cnt = 0, ref = 0, can = 0, fl = 0;
        snprintf(path, sizeof(path), RESULTS_DIR "/batch_%d.log", b);
        f = fopen(path, "r");
        if (f == NULL) {
            missing_batches++;
            continue;
        }
        while (fgets(line, (int)sizeof(line), f) != NULL) {
            if (line[0] == 'S' && line[1] == ' ') {
                s_lines++;
            } else if (line[0] == 'B' && line[1] == 'A') {
                if (sscanf(line, "BATCH %d %d %d %d %d", &bb, &cnt, &ref,
                           &can, &fl) == 5) {
                    refusals += ref;
                    canary_total += can;
                    corruptions += fl;
                }
            }
        }
        fclose(f);
    }

    fprintf(stderr,
            "DETAIL dispatched=%d s_lines=%d crashes=%d hangs=%d "
            "corruptions=%d refusals=%d canary_ok=%d missing=%d "
            "spawn_err=%d wait_err=%d child_fail_exit=%d\n",
            TOTAL, s_lines, crashes, hangs, corruptions, refusals,
            canary_total, missing_batches, spawn_errors, wait_errors,
            child_fail_exits);

    print("FUZZ10K", "containers", TOTAL, crashes, hangs, corruptions,
          refusals);
    fflush(stdout);

    return (crashes || hangs || corruptions || missing_batches ||
            spawn_errors || wait_errors)
               ? 1
               : 0;
}
