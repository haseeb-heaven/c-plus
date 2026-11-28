/* Adversarial resource-lifecycle corpus for the C+ hardened libc.
 *
 * Standalone C99.  Links only build/libc/libcplus_hardened.a.  Every
 * interaction uses a defined public cplus_* entry point.  We never keep or
 * dereference a freed pointer and never free the same address twice.
 *
 * TABLE-DRIVEN: g_cases[] holds 700 deterministic (resource_kind,
 * operation_sequence) cases:
 *   300 x alloc/free cycles          (sizes 0..4096, live accounting baseline)
 *   200 x tmpfile write/rewind/read  (payloads 0..1024, round-trip + close)
 *   100 x realloc grow/shrink/grow   (content verified at every step)
 *   100 x fail-injection cycles      (fail_after n in 0..20, fail_reset)
 *
 * On POSIX the number of open file descriptors (via /dev/fd) is sampled
 * around every file case and around the whole file group.  A leaked fd, a
 * failed close, a stale fd, or any accounting imbalance prints "FINDING:".
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stddef.h>
#include <stdint.h>

#ifndef _WIN32
#include <unistd.h>
#include <dirent.h>
#include <fcntl.h>
#include <errno.h>
#endif

/* Local declarations: the verify command does not add install-include to
 * the include path (same convention as the other libc attack tests). */
extern void  *cplus_malloc(size_t);
extern void  *cplus_calloc(size_t, size_t);
extern void  *cplus_realloc(void *, size_t);
extern void   cplus_free(void *);
extern int    cplus_free_checked(void **);
extern size_t cplus_alloc_live_count(void);
extern size_t cplus_alloc_live_bytes(void);
extern void   cplus_alloc_fail_after(long);
extern void   cplus_alloc_fail_reset(void);
extern FILE  *cplus_tmpfile(void);
extern int    cplus_fclose(FILE *);
extern size_t cplus_fread(void *, size_t, size_t, FILE *);
extern size_t cplus_fwrite(const void *, size_t, size_t, FILE *);
extern void   cplus_rewind(FILE *);
extern int    cplus_fflush(FILE *);
extern int    cplus_fileno(FILE *);

/* ------------------------------------------------------------------------ */
/* Diagnostics                                                             */
/* ------------------------------------------------------------------------ */
static int g_findings;
static int g_cases_ok;

#define FINDING(msg) do {                                                     \
    ++g_findings;                                                             \
    fprintf(stderr, "FINDING: %s\n", (msg));                                  \
} while (0)

/* Non-fatal accounting / return-value check: records a finding and keeps
 * going so all 700 cases can be exercised. */
#define CHECK(cond, msg) do {                                                 \
    if (!(cond)) FINDING(msg);                                                \
} while (0)

/* Fatal check: a violated precondition would make subsequent work UB. */
#define REQUIRE(cond, msg) do {                                               \
    if (!(cond)) {                                                            \
        FINDING(msg);                                                         \
        fprintf(stderr, "corpus_resource: aborting (unsafe continuation)\n"); \
        abort();                                                              \
    }                                                                         \
} while (0)

/* ------------------------------------------------------------------------ */
/* Case table                                                              */
/* ------------------------------------------------------------------------ */
enum resource_kind {
    RK_ALLOC_FREE = 0,
    RK_TMPFILE = 1,
    RK_REALLOC_CHAIN = 2,
    RK_FAIL_INJECT = 3
};

typedef struct {
    int           kind;   /* resource_kind */
    unsigned int  seq;    /* operation_sequence variant */
    unsigned int  size;   /* primary size parameter */
    unsigned int  n;      /* secondary parameter (failure count, etc.) */
} resource_case;

#define N_ALLOC   300u
#define N_TMPFILE 200u
#define N_REALLOC 100u
#define N_FAIL    100u
#define N_CASES   (N_ALLOC + N_TMPFILE + N_REALLOC + N_FAIL) /* 700 */

static resource_case g_cases[N_CASES];

static void build_table(void) {
    unsigned int i;
    for (i = 0; i < N_ALLOC; ++i) {
        g_cases[i].kind = RK_ALLOC_FREE;
        g_cases[i].seq  = i;
        g_cases[i].size = (i * 13u + 7u) % 4097u;   /* 0..4096 */
        g_cases[i].n    = (i % 3u) + 1u;            /* blocks per cycle 1..3 */
    }
    for (i = 0; i < N_TMPFILE; ++i) {
        unsigned int k = N_ALLOC + i;
        g_cases[k].kind = RK_TMPFILE;
        g_cases[k].seq  = i;
        g_cases[k].size = (i * 17u + 3u) % 1025u;   /* 0..1024 */
        g_cases[k].n    = (i % 4u);                 /* stream op variant */
    }
    for (i = 0; i < N_REALLOC; ++i) {
        unsigned int k = N_ALLOC + N_TMPFILE + i;
        g_cases[k].kind = RK_REALLOC_CHAIN;
        g_cases[k].seq  = i;
        g_cases[k].size = (i * 29u + 11u) % 1024u + 1u; /* 1..1024 */
        g_cases[k].n    = (i % 5u);
    }
    for (i = 0; i < N_FAIL; ++i) {
        unsigned int k = N_ALLOC + N_TMPFILE + N_REALLOC + i;
        g_cases[k].kind = RK_FAIL_INJECT;
        g_cases[k].seq  = i;
        g_cases[k].size = (i * 7u + 1u) % 64u + 1u;
        g_cases[k].n    = i % 21u;                  /* 0..20 */
    }
}

static unsigned char pat(unsigned int seq, unsigned int j) {
    return (unsigned char)((seq * 131u + j * 37u + 0x5au) & 0xffu);
}

/* ------------------------------------------------------------------------ */
/* POSIX fd counting                                                       */
/* ------------------------------------------------------------------------ */
#ifndef _WIN32
static int count_open_fds(void) {
    int n = 0;
    DIR *d = opendir("/dev/fd");
    struct dirent *e;
    if (d == NULL) return -1;
    while ((e = readdir(d)) != NULL) {
        if (e->d_name[0] == '.') continue;
        ++n;
    }
    closedir(d);
    return n;
}
#endif

/* ------------------------------------------------------------------------ */
/* Case handlers                                                           */
/* ------------------------------------------------------------------------ */
static void run_alloc_free(const resource_case *c) {
    size_t base_live = cplus_alloc_live_count();
    size_t base_bytes = cplus_alloc_live_bytes();
    void *ps[3];
    size_t sz[3];
    unsigned int nb = c->n;            /* 1..3 */
    unsigned int j;

    REQUIRE(nb <= 3u, "alloc/free block count out of range");
    for (j = 0; j < nb; ++j) {
        unsigned char *p;
        unsigned int s = (c->size + j * 37u) % 4097u; /* 0..4096 */
        sz[j] = (size_t)s;
        ps[j] = cplus_malloc(sz[j]);
        REQUIRE(ps[j] != NULL, "cplus_malloc returned NULL unexpectedly");
        CHECK(cplus_alloc_live_count() == base_live + (size_t)j + 1u,
              "alloc/free live_count did not increase per allocation");
        p = (unsigned char *)ps[j];
        if (sz[j] != 0u) {
            memset(p, (int)pat(c->seq, j), sz[j]);
            CHECK(p[0] == pat(c->seq, j), "alloc/free pattern not retained");
            CHECK(p[sz[j] - 1u] == pat(c->seq, j),
                  "alloc/free pattern not retained at tail");
        }
    }
    for (j = 0; j < nb; ++j) cplus_free(ps[j]);

    CHECK(cplus_alloc_live_count() == base_live,
          "alloc/free live_count did not return to baseline");
    CHECK(cplus_alloc_live_bytes() == base_bytes,
          "alloc/free live_bytes did not return to baseline");
}

static void run_tmpfile(const resource_case *c) {
    size_t len = (size_t)c->size;      /* 0..1024 */
    unsigned char wbuf[1024];
    unsigned char rbuf[1024];
    unsigned char extra;
    size_t i;
    FILE *f;
    int fd = -1;
#ifndef _WIN32
    int fd_before = count_open_fds();
#endif

    for (i = 0; i < len; ++i) wbuf[i] = pat(c->seq, (unsigned int)i);
    memset(rbuf, 0, sizeof(rbuf));

    f = cplus_tmpfile();
    REQUIRE(f != NULL, "cplus_tmpfile returned NULL");

    if (len != 0u) {
        CHECK(cplus_fwrite(wbuf, 1u, len, f) == len,
              "tmpfile short write");
    }
    CHECK(cplus_fflush(f) == 0, "tmpfile fflush failed");

    /* Operation-sequence variants: use rewind (even seq) or explicit
     * fseek-to-zero style via rewind in all variants, plus extra flushes
     * and a second rewind for odd variants. */
    cplus_rewind(f);
    if ((c->n & 1u) != 0u) {
        CHECK(cplus_fflush(f) == 0, "tmpfile second fflush failed");
        cplus_rewind(f);
    }

    if (len != 0u) {
        CHECK(cplus_fread(rbuf, 1u, len, f) == len,
              "tmpfile short read");
        CHECK(memcmp(wbuf, rbuf, len) == 0,
              "tmpfile data did not round-trip");
    }
    extra = 0xa5u;
    CHECK(cplus_fread(&extra, 1u, 1u, f) == 0u,
          "tmpfile returned data past EOF");

    fd = cplus_fileno(f);
    CHECK(fd >= 0, "cplus_fileno returned a negative fd");

    CHECK(cplus_fclose(f) == 0, "tmpfile close failed");

#ifndef _WIN32
    /* tmpfile() creates an unnamed file that the platform unlinks; the
     * observable cleanup evidence is that its fd is gone.  Check both the
     * specific fd and the aggregate /dev/fd count. */
    if (fd >= 0) {
        errno = 0;
        CHECK(fcntl(fd, F_GETFD) == -1 && errno == EBADF,
              "tmpfile fd still open after close (file not cleaned up)");
    }
    {
        int fd_after = count_open_fds();
        CHECK(fd_after == fd_before,
              "tmpfile cycle leaked a file descriptor");
    }
#endif
}

static void run_realloc_chain(const resource_case *c) {
    size_t base_live = cplus_alloc_live_count();
    size_t base_bytes = cplus_alloc_live_bytes();
    size_t base = (size_t)c->size;     /* 1..1024 */
    size_t g1 = base * 2u + (c->n % 7u);
    size_t s1 = (base / 2u) + 1u;
    size_t g2 = base * 3u + 5u;
    unsigned char *p;
    size_t i;

    REQUIRE(s1 <= base, "invalid shrink size");

    p = (unsigned char *)cplus_malloc(base);
    REQUIRE(p != NULL, "realloc-chain initial malloc failed");
    for (i = 0; i < base; ++i) p[i] = pat(c->seq, (unsigned int)i);

    /* Grow: original bytes preserved, new bytes initialized. */
    p = (unsigned char *)cplus_realloc(p, g1);
    REQUIRE(p != NULL, "realloc-chain grow step failed");
    for (i = 0; i < base; ++i)
        CHECK(p[i] == pat(c->seq, (unsigned int)i),
              "realloc-chain lost data on grow");
    for (i = base; i < g1; ++i) p[i] = (unsigned char)(0x30u + (i & 0x0fu));
    for (i = base; i < g1; ++i)
        CHECK(p[i] == (unsigned char)(0x30u + (i & 0x0fu)),
              "realloc-chain new grow bytes corrupt");

    /* Shrink: bytes within the new size must survive. */
    p = (unsigned char *)cplus_realloc(p, s1);
    REQUIRE(p != NULL, "realloc-chain shrink step failed");
    for (i = 0; i < s1; ++i)
        CHECK(p[i] == pat(c->seq, (unsigned int)i),
              "realloc-chain lost data on shrink");

    /* Grow again: verify surviving prefix, initialize the rest. */
    p = (unsigned char *)cplus_realloc(p, g2);
    REQUIRE(p != NULL, "realloc-chain second grow failed");
    for (i = 0; i < s1; ++i)
        CHECK(p[i] == pat(c->seq, (unsigned int)i),
              "realloc-chain lost data on second grow");
    for (i = s1; i < g2; ++i) p[i] = (unsigned char)(0xc0u ^ (i & 0x1fu));
    for (i = s1; i < g2; ++i)
        CHECK(p[i] == (unsigned char)(0xc0u ^ (i & 0x1fu)),
              "realloc-chain second-grow bytes corrupt");

    cplus_free(p);
    CHECK(cplus_alloc_live_count() == base_live,
          "realloc-chain live_count did not return to baseline");
    CHECK(cplus_alloc_live_bytes() == base_bytes,
          "realloc-chain live_bytes did not return to baseline");
}

static void run_fail_inject(const resource_case *c) {
    long n = (long)c->n;                /* 0..20 */
    size_t base_live = cplus_alloc_live_count();
    size_t base_bytes = cplus_alloc_live_bytes();
    void *ps[20];
    long j;
    void *after_reset;

    cplus_alloc_fail_after(n);
    for (j = 0; j < n; ++j) {
        unsigned char *q;
        size_t s = (size_t)((c->size + (unsigned int)j * 13u) % 128u + 1u);
        ps[j] = cplus_malloc(s);
        REQUIRE(ps[j] != NULL, "fail-injection early allocation failed");
        CHECK(cplus_alloc_live_count() == base_live + (size_t)j + 1u,
              "fail-injection live_count inconsistent before failure");
        q = (unsigned char *)ps[j];
        memset(q, (int)pat(c->seq, (unsigned int)j), s);
        CHECK(q[0] == pat(c->seq, (unsigned int)j),
              "fail-injection payload corrupt");
    }
    /* The (n+1)-th allocation must be the injected failure. */
    CHECK(cplus_malloc(16u) == NULL,
          "fail_after(n) did not fail the (n+1)-th allocation");

    /* fail_reset must restore normal operation. */
    cplus_alloc_fail_reset();
    after_reset = cplus_malloc(c->size + 1u);
    REQUIRE(after_reset != NULL,
            "fail_reset did not restore allocation");
    cplus_free(after_reset);

    for (j = 0; j < n; ++j) cplus_free(ps[j]);

    CHECK(cplus_alloc_live_count() == base_live,
          "fail-injection live_count did not return to baseline");
    CHECK(cplus_alloc_live_bytes() == base_bytes,
          "fail-injection live_bytes did not return to baseline");
}

/* ------------------------------------------------------------------------ */
/* Driver                                                                  */
/* ------------------------------------------------------------------------ */
int main(void) {
    unsigned int i;
#ifndef _WIN32
    int fd_group_before;
    int fd_group_after;
#endif

    build_table();

    /* Start from a known-clean injection state. */
    cplus_alloc_fail_reset();
    {
        size_t l0 = cplus_alloc_live_count();
        size_t b0 = cplus_alloc_live_bytes();
        CHECK(l0 == 0u, "initial live_count is not zero");
        CHECK(b0 == 0u, "initial live_bytes is not zero");
    }

#ifndef _WIN32
    fd_group_before = count_open_fds();
#endif

    for (i = 0; i < (unsigned int)N_CASES; ++i) {
        const resource_case *c = &g_cases[i];
        switch (c->kind) {
            case RK_ALLOC_FREE:    run_alloc_free(c);    break;
            case RK_TMPFILE:       run_tmpfile(c);       break;
            case RK_REALLOC_CHAIN: run_realloc_chain(c); break;
            case RK_FAIL_INJECT:   run_fail_inject(c);   break;
            default:
                FINDING("unknown resource_kind in case table");
                break;
        }
        ++g_cases_ok;
    }

#ifndef _WIN32
    fd_group_after = count_open_fds();
    CHECK(fd_group_after == fd_group_before,
          "file-cycle group leaked file descriptors");
#endif

    CHECK(cplus_alloc_live_count() == 0u,
          "final live_count is not zero (allocator leak)");
    CHECK(cplus_alloc_live_bytes() == 0u,
          "final live_bytes is not zero (allocator leak)");

    if (g_findings != 0) {
        fprintf(stderr,
                "corpus_resource: %d findings across %d cases\n",
                g_findings, g_cases_ok);
        return 1;
    }

    printf("corpus_resource: %d cases ok\n", g_cases_ok);
    return 0;
}
