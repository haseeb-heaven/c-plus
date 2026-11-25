/* F-076 - deterministic fuzz config: string append with bounded lengths.
 *
 * Standalone program (own main).  Fixed-seed LCG, 1200 valid operations.
 * Drives stdlib/string.hp::append (plain, empty and self-append) against an
 * independent byte shadow.  Every generated length is bounded so the append is
 * always a documented-valid operation: a crash or a shadow mismatch is a real
 * library defect, not a contract-violating caller.
 *
 * Output (identical across default / strict+none / strict+UBSan):
 *   f100_strview_append_digest <16-hex-digits>
 *   f100_strview_append: 1200 cases ok
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define CASES 1200
#define MAXSTR 64
#define NOPS 5

static const char ALPHA[] = "abcdefghijklmnopqrstuvwxyz0123456789,.-";

/* Fixed seed, no environment input, no wall clock. */
static unsigned long long rng_state = 0xF076C0DEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

static string g_s;
static char s_shadow[MAXSTR + 1];
static size_t s_n = 0;

static int fail_here(const char *label) {
    print("F-076 INVARIANT", label);
    return 1;
}

/* Independent shadow check after every operation. */
static int check_invariants(void) {
    size_t i;
    if (!g_s.valid())
        return fail_here("valid");
    if (g_s.size() != s_n)
        return fail_here("size");
    if (g_s.capacity() < g_s.size())
        return fail_here("capacity");
    if (strlen(g_s.c_str()) != s_n)
        return fail_here("strlen");
    for (i = 0; i < s_n; i++)
        if (g_s.at(i) != s_shadow[i])
            return fail_here("at");
    if (s_n > 0 && memcmp(g_s.c_str(), s_shadow, s_n) != 0)
        return fail_here("contents");
    if (!g_s.is_valid_utf8())
        return fail_here("utf8");
    {
        string_view tv = g_s.to_view();
        if (tv.size() != s_n)
            return fail_here("view.size");
    }
    return 0;
}

/* Build a short NUL-terminated ASCII piece from a bounded word. */
static size_t make_piece(unsigned int r, char *out, size_t cap) {
    size_t n = 1 + (size_t)(r % 6u);
    size_t i;
    if (n + 1 > cap)
        n = cap - 1;
    for (i = 0; i < n; i++)
        out[i] = ALPHA[(r >> (8u + 4u * (unsigned int)i)) %
                       (sizeof(ALPHA) - 1u)];
    out[n] = '\0';
    return n;
}

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    char piece[8];
    char hex[24];
    int i;
    int cases = 0;

    if (g_s.append("seed") != 0)
        return fail_here("init.append");
    s_n = 4;
    memcpy(s_shadow, "seed", 4);
    s_shadow[s_n] = '\0';
    if (check_invariants() != 0)
        return 1;

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        size_t plen = 0;
        size_t off = 0;
        int rc;

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);

        switch (kind) {
        case 0: /* bounded plain append */
            plen = make_piece(r1, piece, sizeof(piece));
            if (s_n + plen > MAXSTR) {
                g_s.clear();
                s_n = 0;
                s_shadow[0] = '\0';
            }
            rc = g_s.append(piece);
            if (rc != 0)
                return fail_here("append.plain");
            memcpy(s_shadow + s_n, piece, plen);
            s_n += plen;
            s_shadow[s_n] = '\0';
            break;

        case 1: /* empty append: documented no-op, even on an empty string */
            rc = g_s.append("");
            if (rc != 0)
                return fail_here("append.empty");
            break;

        case 2: /* clear then append; bounds are recomputed from the shadow */
            g_s.clear();
            s_n = 0;
            s_shadow[0] = '\0';
            plen = make_piece(r2, piece, sizeof(piece));
            if (g_s.append(piece) != 0)
                return fail_here("append.after.clear");
            memcpy(s_shadow, piece, plen);
            s_n = plen;
            s_shadow[s_n] = '\0';
            break;

        case 3: /* self-append of a suffix; append() must handle the alias */
            if (s_n == 0) {
                if (g_s.append("s") != 0)
                    return fail_here("append.self.seed");
                s_shadow[0] = 's';
                s_n = 1;
                s_shadow[s_n] = '\0';
            } else {
                size_t keep = 1 + (size_t)(r1 % s_n);
                off = s_n - keep;
                plen = keep;
                if (s_n + plen > MAXSTR) {
                    off += s_n + plen - MAXSTR;
                    plen = s_n - off;
                }
                rc = g_s.append(g_s.c_str() + off);
                if (rc != 0)
                    return fail_here("append.self");
                memcpy(s_shadow + s_n, s_shadow + off, plen);
                s_n += plen;
                s_shadow[s_n] = '\0';
            }
            break;

        default: /* bounded reserve followed by a bounded append */
            plen = make_piece(r2, piece, sizeof(piece));
            if (s_n + plen > MAXSTR) {
                g_s.clear();
                s_n = 0;
                s_shadow[0] = '\0';
            }
            rc = g_s.reserve(s_n + plen + 1u);
            if (rc != 0)
                return fail_here("reserve");
            if (g_s.append(piece) != 0)
                return fail_here("append.after.reserve");
            memcpy(s_shadow + s_n, piece, plen);
            s_n += plen;
            s_shadow[s_n] = '\0';
            break;
        }

        digest = mix(digest, (unsigned long long)plen);
        digest = mix(digest, (unsigned long long)off);
        digest = mix(digest, (unsigned long long)s_n);
        digest = mix(digest, (unsigned long long)g_s.capacity());
        digest = mix(digest, (unsigned long long)(unsigned char)
                     g_s.at(s_n > 0 ? s_n - 1u : 0u));

        if (check_invariants() != 0)
            return 1;
        cases++;
    }

    g_s.drop();
    g_s.drop(); /* idempotent cleanup */
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_strview_append_digest", hex);
    print("f100_strview_append:", cases, "cases ok");
    return 0;
}
