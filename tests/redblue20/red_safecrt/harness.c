/*
 * harness.c -- RED20 safecrt red-team harness.
 *
 * Attacks the checked SafeCRT layer (stdlib/safe) with deterministic, hostile
 * inputs: null args, one-over, SIZE_MAX, overlap, unterminated sources,
 * double-free / UAF, foreign pointers, and alignment lies.
 *
 * Every case runs in a FORKED CHILD with a 2-page mmap: a readable first page
 * whose last CANARY bytes past the declared capacity are filled with 0xC3, and
 * a PROT_NONE guard page immediately after it.  A small overflow is classified
 * CORRUPT (canary changed); a large overflow hits the guard page and is a
 * CRASH (SIGSEGV).  The child writes its observed rc/errno to a pipe and exits
 * with a classification byte; the parent tallies and logs every case.
 *
 * Outcome classes (mutually exclusive, precedence CRASH > CORRUPT >
 * UNEXPECTED_OK > REFUSED):
 *   REFUSED       - hostile case returned a documented non-zero code / safe
 *                   sentinel and no canary/guard violation was seen.
 *   CRASH         - child died on a signal (SIGSEGV / SIGBUS / SIGABRT / ...).
 *   CORRUPT       - child survived but bytes past the declared capacity changed.
 *   UNEXPECTED_OK - child survived, canary intact, but the API reported success
 *                   on a hostile case.  These are triaged by hand in FINDINGS.md.
 *
 * Prints exactly:
 *   RED20 safecrt <cases> <crash> <corrupt> <unexpected_ok> <refused>
 */
#define _DARWIN_C_SOURCE
#define _POSIX_C_SOURCE 200112L
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>

typedef int errno_t;
typedef size_t rsize_t;
#define CPLUS_SAFE_ERRNO_T_DEFINED 1
#define CPLUS_SAFE_RSIZE_T_DEFINED 1
#include "safe_alloc.h"

/* ---- SafeCRT memory (raw + auto) -------------------------------------- */
extern errno_t cplus_memcpy_s(void *, rsize_t, const void *, rsize_t);
extern errno_t cplus_memmove_s(void *, rsize_t, const void *, rsize_t);
extern errno_t cplus_memset_s(void *, rsize_t, int, rsize_t);
extern errno_t cplus_memcmp_s(const void *, rsize_t, const void *, rsize_t, int *);
extern errno_t cplus_memzero_s(void *, rsize_t);
extern void *cplus_memcpy_s_auto(void *, const void *, size_t, size_t);
extern void *cplus_memmove_s_auto(void *, const void *, size_t, size_t);
extern void *cplus_memset_s_auto(void *, int, size_t, size_t);
extern int cplus_memcmp_s_auto(const void *, const void *, size_t, size_t, size_t);

/* ---- SafeCRT string (raw + auto) -------------------------------------- */
extern errno_t cplus_strcpy_s(char *, rsize_t, const char *);
extern errno_t cplus_strncpy_s(char *, rsize_t, const char *, rsize_t);
extern errno_t cplus_strcat_s(char *, rsize_t, const char *);
extern errno_t cplus_strncat_s(char *, rsize_t, const char *, rsize_t);
extern errno_t cplus_strcmp_s(const char *, rsize_t, const char *, int *);
extern rsize_t cplus_strnlen_s(const char *, rsize_t);
extern char *cplus_strtok_s(char *, rsize_t *, const char *, char **);
extern errno_t cplus_strerror_s(char *, rsize_t, int);
extern char *cplus_strcpy_s_auto(char *, const char *, size_t);
extern char *cplus_strncpy_s_auto(char *, const char *, size_t, size_t);
extern char *cplus_strcat_s_auto(char *, const char *, size_t);
extern char *cplus_strncat_s_auto(char *, const char *, size_t, size_t);

/* ---- SafeCRT allocator / runtime ------------------------------------- */
extern void *malloc_s(size_t);
extern void *calloc_s(size_t, size_t);
extern void *realloc_s(void *, size_t);
extern void free_s(void *);
extern int free_checked_s(void *);
extern int free_checked_generation_s(void *, unsigned long long);
extern void *aligned_alloc_s(size_t, size_t);
extern size_t cplus_safe_ptr_size(const void *);
extern size_t cplus_safe_remaining_capacity(const void *);
extern void cplus_runtime_set_violation_trap(int);
extern void cplus_sc_set_mem_constraint_handler_s_wrapper(void (*)(const char *, void *, int));
extern void cplus_sc_set_str_constraint_handler_s_wrapper(void (*)(const char *, void *, int));

/* API indices */
enum {
    A_MEMCPY, A_MEMMOVE, A_MEMSET, A_MEMCMP, A_MEMZERO,
    A_MEMCPY_A, A_MEMMOVE_A, A_MEMSET_A, A_MEMCMP_A,
    A_STRCPY, A_STRNCPY, A_STRCAT, A_STRNCAT, A_STRCMP, A_STRNLEN, A_STRTOK, A_STRERROR,
    A_STRCPY_A, A_STRNCPY_A, A_STRCAT_A, A_STRNCAT_A,
    A_FREECHK, A_FREEGEN, A_REALLOC, A_MALLOC, A_CALLOC, A_ALIGNED, A_QUERY,
    NAPI
};
/* Attack indices (the eight required families) */
enum { T_NULL, T_ONE_OVER, T_SIZEMAX, T_OVERLAP, T_UNTERM,
       T_DOUBLEFREE, T_FOREIGN, T_ALIGNLIE, NTATT };

static const char *API_NAMES[NAPI] = {
    "memcpy_s","memmove_s","memset_s","memcmp_s","memzero_s",
    "memcpy_s_auto","memmove_s_auto","memset_s_auto","memcmp_s_auto",
    "strcpy_s","strncpy_s","strcat_s","strncat_s","strcmp_s","strnlen_s","strtok_s","strerror_s",
    "strcpy_s_auto","strncpy_s_auto","strcat_s_auto","strncat_s_auto",
    "free_checked_s","free_checked_generation_s","realloc_s","malloc_s","calloc_s","aligned_alloc_s","query"
};
static const char *ATT_NAMES[NTATT] = {
    "NULL_ARG","ONE_OVER","SIZE_MAX","OVERLAP","UNTERMINATED",
    "DOUBLE_FREE_UAF","FOREIGN_PTR","ALIGN_LIE"
};

#define CANARY 256u
#define CAN    0xC3u
static size_t g_ps;             /* system page size */
#define MAXCAP 512u

typedef struct {
    unsigned long id;
    int api, att;
    size_t cap, n, dm;
    unsigned v;
} Case;

static void noop_handler(const char *m, void *p, int e) {
    (void)m; (void)p; (void)e;
}

/* Two pages: page 0 RW, page 1 PROT_NONE.  Return a buffer whose end is
 * CANARY bytes before the guard page.  Fill all of page 0 with CAN. */
static unsigned char *make_guard(size_t cap, unsigned char **base_out) {
    unsigned char *base = (unsigned char *)mmap(NULL, 2 * g_ps, PROT_NONE,
                              MAP_PRIVATE | MAP_ANON, -1, 0);
    if (base == MAP_FAILED) return NULL;
    if (mprotect(base, g_ps, PROT_READ | PROT_WRITE) != 0) {
        munmap(base, 2 * g_ps);
        return NULL;
    }
    memset(base, CAN, g_ps);
    *base_out = base;
    return base + g_ps - CANARY - cap;
}
/* Return offset of first changed canary byte, or -1 when intact. */
static int canary_bad(const unsigned char *d, size_t cap) {
    size_t i;
    for (i = 0; i < CANARY; ++i)
        if (d[cap + i] != CAN) return (int)i;
    return -1;
}

/* Deterministic per-variant parameter mixing (no dependence on wall clock). */
static unsigned long rnd(unsigned long x) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17;
    return x;
}

/* Run one hostile case in a child.  Returns classification as exit code:
 * 0 REFUSED, 1 CORRUPT, 2 UNEXPECTED_OK; signals handled by parent. */
static void run_child(const Case *c, int wrfd) {
    unsigned char *dbase = NULL, *sbase = NULL;
    unsigned char *d = NULL, *s = NULL;
    int d_valid = 1;
    int diff = -777, ind = -777, bad = -1, ok = 0;
    rsize_t dmaxp = 0;
    char *tok = NULL;
    const char *delim = " ";
    long long rc = -999;
    size_t cap = c->cap, n = c->n, dm = c->dm, off;
    unsigned long r = rnd(c->v * 2654435761u + (unsigned long)c->api * 97u +
                          (unsigned long)c->att * 31u + c->id);
    void *heap = NULL, *foreign = NULL;
    unsigned long long gen = 0;
    int outcome;

    (void)signal(SIGSEGV, SIG_DFL);
    (void)signal(SIGBUS, SIG_DFL);
    alarm(2);                       /* a hang is a CRASH */

    d = make_guard(cap, &dbase);
    s = make_guard(cap, &sbase);
    if (!d || !s) _exit(3);

    /* Default: truthful bounds, valid NUL-terminated source, NUL dest. */
    memset(s, 'A', cap);
    s[cap - 1] = 0;                 /* source terminates at the last live byte */
    memset(d, CAN, cap);
    d[0] = 0;                       /* empty, terminated destination */
    off = 1 + (size_t)(r % (cap > 8 ? 8 : cap - 1));

    /* Per-attack mutation of pointers and bounds. */
    switch (c->att) {
    case T_NULL:
        switch (c->v % 4) {
        case 0: d = NULL; d_valid = 0; break;          /* null dest */
        case 1: s = NULL; break;                       /* null src */
        case 2: diff = 0; ind = 0; delim = NULL; tok = NULL; break; /* null out/delim */
        default: d = NULL; s = NULL; d_valid = 0; break;
        }
        break;
    case T_ONE_OVER:
        dm = cap + 1;
        n = cap + 1;
        memset(s, 'A', cap);                           /* src longer than cap */
        s[cap - 1] = 'A';                              /* no terminator */
        break;
    case T_SIZEMAX:
        dm = (rsize_t)SIZE_MAX;
        n = (size_t)SIZE_MAX;
        break;
    case T_OVERLAP:
        s = d + off;                                   /* src inside dest object */
        memset(d + off, 'B', cap - off - 1);
        d[cap - 1] = 0;
        break;
    case T_UNTERM:
        memset(d, 'A', cap);                           /* no NUL in dest */
        memset(s, 'A', cap);                           /* no NUL in src */
        /* A legal-but-oversized bound (RSIZE_MAX_STR == 4096 in this build)
         * on an unterminated source: the checked layer accepts it and the
         * bounded scan runs past the object into the guard page. */
        if ((c->api == A_STRNLEN || c->api == A_STRTOK) && (c->v % 3) == 0)
            dm = 4096;
        if ((c->api == A_STRCPY || c->api == A_STRNCPY ||
             c->api == A_STRCAT || c->api == A_STRNCAT ||
             c->api == A_STRCMP) && (c->v % 3) == 1)
            dm = 4096;
        break;
    case T_DOUBLEFREE:
        heap = malloc_s(cap);
        if (heap) {
            free_s(heap);                              /* now a freed address */
            d = (unsigned char *)heap;                 /* use-after-free as dest */
            s = (unsigned char *)heap;                 /* and as src */
        }
        break;
    case T_FOREIGN:
        foreign = malloc(cap ? cap : 1);               /* raw host allocation */
        if (!foreign) _exit(3);
        memset(foreign, 'A', cap);
        ((char *)foreign)[cap ? cap - 1 : 0] = 0;
        d = (unsigned char *)foreign;                  /* untracked dest */
        s = (unsigned char *)foreign;                  /* untracked src */
        break;
    case T_ALIGNLIE:
        if (cap > 1) { d += 1; s += 1; cap -= 1; }     /* deliberately misaligned */
        break;
    }

    /* Dispatch.  `ok` is 1 when the API reports success. */
    switch (c->api) {
    case A_MEMCPY:  rc = cplus_memcpy_s(d, dm, s, n); ok = (rc == 0); break;
    case A_MEMMOVE: rc = cplus_memmove_s(d, dm, s, n); ok = (rc == 0); break;
    case A_MEMSET:  rc = cplus_memset_s(d, dm, (int)(r & 0xff), n); ok = (rc == 0); break;
    case A_MEMCMP:  rc = cplus_memcmp_s(d, dm, s, n, (c->att == T_NULL && c->v % 4 == 2) ? NULL : &diff); ok = (rc == 0); break;
    case A_MEMZERO: rc = cplus_memzero_s(d, dm); ok = (rc == 0); break;
    case A_MEMCPY_A: ok = (cplus_memcpy_s_auto(d, s, n, cap) != NULL); break;
    case A_MEMMOVE_A: ok = (cplus_memmove_s_auto(d, s, n, cap) != NULL); break;
    case A_MEMSET_A: ok = (cplus_memset_s_auto(d, (int)(r & 0xff), n, cap) != NULL); break;
    case A_MEMCMP_A: {
        /* Probe the NULL-operand-at-n==0 inconsistency: for a slice of NULL
         * cases force n==0 with one or both operands NULL. */
        size_t cn = (c->att == T_NULL) ? 0 : n;
        int v = cplus_memcmp_s_auto(d, s, cn, cap, cap);
        ok = (v == 0);
        break;
    }
    case A_STRCPY:  rc = cplus_strcpy_s((char *)d, dm, (const char *)s); ok = (rc == 0); break;
    case A_STRNCPY: rc = cplus_strncpy_s((char *)d, dm, (const char *)s, (rsize_t)n); ok = (rc == 0); break;
    case A_STRCAT:  rc = cplus_strcat_s((char *)d, dm, (const char *)s); ok = (rc == 0); break;
    case A_STRNCAT: rc = cplus_strncat_s((char *)d, dm, (const char *)s, (rsize_t)n); ok = (rc == 0); break;
    case A_STRCMP:  rc = cplus_strcmp_s((const char *)d, dm, (const char *)s,
                                        (c->att == T_NULL && c->v % 4 == 2) ? NULL : &ind); ok = (rc == 0); break;
    case A_STRNLEN: { rsize_t z = cplus_strnlen_s((const char *)s, (rsize_t)dm); ok = (z != 0); rc = (long long)z; break; }
    case A_STRTOK: {
        dmaxp = (rsize_t)dm;
        if (c->att != T_SIZEMAX && dmaxp > 4096) dmaxp = 4096; /* avoid host-lie-only noise */
        char *tk = cplus_strtok_s((char *)d, &dmaxp, delim, &tok);
        ok = (tk != NULL); rc = (long long)dmaxp;
        break;
    }
    case A_STRERROR: rc = cplus_strerror_s((char *)d, dm, (int)(r & 0x7fffffff)); ok = (rc == 0); break;
    case A_STRCPY_A: ok = (cplus_strcpy_s_auto((char *)d, (const char *)s, cap) != NULL); break;
    case A_STRNCPY_A: ok = (cplus_strncpy_s_auto((char *)d, (const char *)s, n, cap) != NULL); break;
    case A_STRCAT_A: ok = (cplus_strcat_s_auto((char *)d, (const char *)s, cap) != NULL); break;
    case A_STRNCAT_A: ok = (cplus_strncat_s_auto((char *)d, (const char *)s, n, cap) != NULL); break;

    /* ---- allocator family ------------------------------------------- */
    case A_FREECHK: {
        void *p = heap ? heap : malloc_s(cap);
        cplus_safe_alloc_meta meta;
        if (!p) _exit(3);
        cplus_safe_alloc_get_meta(p, &meta);
        gen = meta.generation;
        if (c->att == T_DOUBLEFREE) { free_checked_s(p); rc = free_checked_s(p); }
        else if (c->att == T_NULL) rc = free_checked_s(NULL);
        else if (c->att == T_FOREIGN) rc = free_checked_s(foreign);
        else if (c->att == T_ALIGNLIE) rc = free_checked_s((char *)p + 1);
        else if (c->att == T_OVERLAP) rc = free_checked_s((char *)p + cap / 2);
        else if (c->att == T_UNTERM) rc = free_checked_s((char *)p + cap);
        else if (c->att == T_SIZEMAX) rc = free_checked_s((void *)(uintptr_t)SIZE_MAX);
        else rc = free_checked_s(p);                   /* valid: expect OK */
        ok = (rc == 0);
        break;
    }
    case A_FREEGEN: {
        void *p = malloc_s(cap);
        cplus_safe_alloc_meta meta;
        if (!p) _exit(3);
        cplus_safe_alloc_get_meta(p, &meta);
        gen = meta.generation;
        if (c->att == T_DOUBLEFREE) {
            free_checked_generation_s(p, gen);
            rc = free_checked_generation_s(p, gen);
        } else if (c->att == T_NULL) rc = free_checked_generation_s(NULL, gen);
        else if (c->att == T_SIZEMAX) rc = free_checked_generation_s(p, SIZE_MAX);
        else if (c->att == T_ALIGNLIE) rc = free_checked_generation_s(p, 0);
        else if (c->att == T_FOREIGN) rc = free_checked_generation_s(foreign, gen);
        else rc = free_checked_generation_s(p, gen);    /* valid: expect OK */
        ok = (rc == 0);
        break;
    }
    case A_REALLOC: {
        void *p = malloc_s(cap);
        void *q;
        if (!p) _exit(3);
        if (c->att == T_NULL) q = realloc_s(NULL, n);
        else if (c->att == T_SIZEMAX) q = realloc_s(p, SIZE_MAX);
        else if (c->att == T_ONE_OVER) q = realloc_s(p, 16385);
        else if (c->att == T_DOUBLEFREE) { free_s(p); q = realloc_s(p, cap); }
        else if (c->att == T_FOREIGN) q = realloc_s(foreign, cap);
        else if (c->att == T_ALIGNLIE) q = realloc_s((char *)p + 1, cap);
        else q = realloc_s(p, cap);                     /* valid: expect OK */
        ok = (q != NULL);
        break;
    }
    case A_MALLOC: {
        void *p;
        if (c->att == T_SIZEMAX) p = malloc_s(SIZE_MAX);
        else if (c->att == T_ONE_OVER) p = malloc_s(16385);
        else if (c->att == T_NULL) p = malloc_s(0);     /* defined 1-byte block */
        else p = malloc_s(cap);
        ok = (p != NULL);
        break;
    }
    case A_CALLOC: {
        void *p;
        if (c->att == T_SIZEMAX) p = calloc_s(SIZE_MAX, 2);
        else if (c->att == T_ONE_OVER) p = calloc_s(SIZE_MAX, cap);
        else if (c->att == T_NULL) p = calloc_s(0, 0);
        else p = calloc_s(cap, 1);
        ok = (p != NULL);
        break;
    }
    case A_ALIGNED: {
        size_t al = 8;
        void *p;
        if (c->att == T_ALIGNLIE) {
            switch (c->v % 4) {
            case 0: al = 0; break;
            case 1: al = 3; break;
            case 2: al = 6; break;
            default: al = SIZE_MAX; break;
            }
        } else if (c->att == T_SIZEMAX) al = SIZE_MAX;
        p = aligned_alloc_s(al, cap);
        ok = (p != NULL);
        /* An alignment promise that is not honoured is itself a finding. */
        if (ok && al && al != SIZE_MAX && ((uintptr_t)p % al) != 0) {
            char msg[160];
            int len = snprintf(msg, sizeof msg, "MISALIGNED p=%p al=%zu\n", p, al);
            if (len > 0) { if (len >= (int)sizeof msg) len = (int)sizeof msg - 1; (void)write(wrfd, msg, (size_t)len); }
            bad = (int)((uintptr_t)p % al);            /* force CORRUPT */
        }
        break;
    }
    case A_QUERY: {
        void *p = malloc_s(cap);
        cplus_safe_alloc_meta meta;
        if (!p) _exit(3);
        if (c->att == T_NULL) { ok = (cplus_safe_ptr_size(NULL) != 0); rc = cplus_safe_alloc_get_meta(NULL, &meta); }
        else if (c->att == T_FOREIGN) { ok = (cplus_safe_ptr_size(foreign) != 0); rc = cplus_safe_alloc_get_meta(foreign, &meta); }
        else if (c->att == T_ALIGNLIE) { ok = (cplus_safe_remaining_capacity((char *)p + cap + 8) != (size_t)-1); }
        else if (c->att == T_DOUBLEFREE) { free_s(p); ok = (cplus_safe_ptr_size(p) != 0); }
        else { cplus_safe_alloc_get_meta(p, &meta); ok = (meta.is_live == 0); }
        break;
    }
    default: ok = 0; break;
    }

    /* Canary check only where a guarded destination was actually used. */
    if (dbase && d_valid && (c->api < A_FREECHK) && (c->att != T_DOUBLEFREE) && (c->att != T_FOREIGN)) {
        int b = canary_bad(d, cap);
        if (b >= 0) bad = b;
    }

    {
        char line[192];
        int len = snprintf(line, sizeof line, "rc=%lld ok=%d errno=%d first_bad=%d\n",
                           rc, ok, errno, bad);
        if (len > 0) { if (len >= (int)sizeof line) len = (int)sizeof line - 1; (void)write(wrfd, line, (size_t)len); }
    }
    if (bad >= 0) outcome = 1;       /* CORRUPT */
    else if (ok) outcome = 2;        /* UNEXPECTED_OK */
    else outcome = 0;                /* REFUSED */
    _exit(outcome);
}

int main(int argc, char **argv) {
    FILE *flog;
    long long ntot = 0, ncrash = 0, ncor = 0, nok = 0, nref = 0;
    struct rlimit rl = {0, 0};
    const char *logfile = (argc > 1) ? argv[1] : "results/scenarios.log";
    int nv = (argc > 2) ? atoi(argv[2]) : 14;
    Case c;
    int api, att, v;
    unsigned long id = 0;

    (void)setrlimit(RLIMIT_CORE, &rl);
    g_ps = (size_t)sysconf(_SC_PAGESIZE);
    if (g_ps == 0 || g_ps > 65536) g_ps = 16384;

    cplus_safe_set_constraint_mode(0);
    cplus_sc_set_mem_constraint_handler_s_wrapper(noop_handler);
    cplus_sc_set_str_constraint_handler_s_wrapper(noop_handler);
    cplus_runtime_set_violation_trap(0);   /* inspect corruptions, never _exit(70) */

    flog = fopen(logfile, "w");
    if (!flog) { fprintf(stderr, "cannot open %s\n", logfile); return 70; }
    fprintf(flog, "# id api attack cap n dm ok_outcome observed\n");

    for (api = 0; api < NAPI; ++api) {
        for (att = 0; att < NTATT; ++att) {
            for (v = 0; v < nv; ++v) {
                int pfd[2];
                pid_t pid;
                int st = 0;
                char observed[200] = "";
                const char *outcome;
                ssize_t got;

                c.id = ++id; c.api = api; c.att = att; c.v = (unsigned)v;
                c.cap = 8 + ((v * 37u + (unsigned)api * 13u + (unsigned)att * 7u) % 248u);
                if (c.cap > MAXCAP) c.cap = MAXCAP;
                c.n = 1 + ((v * 11u + (unsigned)att * 5u) % c.cap);
                c.dm = 1 + ((v * 17u + (unsigned)api * 3u) % c.cap);

                if (pipe(pfd) != 0) { perror("pipe"); return 70; }
                pid = fork();
                if (pid < 0) { perror("fork"); return 70; }
                if (pid == 0) { close(pfd[0]); run_child(&c, pfd[1]); _exit(3); }
                close(pfd[1]);
                got = read(pfd[0], observed, sizeof observed - 1);
                if (got > 0) { observed[got] = 0; if (observed[got-1] == '\n') observed[got-1] = 0; }
                else observed[0] = 0;
                close(pfd[0]);
                waitpid(pid, &st, 0);

                if (WIFSIGNALED(st)) { outcome = "CRASH"; ++ncrash; snprintf(observed, sizeof observed, "signal=%d", WTERMSIG(st)); }
                else if (WEXITSTATUS(st) == 1) { outcome = "CORRUPT"; ++ncor; }
                else if (WEXITSTATUS(st) == 2) { outcome = "UNEXPECTED_OK"; ++nok; }
                else if (WEXITSTATUS(st) == 0) { outcome = "REFUSED"; ++nref; }
                else outcome = "SKIP";
                ++ntot;

                fprintf(flog, "%lu %s %s %zu %zu %zu %s [%s]\n",
                        c.id, API_NAMES[c.api], ATT_NAMES[c.att], c.cap, c.n, c.dm,
                        outcome, observed);
                if (strcmp(outcome, "CRASH") == 0 || strcmp(outcome, "CORRUPT") == 0) {
                    fprintf(stderr, "FINDING id=%lu api=%s att=%s cap=%zu n=%zu dm=%zu %s [%s]\n",
                            c.id, API_NAMES[c.api], ATT_NAMES[c.att], c.cap, c.n, c.dm,
                            outcome, observed);
                }
            }
        }
    }
    fclose(flog);
    printf("RED20 safecrt %lld %lld %lld %lld %lld\n", ntot, ncrash, ncor, nok, nref);
    return 0;
}
