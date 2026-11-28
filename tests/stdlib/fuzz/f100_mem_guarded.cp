/* F-084 - deterministic fuzz config: guarded memory copy/move/set.
 *
 * Two tracked stdlib_alloc buffers are mutated only through memory.hp's
 * guarded helpers.  Byte-exact shadow arrays check every write; documented
 * out-of-range attempts are verified to fail without touching the buffer.
 * Capacity and generation-token queries are folded into the invariant check.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

#define CASES 1200
#define CAP 64
#define NOPS 5

static unsigned long long rng_state = 0xF084C0DEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

static void *gd = (void *)0;
static void *gs = (void *)0;
static unsigned char dsh[CAP];
static unsigned char ssh[CAP];
static unsigned long long td = 0;
static unsigned long long ts = 0;

static int fail_here(const char *label) {
    print("F-084 INVARIANT", label);
    return 1;
}

static int check_invariants(void) {
    unsigned char *db = (unsigned char *)gd;
    unsigned char *sb = (unsigned char *)gs;
    error e = error();
    size_t count = 0;
    if (stdlib_mem_capacity(gd) != (size_t)CAP ||
        stdlib_mem_capacity(gs) != (size_t)CAP)
        return fail_here("capacity");
    if (stdlib_mem_count(gd, 1u, &count) != 0 || count != (size_t)CAP)
        return fail_here("count");
    if (stdlib_mem_check(gd, td, &e) != STDLIB_OK || !e.ok())
        return fail_here("token.dst");
    if (stdlib_mem_check(gs, ts, &e) != STDLIB_OK || !e.ok())
        return fail_here("token.src");
    if (memcmp(db, dsh, CAP) != 0) return fail_here("dst.bytes");
    if (memcmp(sb, ssh, CAP) != 0) return fail_here("src.bytes");
    return 0;
}

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    unsigned char *db;
    unsigned char *sb;
    char hex[24];
    int i;
    int cases = 0;
    size_t k;
    size_t doff, soff, n;
    size_t count = 0;
    int value;
    int rc = 0;

    gd = stdlib_alloc(CAP);
    gs = stdlib_alloc(CAP);
    if (gd == (void *)0 || gs == (void *)0)
        return fail_here("alloc");
    db = (unsigned char *)gd;
    sb = (unsigned char *)gs;
    for (k = 0; k < CAP; k++) {
        db[k] = (unsigned char)(k & 0xFFu);
        sb[k] = (unsigned char)((k * 7u + 3u) & 0xFFu);
        dsh[k] = db[k];
        ssh[k] = sb[k];
    }
    td = stdlib_mem_token(gd);
    ts = stdlib_mem_token(gs);
    if (td == 0 || ts == 0 || check_invariants() != 0)
        return fail_here("init");

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        error err = error();

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);

        doff = (size_t)(r1 % (unsigned int)(CAP + 1u));
        soff = (size_t)(r2 % (unsigned int)(CAP + 1u));
        n = (size_t)((r1 >> 8u) % 9u);
        value = (int)(r2 & 0xFFu);
        if (n > CAP - doff) n = CAP - doff;

        switch (kind) {
        case 0: /* guarded copy src -> dst */
            if (n > CAP - soff) n = CAP - soff;
            rc = stdlib_mem_copy(gd, doff, gs, soff, n, td, &err);
            if (rc != STDLIB_OK || !err.ok()) return fail_here("copy.rc");
            memmove(dsh + doff, ssh + soff, n);
            break;

        case 1: /* guarded overlapping move inside dst */
            if (n > CAP - soff) n = CAP - soff;
            rc = stdlib_mem_move(gd, doff, gd, soff, n, td, &err);
            if (rc != STDLIB_OK || !err.ok()) return fail_here("move.rc");
            memmove(dsh + doff, dsh + soff, n);
            break;

        case 2: /* guarded fill */
            rc = stdlib_mem_set(gd, doff, value, n, td, &err);
            if (rc != STDLIB_OK || !err.ok()) return fail_here("set.rc");
            memset(dsh + doff, value, n);
            break;

        case 3: /* documented out-of-range refusal, no write */
            {
                size_t badoff = CAP + 1u + (size_t)(r1 % 4u);
                rc = stdlib_mem_set(gd, badoff, value, 1u, td, &err);
                if (rc != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_BOUNDS)
                    return fail_here("bounds.off");
                rc = stdlib_mem_set(gd, CAP, value, 2u, td, &err);
                if (rc != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_BOUNDS)
                    return fail_here("bounds.len");
            }
            break;

        default: /* pure queries, including an opt-out token of zero */
            {
                size_t bn = n;
                if (bn > CAP - soff) bn = CAP - soff;
                if (stdlib_mem_borrow(gs, soff, bn, ts, &err) != STDLIB_OK ||
                    !err.ok())
                    return fail_here("borrow");
            }
            if (stdlib_mem_borrow(gs, 0u, 0u, 0ULL, &err) != STDLIB_OK ||
                !err.ok())
                return fail_here("borrow.zero");
            if (stdlib_mem_count(gd, 0u, &count) != 0 || count != (size_t)CAP)
                return fail_here("count.default.elem");
            break;
        }

        digest = mix(digest, (unsigned long long)doff);
        digest = mix(digest, (unsigned long long)soff);
        digest = mix(digest, (unsigned long long)n);
        digest = mix(digest, (unsigned long long)value);
        digest = mix(digest, (unsigned long long)dsh[doff < CAP ? doff : 0u]);
        digest = mix(digest, (unsigned long long)(rc + 2));
        if (check_invariants() != 0)
            return 1;
        cases++;
    }

    stdlib_mem_release(&gd);
    stdlib_mem_release(&gs);
    stdlib_mem_release(&gd); /* repeated cleanup is a no-op */
    stdlib_mem_release(&gs);
    if (gd != (void *)0 || gs != (void *)0)
        return fail_here("release.handle");

    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_mem_guarded_digest", hex);
    print("f100_mem_guarded:", cases, "cases ok");
    return 0;
}
