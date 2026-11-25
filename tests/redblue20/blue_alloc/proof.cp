/* BLUE-TEAM agent 11 -- deterministic safety PROOF for the modern allocators:
 *   stdlib/alloc_iface.hp, arena.hp, mempool.hp, datastack.hp
 *
 * 2400 fixed-seed cases (SplitMix64 SEED 0xA11CE0000000000E5, per-case seed
 * SEED ^ (id+1)*GOLDEN), 300 per category:
 *   0 ALIGN      alignment 1..16 across arena / pool / datastack
 *   1 ARENA      chained block growth + reset generation invalidation
 *   2 BUF        from_buffer caller storage is used and never freed
 *   3 POOL       double-free refusal (104) + foreign-pointer refusal (106)
 *   4 SLOT       stale-handle refusal (103) + double-free refusal (104)
 *   5 SCOPE      DataStack nested scope rewind (outer data intact)
 *   6 REALLOC    top-of-stack realloc stays in place for arena + datastack
 *   7 VM         VM commit-on-demand page accounting
 *
 * Every case runs in a forked child with an alarm(60) watchdog and an 0xA5
 * canary band around a fixed work buffer.  A case PASSES only when the child
 * exits 0 AND emits the exact token "BLUE20_ALLOC_PASS <id> <cat>".  Anything
 * else (signal, hang, wrong token, canary overwrite, property violation) is a
 * FAIL.  Parent prints the final line:
 *   BLUE20 alloc <cases> <passed> <failed>
 *
 * Build (from repo root):
 *   ./build/c+ build tests/redblue20/blue_alloc/proof.cp \
 *     --default --sanitize=none -I. \
 *     -o tests/redblue20/blue_alloc/out/proof \
 *     -- build/libc/libcplus_hardened.a
 * Run:
 *   timeout 120 tests/redblue20/blue_alloc/out/proof \
 *     tests/redblue20/blue_alloc/out/results.log
 */
#include <errno.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/alloc_iface.hp"
#include "stdlib/arena.hp"
#include "stdlib/mempool.hp"
#include "stdlib/datastack.hp"

#define N_CASES 2400
#define PER_CAT 300
#define SEED 0xA11CE00000DEAD55ULL
#define GOLDEN 0x9E3779B97F4A7C15ULL
#define GUARD 32u
#define WORK 2048u
#define ALARM_SEC 60
#define TOKEN "BLUE20_ALLOC_PASS"

static unsigned char g_canary[GUARD + WORK + GUARD];
static char g_reason[128];
static unsigned long long g_digest = 1469598103934665603ULL;

typedef struct Rng { unsigned long long s; } Rng;
static void rinit(Rng *r, unsigned long long seed) { r->s = seed; }
static unsigned int rnd(Rng *r) {
    unsigned long long x = r->s;
    x ^= x >> 12; x ^= x << 25; x ^= x >> 27;
    r->s = x;
    return (unsigned int)((x * 2685821657736338717ULL) >> 32);
}
static unsigned long long case_seed(int id) {
    return SEED ^ ((unsigned long long)(id + 1) * GOLDEN);
}
static void fill_canary(void) { memset(g_canary, 0xA5, sizeof(g_canary)); }
static int canary_ok(void) {
    size_t i;
    for (i = 0; i < GUARD; i++) if (g_canary[i] != 0xA5) return 0;
    for (i = GUARD + WORK; i < GUARD + WORK + GUARD; i++)
        if (g_canary[i] != 0xA5) return 0;
    return 1;
}
static int fail(const char *why) {
    snprintf(g_reason, sizeof(g_reason), "%s", why);
    return 0;
}
static unsigned char *work(void) { return g_canary + GUARD; }

static const char *cat_name(int cat) {
    static const char *n[] = { "ALIGN", "ARENA", "BUF", "POOL",
                               "SLOT", "SCOPE", "REALLOC", "VM" };
    return n[cat];
}

/* ---------------------------- case 0: ALIGN ----------------------------- */
static int case_align(Rng *r) {
    error e = error();
    stdlib_arena a;
    stdlib_datastack ds;
    stdlib_mem_pool p;
    unsigned align = 1u << (rnd(r) % 5u); /* 1,2,4,8,16 */
    void *v[3];
    unsigned char *q;
    size_t sz[3];
    size_t i, j;
    if (stdlib_arena_init(&a, 256, &e) != STDLIB_OK) return fail("arena init");
    for (i = 0; i < 3; i++) {
        sz[i] = 1u + (rnd(r) % 32u);
        v[i] = stdlib_arena_alloc(&a, sz[i], align, &e);
        if (v[i] == (void *)0 || ((uintptr_t)v[i] & (uintptr_t)(align - 1u)) != 0)
            return fail("arena align");
        memset(v[i], 0x5A, sz[i]);
    }
    for (i = 0; i < 3; i++)
        for (j = 0; j < sz[i]; j++)
            if (((unsigned char *)v[i])[j] != 0x5A) return fail("arena overwrite");
    stdlib_arena_destroy(&a, &e);

    if (stdlib_datastack_init(&ds, 4096, &e) != STDLIB_OK) return fail("ds init");
    q = (unsigned char *)stdlib_datastack_alloc(&ds, 1u + (rnd(r) % 32u), align, &e);
    if (q == (void *)0 || ((uintptr_t)q & (uintptr_t)(align - 1u)) != 0)
        return fail("ds align");
    stdlib_datastack_destroy(&ds, &e);

    /* Slot stride is header + slot_size; a 16-multiple slot_size keeps every
     * slot 16-byte aligned (the pool's documented alignment guarantee). */
    if (stdlib_mem_pool_init(&p, 16u * (1u + rnd(r) % 3u), 8, &e) != STDLIB_OK)
        return fail("pool init");
    q = (unsigned char *)stdlib_mem_pool_alloc(&p, &e);
    if (q == (void *)0 || ((uintptr_t)q & 15u) != 0) return fail("pool align");
    stdlib_mem_pool_destroy(&p, &e);
    return 1;
}

/* ---------------------- case 1: ARENA chained growth -------------------- */
static int case_arena(Rng *r) {
    error e = error();
    stdlib_arena a;
    void *v[24];
    size_t i, j, k, blocks = 0;
    stdlib_arena_block *b;
    stdlib_arena_handle h, stale;
    size_t cap = 32u + (rnd(r) % 32u);
    if (stdlib_arena_init(&a, cap, &e) != STDLIB_OK) return fail("arena init");
    for (i = 0; i < 24; i++) {
        v[i] = stdlib_arena_alloc(&a, 32, 8, &e);
        if (v[i] == (void *)0 || ((uintptr_t)v[i] & 7u) != 0)
            return fail("arena chain alloc");
        memset(v[i], (unsigned char)(i + 1), 32);
    }
    for (b = a.block; b != (stdlib_arena_block *)0; b = b->prev) blocks++;
    if (blocks < 3) return fail("no chained growth");
    for (i = 0; i < 24; i++) {
        for (j = i + 1; j < 24; j++) {
            uintptr_t ai = (uintptr_t)v[i], aj = (uintptr_t)v[j];
            if (ai < aj + 32 && aj < ai + 32) return fail("arena overlap");
        }
        for (k = 0; k < 32; k++)
            if (((unsigned char *)v[i])[k] != (unsigned char)(i + 1))
                return fail("arena cross-block overwrite");
    }
    h = stdlib_arena_mark(&a);
    if (!stdlib_arena_handle_valid(&h)) return fail("mark invalid");
    stale = h;
    stdlib_arena_do_reset(&a, &e);
    if (stdlib_arena_handle_valid(&stale)) return fail("reset did not invalidate");
    if (h.generation == a.generation) return fail("generation not bumped");
    if (stdlib_arena_used(&a) != 0) return fail("used not rewound");
    if (a.remaining != a.first_capacity) return fail("remaining not rewound");
    if (stdlib_arena_alloc(&a, 16, 8, &e) == (void *)0) return fail("post-reset alloc");
    stdlib_arena_destroy(&a, &e);
    return 1;
}

/* ------------------- case 2: from_buffer caller storage ----------------- */
static int case_buf(Rng *r) {
    error e = error();
    stdlib_arena a;
    unsigned char *buf = work();
    size_t len = 512u + (rnd(r) % 512u), n;
    void *p;
    if (len > WORK) len = WORK;
    memset(buf, 0xA5, len);
    if (stdlib_arena_from_buffer(&a, buf, len, &e) != STDLIB_OK)
        return fail("from_buffer init");
    /* Structural proof of caller ownership: the first block is embedded in
     * caller storage (owns == 0), so destroy cannot cplus_release it. */
    if (a.owns_first != 0 || a.first_block == (stdlib_arena_block *)0 ||
        a.first_block->owns != 0 || a.block != a.first_block)
        return fail("caller block marked heap-owned");
    p = stdlib_arena_alloc(&a, 64, 8, &e);
    if (p == (void *)0) return fail("from_buffer alloc");
    if ((unsigned char *)p < buf || (unsigned char *)p + 64 > buf + len)
        return fail("alloc outside caller storage");
    memset(p, 0x3C, 64);
    for (n = 0; n < 64; n++) if (((unsigned char *)p)[n] != 0x3C)
        return fail("from_buffer write");
    stdlib_arena_destroy(&a, &e);
    if (a.block != (stdlib_arena_block *)0) return fail("arena destroy state");
    /* Caller buffer still owned by caller: writable after arena destroy. */
    buf[0] = 0x11; buf[len - 1] = 0x22;
    if (buf[0] != 0x11 || buf[len - 1] != 0x22) return fail("buffer not writable");
    buf[0] = 0xA5; buf[len - 1] = 0xA5;
    return 1;
}

/* -------------------- case 3: pool free refusal ------------------------- */
static int case_pool(Rng *r) {
    error e = error();
    stdlib_mem_pool p, other;
    unsigned char *foreign;
    void *a;
    void *b;
    void *c;
    size_t before;
    size_t slot = 16u + (rnd(r) % 16u);
    if (stdlib_mem_pool_init(&p, slot, 8, &e) != STDLIB_OK) return fail("pool init");
    if (stdlib_mem_pool_init(&other, slot, 4, &e) != STDLIB_OK) return fail("pool2 init");
    a = stdlib_mem_pool_alloc(&p, &e);
    if (a == (void *)0) return fail("pool alloc");
    stdlib_mem_pool_release(&p, a, &e);
    if (e.failed_now()) return fail("first release failed");
    before = p.live;
    error_reset(&e);
    stdlib_mem_pool_release(&p, a, &e);
    if (e.code() != CPLUS_EXIT_DOUBLE_FREE) return fail("double-free code");
    if (p.live != before) return fail("double-free changed live count");

    /* Foreign pointer with an all-zero readable header -> refused as 106. */
    foreign = work();
    memset(foreign, 0, 128);
    error_reset(&e);
    stdlib_mem_pool_release(&p, foreign + 64, &e);
    if (e.code() != CPLUS_EXIT_OWNERSHIP) return fail("foreign code");
    if (p.live != before) return fail("foreign changed live count");

    /* Foreign pointer from a different (live) pool -> also 106. */
    c = stdlib_mem_pool_alloc(&other, &e);
    if (c == (void *)0) return fail("pool2 alloc");
    error_reset(&e);
    stdlib_mem_pool_release(&p, c, &e);
    if (e.code() != CPLUS_EXIT_OWNERSHIP) return fail("cross-pool code");
    if (other.live != 1u || p.live != before) return fail("foreign changed counts");

    b = stdlib_mem_pool_alloc(&p, &e);
    if (b == (void *)0 || b != a) return fail("freed slot not reused");
    stdlib_mem_pool_destroy(&p, &e);
    stdlib_mem_pool_destroy(&other, &e);
    return 1;
}

/* --------------------- case 4: slot_pool refusal ------------------------ */
static int case_slot(Rng *r) {
    error e = error();
    stdlib_slot_pool sp;
    stdlib_slot_ref r0, stale, r2;
    void *g;
    size_t slot = 8u + (rnd(r) % 8u);
    if (stdlib_slot_pool_init(&sp, slot, 8, &e) != STDLIB_OK) return fail("slot init");
    if (stdlib_slot_pool_alloc_ref(&sp, &r0, &e) != STDLIB_OK) return fail("slot alloc");
    if (r0.index != 0) return fail("first index not 0");
    stale = r0;
    stdlib_slot_ref_free(&r0, &e);
    if (e.failed_now()) return fail("slot first free failed");
    if (sp.free_count != 1u) return fail("free list not pushed");

    error_reset(&e);
    g = stdlib_slot_ref_get(&stale, &e);   /* stale -> refused before check_live */
    if (e.code() != CPLUS_EXIT_USE_AFTER_FREE || g != (void *)0)
        return fail("stale get code");
    error_reset(&e);
    stdlib_slot_ref_free(&stale, &e);     /* stale double free -> 104 */
    if (e.code() != CPLUS_EXIT_DOUBLE_FREE) return fail("slot double-free code");
    if (sp.free_count != 1u || sp.count != 1u) return fail("refusal mutated pool");

    if (stdlib_slot_pool_alloc_ref(&sp, &r2, &e) != STDLIB_OK) return fail("slot realloc");
    if (r2.index != 0 || r2.generation == stale.generation)
        return fail("slot generation not bumped");
    g = stdlib_slot_ref_get(&r2, &e);     /* index 0 == base, live -> defined */
    if (g == (void *)0 || e.failed_now()) return fail("valid slot get");
    stdlib_slot_ref_free(&r2, &e);
    stdlib_slot_pool_destroy(&sp, &e);
    return 1;
}

/* ------------------- case 5: DataStack nested scopes -------------------- */
static int case_scope(Rng *r) {
    error e = error();
    stdlib_datastack ds;
    stdlib_scratch_mark m0, m1, stale;
    unsigned char *a;
    unsigned char *b;
    unsigned char *c;
    size_t i;
    size_t base_cap = 4096u + (rnd(r) % 4096u);
    if (stdlib_datastack_init(&ds, base_cap, &e) != STDLIB_OK) return fail("ds init");
    m0 = stdlib_scratch_enter(&ds);
    a = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 8, &e);
    if (a == (void *)0) return fail("outer alloc");
    memset(a, 0x7E, 64);
    m1 = stdlib_scratch_enter(&ds);
    stale = m1;
    b = (unsigned char *)stdlib_datastack_alloc(&ds, 128, 8, &e);
    if (b == (void *)0) return fail("inner alloc");
    memset(b, 0x1C, 128);
    stdlib_scratch_leave(&ds, m1);
    if (ds.top != m1.saved_top) return fail("inner rewind");
    c = (unsigned char *)stdlib_datastack_alloc(&ds, 96, 8, &e);
    if (c != b) return fail("rewind did not reuse slot");
    memset(c, 0x2D, 96);
    for (i = 0; i < 64; i++) if (a[i] != 0x7E) return fail("outer data clobbered");
    stdlib_scratch_leave(&ds, m0);
    if (ds.top != m0.saved_top || ds.top != ds.base) return fail("outer rewind");
    /* Stale mark cannot be used after destroy bumped generation. */
    stdlib_datastack_destroy(&ds, &e);
    stdlib_scratch_leave(&ds, stale);
    if (ds.top != (unsigned char *)0) return fail("stale mark after destroy");
    return 1;
}

/* ------------------- case 6: top-of-stack realloc ----------------------- */
static int case_realloc(Rng *r) {
    error e = error();
    stdlib_arena a;
    stdlib_datastack ds;
    unsigned char *p;
    unsigned char *q;
    size_t i;
    size_t cap = 2048u + (rnd(r) % 2048u);
    if (stdlib_arena_init(&a, cap, &e) != STDLIB_OK) return fail("arena init");
    p = (unsigned char *)stdlib_arena_alloc(&a, 64, 8, &e);
    if (p == (void *)0) return fail("arena alloc");
    for (i = 0; i < 64; i++) p[i] = (unsigned char)(i ^ 0xA5u);
    q = (unsigned char *)stdlib_arena_realloc(&a, p, 64, 256, 8, &e);
    if (q != p) return fail("arena realloc moved top block");
    for (i = 0; i < 64; i++) if (q[i] != (unsigned char)(i ^ 0xA5u))
        return fail("arena realloc lost data");
    if (a.cursor != q + 256) return fail("arena cursor not advanced");
    q = (unsigned char *)stdlib_arena_realloc(&a, p, 256, 128, 8, &e);
    if (q != p || a.cursor != q + 128) return fail("arena shrink not in place");
    stdlib_arena_destroy(&a, &e);

    if (stdlib_datastack_init(&ds, cap, &e) != STDLIB_OK) return fail("ds init");
    p = (unsigned char *)stdlib_datastack_alloc(&ds, 64, 8, &e);
    if (p == (void *)0) return fail("ds alloc");
    for (i = 0; i < 64; i++) p[i] = (unsigned char)(i ^ 0x5Au);
    q = (unsigned char *)stdlib_datastack_realloc(&ds, p, 64, 256, 8, &e);
    if (q != p) return fail("ds realloc moved top block");
    for (i = 0; i < 64; i++) if (q[i] != (unsigned char)(i ^ 0x5Au))
        return fail("ds realloc lost data");
    if (ds.top != q + 256) return fail("ds top not advanced");
    q = (unsigned char *)stdlib_datastack_realloc(&ds, p, 256, 128, 8, &e);
    if (q != p || ds.top != q + 128) return fail("ds shrink not in place");
    stdlib_datastack_destroy(&ds, &e);
    return 1;
}

/* -------------------- case 7: VM commit-on-demand ----------------------- */
static int case_vm(Rng *r) {
    error e = error();
    stdlib_datastack ds;
    size_t page = stdlib_datastack_page_size();
    size_t reserve = page * 8u;
    void *p1;
    void *p2;
    void *p3;
    (void)r;
    if (page == 0 || reserve < page * 4u) return fail("page size");
    if (stdlib_datastack_init_vm(&ds, reserve, &e) != STDLIB_OK) return fail("vm init");
    if (ds.backing != 1 || ds.committed != 0 || ds.top != ds.base)
        return fail("vm initial state");
    p1 = stdlib_datastack_alloc(&ds, 1, 1, &e);
    if (p1 == (void *)0) return fail("vm alloc1");
    if (ds.committed != page) return fail("vm first commit != page");
    p2 = stdlib_datastack_alloc(&ds, 1, 1, &e);
    if (p2 == (void *)0) return fail("vm alloc2");
    if (ds.committed != page) return fail("vm committed same page");
    if (stdlib_datastack_ensure(&ds, page, &e) != STDLIB_OK ||
        ds.committed != page) return fail("vm ensure no-op");
    p3 = stdlib_datastack_alloc(&ds, page + 1u, 1, &e);
    if (p3 == (void *)0) return fail("vm alloc3");
    if (ds.committed != 2u * page) return fail("vm second commit != 2 pages");
    if (ds.top - ds.base != (ptrdiff_t)(page + 3u)) return fail("vm top accounting");
    stdlib_datastack_destroy(&ds, &e);
    if (ds.base != (unsigned char *)0 || ds.committed != 0)
        return fail("vm destroy state");
    return 1;
}

/* -------------------------------- dispatch ------------------------------ */
static int run_case(int id, int cat, Rng *r) {
    switch (cat) {
        case 0: return case_align(r);
        case 1: return case_arena(r);
        case 2: return case_buf(r);
        case 3: return case_pool(r);
        case 4: return case_slot(r);
        case 5: return case_scope(r);
        case 6: return case_realloc(r);
        default: return case_vm(r);
    }
}

static int fork_case(int id, int cat, int pfds[2], pid_t *pid) {
    Rng r;
    if (pipe(pfds) != 0) return -1;
    *pid = fork();
    if (*pid < 0) { close(pfds[0]); close(pfds[1]); return -1; }
    if (*pid == 0) {
        int ok;
        char line[192];
        close(pfds[0]);
        alarm(ALARM_SEC);
        fill_canary();
        rinit(&r, case_seed(id));
        g_reason[0] = '\0';
        ok = run_case(id, cat, &r);
        if (!canary_ok() && ok) ok = fail("canary overwritten");
        if (ok)
            snprintf(line, sizeof(line), "%s %d %s\n", TOKEN, id, cat_name(cat));
        else
            snprintf(line, sizeof(line), "BLUE20_ALLOC_FAIL %d %s %s\n",
                     id, cat_name(cat), g_reason);
        (void)write(pfds[1], line, strlen(line));
        close(pfds[1]);
        _exit(ok ? 0 : 1);
    }
    close(pfds[1]);
    return 0;
}

static size_t read_line(int fd, char *out, size_t cap) {
    size_t n = 0;
    for (;;) {
        char c = '\0';
        ssize_t k = read(fd, &c, 1);
        if (k <= 0 || c == '\n') break;
        if (n + 1 < cap) out[n++] = c; else break;
    }
    out[n] = '\0';
    return n;
}
static void hash_line(const char *s) {
    while (*s) {
        unsigned char c = (unsigned char)*s;
        g_digest ^= c; g_digest *= 1099511628211ULL; s++;
    }
    g_digest ^= '\n'; g_digest *= 1099511628211ULL;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : "tests/redblue20/blue_alloc/out/results.log";
    FILE *log = fopen(path, "w");
    char line[256], want[192];
    int pfds[2], status = 0, id, cat, passed = 0, failed = 0;
    pid_t pid = (pid_t)0;
    if (log == (FILE *)0) {
        fprintf(stderr, "BLUE20: cannot open %s\n", path);
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    for (id = 0; id < N_CASES; id++) {
        char rec[320];
        size_t n;
        int rc;
        cat = id / PER_CAT;
        if (fork_case(id, cat, pfds, &pid) != 0) {
            snprintf(rec, sizeof(rec), "E %05d %s FORK_FAIL\n", id, cat_name(cat));
            fputs(rec, log); failed++; hash_line(rec); continue;
        }
        n = read_line(pfds[0], line, sizeof(line));
        close(pfds[0]);
        rc = waitpid(pid, &status, 0);
        snprintf(want, sizeof(want), "%s %d %s", TOKEN, id, cat_name(cat));
        if (rc != pid || WIFSIGNALED(status) || !WIFEXITED(status) ||
            WEXITSTATUS(status) != 0 || n == 0 || strcmp(line, want) != 0) {
            snprintf(rec, sizeof(rec), "FAIL id=%d cat=%s rc=%d sig=%d line=%s\n",
                     id, cat_name(cat),
                     WIFEXITED(status) ? WEXITSTATUS(status) : -1,
                     WIFSIGNALED(status) ? WTERMSIG(status) : 0, line);
            fputs(rec, log); failed++; hash_line(rec); continue;
        }
        snprintf(rec, sizeof(rec), "PASS %s\n", line);
        fputs(rec, log); passed++; hash_line(rec);
    }
    fclose(log);
    printf("BLUE20_DIGEST %016llx\n", g_digest);
    printf("BLUE20 alloc %d %d %d\n", N_CASES, passed, failed);
    return failed == 0 ? 0 : 1;
}
