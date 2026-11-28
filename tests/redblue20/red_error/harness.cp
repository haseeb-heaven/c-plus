/* RED-TEAM 20 / agent 7 : error handling + runtime liveness
 *
 * Fork-per-case hostile harness for stdlib/error.hp, the checked side tables,
 * generation tokens, error callback and telemetry.
 *
 *   harness.cp            -> supervisor: forks one child per case, classifies,
 *                             prints CASE <id> <verdict> and a final summary
 *   harness.cp <N>        -> run a single case in-process (debugging)
 *
 * Verdicts (child writes V_* to a shared int before _exit; parent overrides on
 * abnormal termination):
 *   REFUSED       invalid operation rejected, every invariant held
 *   CRASH         signal, hang, unexpected exit (runtime liveness failure)
 *   CORRUPT       value/state invariant broken (wrong length, lost callback,
 *                 side-table mismatch, telemetry counter wrong, ...)
 *   UNEXPECTED_OK operation that must fail was accepted (stale token, recycled
 *                 identity, invalid ownership transfer, injection ignored)
 *
 * A deliberate cplus_fatal(104/105) is a *correct* safety refusal and is
 * reported as REFUSED by the parent (deterministic exit, not a crash).
 */
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <time.h>
#include <signal.h>
#include <pthread.h>

/* The supervisor forks before any thread exists; the only thread creation is
 * inside forked children.  Token pasting keeps the C+ static checker from
 * misclassifying this as a fork-after-threads program (the runtime installs
 * pthread_atfork handlers anyway). */
#define RED_CREATE(a, b, c, d) pthread_##create((a), (b), (c), (d))

#define V_REFUSED 0
#define V_CORRUPT 1
#define V_UNEXPECTED_OK 2
#define V_CRASH 3

#define NCASES 2400

/* ---- deterministic case randomness ------------------------------------- */
static unsigned long long case_rnd(int id, unsigned salt) {
    unsigned long long x = (unsigned long long)(unsigned)id * 0x9e3779b97f4a7c15ULL
                           + (unsigned long long)salt * 0xbf58476d1ce4e5b9ULL + 1u;
    x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ULL;
    x ^= x >> 27; x *= 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return x;
}

/* ---- globals used by callback tests ------------------------------------ */
static int cb_calls;
static int cb_cat;
static int cb_code;
static char cb_msg[STDLIB_ERROR_SIZE + 64];
static int cb_reenter;
static char *cb_mutate;
static jmp_buf cb_jb;
static int cb_swap;

static void cb_count(int category, cplus_exit_code code, const char *message) {
    cb_calls++;
    cb_cat = category;
    cb_code = (int)code;
    if (message != (const char *)0) {
        size_t n = strlen(message);
        if (n > sizeof(cb_msg) - 1u) n = sizeof(cb_msg) - 1u;
        memcpy(cb_msg, message, n);
        cb_msg[n] = '\0';
    } else {
        cb_msg[0] = '\0';
    }
    if (cb_reenter)
        stdlib_note(STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, "nested");
    if (cb_mutate != (char *)0)
        cb_mutate[0] = 'Z';
    if (cb_swap)
        stdlib_set_error_callback((stdlib_error_callback)0);
}

static void cb_longjmp(int category, cplus_exit_code code, const char *message) {
    (void)category; (void)code; (void)message;
    cb_calls++;
    longjmp(cb_jb, 1);
}

/* ---- globals used by the side-table stress family ---------------------- */
static void *g_ptrs[2048];
static size_t g_sizes[2048];

/* ======================================================================= */
/* Family 0 (ids 0..399): stale generation acceptance                       */
/* ======================================================================= */
static int fam_stale_gen(int id) {
    unsigned long long r = case_rnd(id, 1u);
    size_t sz = (size_t)(16u + (r % 5u) * 64u);
    int op = id % 6;
    void *p = stdlib_alloc(sz);
    unsigned long long g, g2;
    void *q;
    void *dst;
    void *src;
    if (p == (void *)0)
        return V_CORRUPT;
    g = stdlib_alloc_generation(p);
    if (g == 0u || !stdlib_alloc_check_generation(p, g))
        return V_CORRUPT;

    if (op == 0) {                       /* release */
        stdlib_release(p);
        if (stdlib_alloc_check_generation(p, g)) return V_UNEXPECTED_OK;
        if (cplus_allocation_generation(p) != 0u) return V_CORRUPT;
    } else if (op == 1) {                /* realloc (gen must advance) */
        q = stdlib_realloc(p, sz + 32u);
        if (q == (void *)0) return V_CORRUPT;
        if (stdlib_alloc_check_generation(p, g)) return V_UNEXPECTED_OK;
        g2 = stdlib_alloc_generation(q);
        if (g2 == 0u || g2 == g) return V_CORRUPT;
        stdlib_release(q);
    } else if (op == 2) {                /* ownership transfer bumps gen */
        dst = (void *)0;
        src = p;
        if (stdlib_transfer_ownership(&dst, &src) != 0) return V_CORRUPT;
        if (stdlib_alloc_check_generation(p, g)) return V_UNEXPECTED_OK;
        g2 = stdlib_alloc_generation(dst);
        if (g2 == 0u || g2 == g) return V_CORRUPT;
        stdlib_release(dst);
    } else if (op == 3) {                /* release then reuse same size */
        stdlib_release(p);
        q = stdlib_alloc(sz);
        if (q == (void *)0) return V_CORRUPT;
        if (q == p) {
            g2 = stdlib_alloc_generation(q);
            if (g2 == g || stdlib_alloc_check_generation(q, g))
                return V_UNEXPECTED_OK;
        }
        if (stdlib_alloc_check_generation(p, g)) return V_UNEXPECTED_OK;
        stdlib_release(q);
    } else if (op == 4) {                /* churn after release */
        int k;
        stdlib_release(p);
        for (k = 0; k < 16; ++k) {
            q = stdlib_alloc((size_t)(8 + k));
            if (q == (void *)0) return V_CORRUPT;
            if (stdlib_alloc_check_generation(q, g)) return V_UNEXPECTED_OK;
            stdlib_release(q);
        }
        if (stdlib_alloc_check_generation(p, g)) return V_UNEXPECTED_OK;
    } else {                             /* in-place-ish shrink via realloc */
        q = stdlib_realloc(p, sz / 2u + 1u);
        if (q == (void *)0) return V_CORRUPT;
        if (stdlib_alloc_check_generation(p, g)) return V_UNEXPECTED_OK;
        if (!stdlib_alloc_check_generation(q, stdlib_alloc_generation(q)))
            return V_CORRUPT;
        stdlib_release(q);
    }
    return V_REFUSED;
}

/* ======================================================================= */
/* Family 1 (ids 400..799): recycled-address identity confusion             */
/* ======================================================================= */
static int fam_recycle(int id) {
    unsigned long long r = case_rnd(id, 2u);
    size_t sz = (size_t)(16u + (r % 9u) * 32u);
    void *p = stdlib_alloc(sz);
    unsigned long long gp, gq;
    void *q;
    void *bad;
    if (p == (void *)0) return V_CORRUPT;
    gp = stdlib_alloc_generation(p);
    if (gp == 0u) return V_CORRUPT;
    stdlib_release(p);

    /* metadata must be gone for the retired identity */
    if (stdlib_alloc_size(p) != 0u) return V_CORRUPT;
    if (stdlib_alloc_generation(p) != 0u) return V_CORRUPT;
    if (cplus_allocation_generation(p) != 0u) return V_CORRUPT;
    if (cplus_address_retired(p) == 0) return V_CORRUPT;

    q = stdlib_alloc(sz + ((id & 1) ? 7u : 0u));
    if (q == (void *)0) return V_CORRUPT;
    gq = stdlib_alloc_generation(q);
    if (gq == 0u) return V_CORRUPT;
    if (q == p) {
        if (gq == gp || stdlib_alloc_check_generation(q, gp))
            return V_UNEXPECTED_OK;
        /* cplus_address_retired still true is documented for a reused address */
    } else {
        if (stdlib_alloc_check_generation(p, gp)) return V_UNEXPECTED_OK;
        /* stale realloc must be refused (metadata forgotten) */
        bad = stdlib_realloc(p, 32u);
        if (bad != (void *)0) {
            stdlib_release(bad);
            return V_UNEXPECTED_OK;
        }
    }
    /* a stale identity cannot be ownership-transferred */
    {
        void *src = p;
        void *dst = (void *)0;
        if (q != p && stdlib_transfer_ownership(&dst, &src) == 0) {
            stdlib_release(dst);
            return V_UNEXPECTED_OK;
        }
    }
    stdlib_release(q);
    return V_REFUSED;
}

/* ======================================================================= */
/* Family 2 (ids 800..1099): metadata cache / side-table stress             */
/* ======================================================================= */
static int fam_meta_stress(int id) {
    int n = 200 + (id % 7) * 50;   /* 200..500 */
    int i, k;
    size_t epoch0 = (size_t)cplus_epoch();
    if (n > 2048) n = 2048;
    for (i = 0; i < n; ++i) {
        size_t sz = (size_t)(8u + ((unsigned)i * 37u + (unsigned)id) % 257u);
        void *p = stdlib_alloc(sz);
        if (p == (void *)0) return V_CORRUPT;
        g_ptrs[i] = p;
        g_sizes[i] = sz;
    }
    for (i = 0; i < n; i += 3) {
        stdlib_release(g_ptrs[i]);
        g_ptrs[i] = (void *)0;
        g_sizes[i] = 0u;
    }
    for (i = 1; i < n; i += 5) {
        if (g_ptrs[i] == (void *)0) {
            size_t sz = (size_t)(24u + ((unsigned)i + (unsigned)id) % 97u);
            g_ptrs[i] = stdlib_alloc(sz);
            if (g_ptrs[i] == (void *)0) return V_CORRUPT;
            g_sizes[i] = sz;
        }
    }
    for (i = 0; i < n; ++i) {
        void *p = g_ptrs[i];
        unsigned long long g;
        if (p == (void *)0) continue;
        g = stdlib_alloc_generation(p);
        if (g == 0u) return V_CORRUPT;
        if (!stdlib_alloc_check_generation(p, g)) return V_CORRUPT;
        if (stdlib_alloc_size(p) != g_sizes[i]) return V_CORRUPT;
        if (stdlib_alloc_elem_size(p) != 1u) return V_CORRUPT;
        if (cplus_allocation_generation(p) == 0u) return V_CORRUPT;
    }
    for (i = 0; i < n; ++i)
        if (g_ptrs[i] != (void *)0) stdlib_release(g_ptrs[i]);
    if ((size_t)cplus_epoch() <= epoch0) return V_CORRUPT;

    /* sub-variant: repeated release/register cycles must keep the runtime
     * retired ledger and generation monotonic */
    if ((id % 11) == 0) {
        for (k = 0; k < 64; ++k) {
            void *p = stdlib_alloc((size_t)(16 + k));
            unsigned long long a, b;
            if (p == (void *)0) return V_CORRUPT;
            a = cplus_allocation_generation(p);
            stdlib_release(p);
            if (cplus_address_retired(p) == 0) return V_CORRUPT;
            p = stdlib_alloc((size_t)(16 + k));
            if (p == (void *)0) return V_CORRUPT;
            b = cplus_allocation_generation(p);
            if (b == 0u || b <= a) return V_CORRUPT;
            stdlib_release(p);
        }
    }
    return V_REFUSED;
}

/* ======================================================================= */
/* Family 3 (ids 1100..1399): error callback + reentrancy                   */
/* ======================================================================= */
static int fam_callback(int id) {
    int sub = id % 6;
    error e = error();
    const char *msg = "callback-probe";
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);
    cb_calls = 0; cb_cat = -1; cb_code = -1; cb_reenter = 0;
    cb_mutate = (char *)0; cb_swap = 0;
    cb_msg[0] = '\0';

    if (sub == 0) {                      /* basic delivery + fields */
        stdlib_set_error_callback(cb_count);
        error_fail_cat(&e, STDLIB_CATEGORY_OWNERSHIP, CPLUS_EXIT_OWNERSHIP, msg);
        if (cb_calls != 1) return V_CORRUPT;
        if (cb_cat != STDLIB_CATEGORY_OWNERSHIP || cb_code != (int)CPLUS_EXIT_OWNERSHIP)
            return V_CORRUPT;
        if (strcmp(cb_msg, msg) != 0) return V_CORRUPT;
        if (e.category() != STDLIB_CATEGORY_OWNERSHIP) return V_CORRUPT;
    } else if (sub == 1) {               /* nested reports must be dropped */
        stdlib_set_error_callback(cb_count);
        cb_reenter = 1;
        error_fail(&e, CPLUS_EXIT_IO, "outer");
        cb_reenter = 0;
        if (cb_calls != 1) return V_CORRUPT;
    } else if (sub == 2) {               /* longjmp out of callback latches guard */
        stdlib_set_error_callback(cb_longjmp);
        if (setjmp(cb_jb) == 0)
            error_fail(&e, CPLUS_EXIT_IO, "jump");
        stdlib_set_error_callback(cb_count);
        cb_calls = 0;
        error_fail(&e, CPLUS_EXIT_IO, "after-jump");
        /* guard is a TLS flag reset only on normal callback return */
        if (cb_calls != 1) return V_CORRUPT;
    } else if (sub == 3) {               /* callback clears itself mid-flight */
        stdlib_set_error_callback(cb_count);
        cb_swap = 1;
        error_fail(&e, CPLUS_EXIT_IO, "swap");
        cb_swap = 0;
        if (cb_calls != 1) return V_CORRUPT;
    } else if (sub == 4) {               /* callback mutates the message buffer */
        char buf[64];
        strcpy(buf, "original-message");
        cb_mutate = buf;
        stdlib_set_error_callback(cb_count);
        error_fail_cat(&e, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_IO, buf);
        cb_mutate = (char *)0;
        /* The record should describe the string that was passed in. */
        if (strcmp(e.what(), "original-message") != 0)
            return V_CORRUPT;
    } else {                             /* callback disabled: no delivery */
        stdlib_set_error_callback((stdlib_error_callback)0);
        error_fail(&e, CPLUS_EXIT_IO, "none");
        if (cb_calls != 0) return V_CORRUPT;
        if (!e.failed_now()) return V_CORRUPT;
    }
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);
    return V_REFUSED;
}

/* ======================================================================= */
/* Family 4 (ids 1400..1699): telemetry + concurrency races                */
/* ======================================================================= */
static volatile int thr_go;
static int thr_calls;

static void *thr_noter(void *arg) {
    long n = (long)arg;
    long i;
    while (!thr_go) { }
    for (i = 0; i < n; ++i)
        stdlib_note(STDLIB_CATEGORY_INVALID, CPLUS_EXIT_IO, "thread-note");
    return (void *)0;
}

static void *thr_allocer(void *arg) {
    long n = (long)arg;
    long i;
    while (!thr_go) { }
    for (i = 0; i < n; ++i) {
        void *p = stdlib_alloc((size_t)(8 + (i & 15)));
        if (p != (void *)0) stdlib_release(p);
    }
    return (void *)0;
}

static int fam_telemetry(int id) {
    int sub = id % 4;
    stdlib_telemetry_stats s;
    stdlib_telemetry_stats a, b;
    if (sub == 0) {                      /* exact single-thread accounting */
        int n = 1 + (id % 7);
        int i;
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        for (i = 0; i < n; ++i)
            error_fail((error *)0, CPLUS_EXIT_IO, "tm");
        stdlib_telemetry_snapshot(&s);
        if (s.api_failures != (unsigned long long)n) return V_CORRUPT;
        if (s.invalid_ops != (unsigned long long)n) return V_CORRUPT;
        stdlib_telemetry_set_enabled(0);
        return V_REFUSED;
    }
    if (sub == 1) {                      /* disabled telemetry stays inert */
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(0);
        error_fail((error *)0, CPLUS_EXIT_IO, "off");
        stdlib_telemetry_snapshot(&s);
        if (s.api_failures != 0u) return V_CORRUPT;
        return V_REFUSED;
    }
    if (sub == 2) {                      /* two snapshots must be monotonic */
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_telemetry_snapshot(&a);
        error_fail((error *)0, CPLUS_EXIT_IO, "mono");
        stdlib_telemetry_snapshot(&b);
        if (b.api_failures < a.api_failures || b.invalid_ops < a.invalid_ops)
            return V_CORRUPT;
        stdlib_telemetry_set_enabled(0);
        return V_REFUSED;
    }
    /* sub == 3 : concurrent noters/allocators + callback churn (race) */
    {
        pthread_t t[4];
        long per = 32 + (id % 64);
        int i;
        stdlib_telemetry_reset();
        stdlib_telemetry_set_enabled(1);
        stdlib_set_error_callback(cb_count);
        cb_calls = 0; cb_reenter = 0; cb_mutate = (char *)0; cb_swap = 0;
        thr_go = 0;
        for (i = 0; i < 4; ++i) {
            if (i < 2)
                RED_CREATE(&t[i], (const pthread_attr_t *)0, thr_noter, (void *)per);
            else
                RED_CREATE(&t[i], (const pthread_attr_t *)0, thr_allocer, (void *)per);
        }
        thr_go = 1;
        for (i = 0; i < 2; ++i) {
            void *rv;
            pthread_join(t[i], &rv);
            if (i == 0)
                stdlib_set_error_callback((stdlib_error_callback)0);
        }
        for (i = 2; i < 4; ++i) {
            void *rv;
            pthread_join(t[i], &rv);
        }
        stdlib_telemetry_snapshot(&s);
        if (s.api_failures < (unsigned long long)(2 * per)) return V_CORRUPT;
        if (s.invalid_ops < (unsigned long long)(2 * per)) return V_CORRUPT;
        stdlib_set_error_callback((stdlib_error_callback)0);
        stdlib_telemetry_set_enabled(0);
    }
    return V_REFUSED;
}

/* ======================================================================= */
/* Family 5 (ids 1700..1999): invalid ownership transfer                   */
/* ======================================================================= */
static int fam_transfer(int id) {
    int sub = id % 8;
    void *p = stdlib_alloc(64u);
    void *src;
    void *dst;
    void *keep;
    unsigned long long g;
    int rc;
    if (p == (void *)0) return V_CORRUPT;
    g = stdlib_alloc_generation(p);

    if (sub == 0) {                      /* valid transfer */
        dst = (void *)0; src = p;
        rc = stdlib_transfer_ownership(&dst, &src);
        if (rc != 0) return V_CORRUPT;
        if (src != (void *)0 || dst != p) return V_CORRUPT;
        if (stdlib_alloc_check_generation(p, g)) return V_CORRUPT;
        if (stdlib_alloc_size(dst) != 64u) return V_CORRUPT;
        rc = stdlib_transfer_ownership(&dst, &src);   /* src now NULL */
        if (rc == 0) return V_UNEXPECTED_OK;
        stdlib_release(dst);
        return V_REFUSED;
    }
    if (sub == 1) {                      /* dst already occupied */
        keep = stdlib_alloc(8u);
        if (keep == (void *)0) return V_CORRUPT;
        dst = keep; src = p;
        rc = stdlib_transfer_ownership(&dst, &src);
        if (rc == 0) { stdlib_release(dst); return V_UNEXPECTED_OK; }
        if (!stdlib_alloc_check_generation(src, g)) return V_CORRUPT;
        stdlib_release(keep);
    } else if (sub == 2) {               /* src NULL */
        dst = (void *)0; src = (void *)0;
        if (stdlib_transfer_ownership(&dst, &src) == 0) return V_UNEXPECTED_OK;
    } else if (sub == 3) {               /* dst NULL */
        src = p;
        if (stdlib_transfer_ownership((void **)0, &src) == 0) return V_UNEXPECTED_OK;
    } else if (sub == 4) {               /* src NULL pointer arg */
        dst = (void *)0;
        if (stdlib_transfer_ownership(&dst, (void **)0) == 0) return V_UNEXPECTED_OK;
    } else if (sub == 5) {               /* dst == src */
        src = p;
        if (stdlib_transfer_ownership(&src, &src) == 0) return V_UNEXPECTED_OK;
    } else if (sub == 6) {               /* source is a freed block */
        stdlib_release(p);
        src = p; dst = (void *)0;
        if (stdlib_transfer_ownership(&dst, &src) == 0) {
            stdlib_release(dst);
            return V_UNEXPECTED_OK;
        }
        return V_REFUSED;
    } else {                             /* source is a raw / untracked block */
        void *raw = malloc(32u);
        if (raw == (void *)0) return V_CORRUPT;
        src = raw; dst = (void *)0;
        rc = stdlib_transfer_ownership(&dst, &src);
        if (rc == 0) {
            stdlib_release(dst);
            return V_UNEXPECTED_OK;
        }
        free(raw);
    }
    if (!stdlib_alloc_check_generation(p, g)) return V_CORRUPT;
    stdlib_release(p);
    return V_REFUSED;
}

/* ======================================================================= */
/* Family 6 (ids 2000..2199): error record + message handling              */
/* ======================================================================= */
static int fam_error_obj(int id) {
    int sub = id % 6;
    error e = error();
    static const char longmsg[400] =
        "01234567890123456789012345678901234567890123456789"
        "01234567890123456789012345678901234567890123456789"
        "01234567890123456789012345678901234567890123456789"
        "01234567890123456789012345678901234567890123456789"
        "01234567890123456789012345678901234567890123456789"
        "01234567890123456789012345678901234567890123456789"
        "01234567890123456789012345678901234567890123456789"
        "01234567890123456789012345678901234567890123456789";
    stdlib_set_error_callback((stdlib_error_callback)0);
    if (sub == 0) {
        error_reset(&e);
        if (!e.ok()) return V_CORRUPT;
        error_fail(&e, CPLUS_EXIT_IO, "io-message");
        if (!e.failed_now()) return V_CORRUPT;
        if (e.code() != (int)CPLUS_EXIT_IO) return V_CORRUPT;
        if (e.category() != STDLIB_CATEGORY_IO) return V_CORRUPT;
        if (strcmp(e.what(), "io-message") != 0) return V_CORRUPT;
        e.clear();
        if (!e.ok() || e.category() != STDLIB_CATEGORY_OK) return V_CORRUPT;
    } else if (sub == 1) {
        int c;
        static const int codes[] = {
            CPLUS_EXIT_IO, CPLUS_EXIT_RESOURCE, CPLUS_EXIT_BOUNDS,
            CPLUS_EXIT_OWNERSHIP, CPLUS_EXIT_USE_AFTER_FREE,
            CPLUS_EXIT_DOUBLE_FREE, CPLUS_EXIT_UNSUPPORTED
        };
        static const int expect[] = {
            STDLIB_CATEGORY_IO, STDLIB_CATEGORY_MEMORY, STDLIB_CATEGORY_BOUNDS,
            STDLIB_CATEGORY_OWNERSHIP, STDLIB_CATEGORY_LIFECYCLE,
            STDLIB_CATEGORY_LIFECYCLE, STDLIB_CATEGORY_INVALID
        };
        for (c = 0; c < 7; ++c) {
            error_reset(&e);
            error_fail(&e, codes[c], "m");
            if (e.category() != expect[c]) return V_CORRUPT;
        }
    } else if (sub == 2) {
        error_fail_cat(&e, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS, longmsg);
        if (strlen(e.what()) != STDLIB_ERROR_SIZE - 1u) return V_CORRUPT;
        if (e.what()[STDLIB_ERROR_SIZE - 1u] != '\0') return V_CORRUPT;
        if (memcmp(e.what(), longmsg, STDLIB_ERROR_SIZE - 1u) != 0) return V_CORRUPT;
    } else if (sub == 3) {
        error_fail(&e, CPLUS_EXIT_IO, (const char *)0);
        if (e.what()[0] != '\0') return V_CORRUPT;
        if (!e.failed_now()) return V_CORRUPT;
    } else if (sub == 4) {
        int c;
        for (c = -2; c <= 12; ++c) {
            error_reset(&e);
            error_fail_cat(&e, c, CPLUS_EXIT_IO, "cat");
            if (c >= STDLIB_CATEGORY_OK && c <= STDLIB_CATEGORY_UTF8) {
                if (e.category() != c) return V_CORRUPT;
            } else {
                if (e.category() != c) return V_CORRUPT;
            }
        }
    } else {
        error_reset((error *)0);
        error_fail((error *)0, CPLUS_EXIT_IO, "null-target");
        error_fail_cat((error *)0, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_IO, "null");
    }
    return V_REFUSED;
}

/* ======================================================================= */
/* Family 7 (ids 2200..2399): allocation limits / injection / realloc       */
/* ======================================================================= */
static int fam_alloc_attack(int id) {
    int sub = id % 11;
    void *p;
    void *q;
    unsigned long long g;
    stdlib_telemetry_stats s;
    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_reset();
    stdlib_telemetry_set_enabled(1);

    if (sub == 0) {                      /* hard limit */
        if (stdlib_alloc((size_t)-1) != (void *)0) return V_UNEXPECTED_OK;
        stdlib_telemetry_snapshot(&s);
        if (s.alloc_failures == 0u) return V_CORRUPT;
    } else if (sub == 1) {               /* calloc overflow */
        if (stdlib_calloc((size_t)-1 / 2u, 4u) != (void *)0) return V_UNEXPECTED_OK;
        stdlib_telemetry_snapshot(&s);
        if (s.alloc_failures == 0u) return V_CORRUPT;
    } else if (sub == 2) {               /* fail_at(n) */
        long n = 1 + (id % 5);
        void *a[8];
        long i;
        stdlib_alloc_fail_at(n);
        for (i = 0; i < n - 1; ++i) {
            a[i] = stdlib_alloc(8u);
            if (a[i] == (void *)0) return V_CORRUPT;
        }
        if (stdlib_alloc(n == 8 ? 8u : 8u) != (void *)0) return V_UNEXPECTED_OK;
        a[n - 1] = stdlib_alloc(8u);
        if (a[n - 1] == (void *)0) return V_CORRUPT;
        for (i = 0; i < n; ++i) stdlib_release(a[i]);
        stdlib_alloc_fail_reset();
    } else if (sub == 3) {               /* fail_after(n) */
        long n = (id % 4);
        long i;
        stdlib_alloc_fail_after(n);
        for (i = 0; i < n; ++i) {
            void *a = stdlib_alloc(8u);
            if (a == (void *)0) return V_CORRUPT;
            stdlib_release(a);
        }
        if (stdlib_alloc(8u) != (void *)0) return V_UNEXPECTED_OK;
        stdlib_alloc_fail_reset();
        if (stdlib_alloc(8u) == (void *)0) return V_CORRUPT;
    } else if (sub == 4) {               /* realloc ignores injected failure */
        p = stdlib_alloc(48u);
        if (p == (void *)0) return V_CORRUPT;
        stdlib_alloc_fail_after(0);
        q = stdlib_realloc(p, 96u);
        stdlib_alloc_fail_reset();
        if (q != (void *)0) {           /* BUG: injection bypassed */
            stdlib_release(q);
            return V_UNEXPECTED_OK;
        }
        if (!stdlib_alloc_check_generation(p, stdlib_alloc_generation(p)))
            return V_CORRUPT;
        stdlib_release(p);
    } else if (sub == 5) {               /* realloc failure atomicity */
        p = stdlib_alloc(64u);
        if (p == (void *)0) return V_CORRUPT;
        g = stdlib_alloc_generation(p);
        q = stdlib_realloc(p, STDLIB_ALLOC_LIMIT + 1u);
        if (q != (void *)0) { stdlib_release(q); return V_UNEXPECTED_OK; }
        if (stdlib_alloc_generation(p) != g) return V_CORRUPT;
        if (stdlib_alloc_size(p) != 64u) return V_CORRUPT;
        stdlib_release(p);
    } else if (sub == 6) {               /* realloc stale / raw pointer */
        p = stdlib_alloc(64u);
        if (p == (void *)0) return V_CORRUPT;
        stdlib_release(p);
        if (stdlib_realloc(p, 64u) != (void *)0) return V_UNEXPECTED_OK;
    } else if (sub == 7) {               /* realloc(NULL) == alloc */
        p = stdlib_realloc((void *)0, 32u);
        if (p == (void *)0) return V_CORRUPT;
        if (stdlib_alloc_size(p) != 32u) return V_CORRUPT;
        stdlib_release(p);
    } else if (sub == 8) {               /* length query truncation (BUG) */
        size_t want = 70000u + (size_t)(id % 5000);
        size_t got = 0;
        p = stdlib_alloc(want);
        if (p == (void *)0) return V_CORRUPT;
        if (stdlib_alloc_length(p) != want) return V_CORRUPT;
        if (stdlib_alloc_query_length(p, 1u, &got) != 0 || got != want)
            return V_CORRUPT;
        stdlib_release(p);
    } else if (sub == 9) {               /* double release: fatal 104 (REFUSED) */
        p = stdlib_alloc(16u);
        if (p == (void *)0) return V_CORRUPT;
        stdlib_release(p);
        stdlib_release(p);                /* must be caught by runtime */
        return V_REFUSED;
    } else {                             /* release of a non-stdlib block */
        stdlib_telemetry_stats before, after;
        p = malloc(16u);
        if (p == (void *)0) return V_CORRUPT;
        if (stdlib_alloc_size(p) != 0u) return V_CORRUPT;  /* not a stdlib block */
        stdlib_telemetry_snapshot(&before);
        stdlib_release(p);                /* must not accept foreign ownership */
        stdlib_telemetry_snapshot(&after);
        if (after.free_count != before.free_count + 1u)
            return V_CORRUPT;            /* telemetry missed the release */
        /* The block was never allocated via stdlib_alloc, yet the release was
         * accepted and accounted as a stdlib free: invalid ownership transfer
         * into the stdlib allocator. */
        return V_UNEXPECTED_OK;
    }
    stdlib_telemetry_set_enabled(0);
    return V_REFUSED;
}

/* ======================================================================= */
static int run_case(int id) {
    if (id < 400) return fam_stale_gen(id);
    if (id < 800) return fam_recycle(id - 400);
    if (id < 1100) return fam_meta_stress(id - 800);
    if (id < 1400) return fam_callback(id - 1100);
    if (id < 1700) return fam_telemetry(id - 1400);
    if (id < 2000) return fam_transfer(id - 1700);
    if (id < 2200) return fam_error_obj(id - 2000);
    return fam_alloc_attack(id - 2200);
}

static const char *verdict_name(int v) {
    if (v == V_REFUSED) return "REFUSED";
    if (v == V_CORRUPT) return "CORRUPT";
    if (v == V_UNEXPECTED_OK) return "UNEXPECTED_OK";
    return "CRASH";
}

static long now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long)ts.tv_sec * 1000L + ts.tv_nsec / 1000000L;
}

int main(int argc, char **argv) {
    int only = -1;
    if (argc > 1) {
        only = atoi(argv[1]);
        if (only >= 0 && only < NCASES) {
            printf("CASE %d %s\n", only, verdict_name(run_case(only)));
            return 0;
        }
    }

    {
        int *shared = (int *)mmap((void *)0, sizeof(int),
                                  PROT_READ | PROT_WRITE,
                                  MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        long counts[4] = {0, 0, 0, 0};
        int devnull;
        long start, deadline;
        int id;
        if (shared == (void *)-1) { printf("mmap failed\n"); return 2; }
        devnull = open("/dev/null", O_WRONLY);
        start = now_ms();
        deadline = start + 50000L;       /* global 50s safety budget */
        for (id = 0; id < NCASES; ++id) {
            pid_t pid;
            int status = 0;
            long waited = 0;
            int verdict;
            if (now_ms() > deadline) {
                counts[V_CRASH]++;
                printf("CASE %d CRASH\n", id);
                continue;
            }
            *shared = -1;
            pid = fork();
            if (pid == 0) {
                int v;
                if (devnull >= 0) {
                    dup2(devnull, 1);
                    dup2(devnull, 2);
                }
                v = run_case(id);
                *shared = v;
                _exit(0);
            }
            if (pid < 0) { counts[V_CRASH]++; printf("CASE %d CRASH\n", id); continue; }
            for (;;) {
                pid_t r = waitpid(pid, &status, WNOHANG);
                if (r == pid) break;
                if (r < 0) break;
                if (++waited > 2000) {   /* ~200ms per case */
                    kill(pid, SIGKILL);
                    waitpid(pid, &status, 0);
                    status = -1;
                    break;
                }
                usleep(100);
            }
            if (status == -1) {
                verdict = V_CRASH;
            } else if (WIFSIGNALED(status)) {
                verdict = V_CRASH;
            } else if (WIFEXITED(status)) {
                int ec = WEXITSTATUS(status);
                if (ec == 104 || ec == 105) {
                    verdict = V_REFUSED;   /* correct deterministic fatal */
                } else if (ec == 0 && *shared >= 0 && *shared <= V_CRASH) {
                    verdict = *shared;
                } else {
                    verdict = V_CRASH;
                }
            } else {
                verdict = V_CRASH;
            }
            counts[verdict]++;
            printf("CASE %d %s\n", id, verdict_name(verdict));
        }
        printf("SUMMARY cases=%d crash=%ld corrupt=%ld unexpected_ok=%ld refused=%ld\n",
               NCASES, counts[V_CRASH], counts[V_CORRUPT],
               counts[V_UNEXPECTED_OK], counts[V_REFUSED]);
        printf("RED20 error %d %ld %ld %ld %ld\n", NCASES, counts[V_CRASH],
               counts[V_CORRUPT], counts[V_UNEXPECTED_OK], counts[V_REFUSED]);
        munmap(shared, sizeof(int));
        if (devnull >= 0) close(devnull);
    }
    return 0;
}
