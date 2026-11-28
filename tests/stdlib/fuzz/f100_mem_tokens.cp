/* F-085 - deterministic fuzz config: memory generation tokens and staleness.
 *
 * Exercises stdlib/memory.hp token capture, guarded writes with a current
 * token, refusal with a stale token after stdlib_realloc / stdlib_mem_transfer,
 * borrow checks, untracked-pointer refusal and repeated release.  The shadow
 * tracks the current capacity and byte contents.  Only deterministic status
 * booleans enter the digest, never raw token or pointer values.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/memory.hp"

#define CASES 1200
#define CAP 96
#define NOPS 6

static unsigned long long rng_state = 0xF085C0DEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

static void *ga = (void *)0;
static unsigned char dsh[CAP];
static size_t cap = CAP;
static unsigned long long cur_token = 0;
static unsigned long long old_token = 0;
static int have_old = 0;

static int fail_here(const char *label) {
    print("F-085 INVARIANT", label);
    return 1;
}

static int check_invariants(void) {
    unsigned char *b = (unsigned char *)ga;
    error e = error();
    size_t k;
    if (ga == (void *)0) return fail_here("null.handle");
    if (stdlib_mem_capacity(ga) != cap) return fail_here("capacity");
    if (stdlib_mem_check(ga, cur_token, &e) != STDLIB_OK || !e.ok())
        return fail_here("current.token");
    if (have_old && stdlib_alloc_check_generation(ga, old_token) != 0)
        return fail_here("old.token.not.stale");
    for (k = 0; k < cap; k++)
        if (b[k] != dsh[k]) return fail_here("bytes");
    return 0;
}

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    unsigned char *b;
    char hex[24];
    int i;
    int cases = 0;
    int rc = 0;
    size_t k;
    size_t off, n;

    ga = stdlib_alloc(CAP);
    if (ga == (void *)0) return fail_here("alloc");
    b = (unsigned char *)ga;
    for (k = 0; k < CAP; k++) {
        b[k] = (unsigned char)(k ^ 0x5Au);
        dsh[k] = b[k];
    }
    cur_token = stdlib_mem_token(ga);
    if (cur_token == 0 || check_invariants() != 0)
        return fail_here("init");

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        error err = error();

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);

        off = (size_t)(r1 % cap);
        n = (size_t)((r2 >> 8u) % 7u);
        if (n > cap - off) n = cap - off;

        switch (kind) {
        case 0: /* guarded write with the current token */
            rc = stdlib_mem_set(ga, off, (int)(r2 & 0xFFu), n, cur_token, &err);
            if (rc != STDLIB_OK || !err.ok())
                return fail_here("set.current");
            memset(dsh + off, (int)(r2 & 0xFFu), n);
            break;

        case 1: /* realloc invalidates the captured token */
            {
                size_t newcap = 32u + (size_t)(r1 % (CAP - 32u + 1u));
                void *fresh = (void *)0;
                old_token = cur_token;
                have_old = 1;
                fresh = stdlib_realloc(ga, newcap);
                if (fresh == (void *)0) return fail_here("realloc");
                ga = fresh;
                cap = newcap;
                b = (unsigned char *)ga;
                /* Re-establish a deterministic byte image: bytes beyond the
                 * old extent are unspecified after a growing realloc. */
                for (k = 0; k < cap; k++) {
                    b[k] = (unsigned char)((k * 3u + 1u) & 0xFFu);
                    dsh[k] = b[k];
                }
                cur_token = stdlib_mem_token(ga);
                if (cur_token == 0 || cur_token == old_token)
                    return fail_here("realloc.token");
                if (stdlib_mem_capacity(ga) != cap)
                    return fail_here("realloc.capacity");
                rc = stdlib_mem_set(ga, 0u, 0xEE, 1u, old_token, &err);
                if (rc != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_LIFECYCLE)
                    return fail_here("realloc.stale.refusal");
                digest = mix(digest, (unsigned long long)cap);
                digest = mix(digest, 1ULL); /* stale token was refused */
            }
            break;

        case 2: /* transfer invalidates the token; transfer back invalidates it again */
            {
                void *gb = (void *)0;
                error e2 = error();
                old_token = cur_token;
                have_old = 1;
                if (stdlib_mem_transfer(&gb, &ga, &err) != STDLIB_OK)
                    return fail_here("transfer.out");
                if (ga != (void *)0 || gb == (void *)0)
                    return fail_here("transfer.handles");
                cur_token = stdlib_mem_token(gb);
                if (cur_token == old_token)
                    return fail_here("transfer.token");
                rc = stdlib_mem_set(gb, 0u, 0xDD, 1u, old_token, &err);
                if (rc != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_LIFECYCLE)
                    return fail_here("transfer.stale.refusal");
                if (stdlib_mem_check(gb, old_token, &e2) != STDLIB_FAIL ||
                    e2.ok() || e2.category() != STDLIB_CATEGORY_LIFECYCLE)
                    return fail_here("transfer.stale.check");
                old_token = cur_token;
                if (stdlib_mem_transfer(&ga, &gb, &err) != STDLIB_OK)
                    return fail_here("transfer.back");
                if (gb != (void *)0 || ga == (void *)0)
                    return fail_here("transfer.back.handles");
                cur_token = stdlib_mem_token(ga);
                if (cur_token == old_token)
                    return fail_here("transfer.back.token");
                rc = stdlib_mem_check(ga, old_token, &e2);
                if (rc != STDLIB_FAIL || e2.ok())
                    return fail_here("transfer.back.stale");
                b = (unsigned char *)ga;
                digest = mix(digest, 2ULL);
            }
            break;

        case 3: /* borrow with current and (when available) stale token */
            if (stdlib_mem_borrow(ga, off, n, cur_token, &err) != STDLIB_OK ||
                !err.ok())
                return fail_here("borrow.current");
            if (have_old) {
                rc = stdlib_mem_borrow(ga, off, n, old_token, &err);
                if (rc != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_LIFECYCLE)
                    return fail_here("borrow.stale");
                digest = mix(digest, 3ULL);
            } else {
                digest = mix(digest, 0ULL);
            }
            break;

        case 4: /* NULL and untracked pointers are refused, never dereferenced */
            {
                int untracked = 7;
                rc = stdlib_mem_check((const void *)0, 0ULL, &err);
                if (rc != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_INVALID)
                    return fail_here("check.null");
                rc = stdlib_mem_check((const void *)&untracked, 0ULL, &err);
                if (rc != STDLIB_FAIL || err.ok() ||
                    err.category() != STDLIB_CATEGORY_INVALID)
                    return fail_here("check.untracked");
                digest = mix(digest, 4ULL);
            }
            break;

        default: /* guarded overlapping move with the current token */
            {
                size_t soff = (size_t)(r1 % cap);
                size_t mn = n;
                if (mn > cap - soff) mn = cap - soff;
                rc = stdlib_mem_move(ga, off, ga, soff, mn, cur_token, &err);
                if (rc != STDLIB_OK || !err.ok())
                    return fail_here("move.current");
                memmove(dsh + off, dsh + soff, mn);
                digest = mix(digest, (unsigned long long)mn);
            }
            break;
        }

        digest = mix(digest, (unsigned long long)off);
        digest = mix(digest, (unsigned long long)n);
        digest = mix(digest, (unsigned long long)(rc + 2));
        digest = mix(digest, (unsigned long long)cap);
        digest = mix(digest, dsh[off]);
        if (check_invariants() != 0)
            return 1;
        cases++;
    }

    stdlib_mem_release(&ga);
    stdlib_mem_release(&ga);
    if (ga != (void *)0) return fail_here("release");

    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_mem_tokens_digest", hex);
    print("f100_mem_tokens:", cases, "cases ok");
    return 0;
}
