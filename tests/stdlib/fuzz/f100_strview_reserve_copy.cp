/* F-077 - deterministic fuzz config: reserve / copy_from / self-append.
 *
 * Two independently shadowed strings are mutated through their documented
 * bounded success paths.  copy_from (including self-copy), reserve and
 * self-referential append all exercise reallocation and aliasing logic.
 * Output: f100_strview_reserve_copy_digest <hex> / "...: 1200 cases ok".
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define CASES 1200
#define MAXSTR 48
#define NOPS 9

static const char ALPHA[] = "abcdefghijklmnopqrstuvwxyz0123456789";

static unsigned long long rng_state = 0xF077C0DEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

static string g_a;
static string g_b;
static char sa[MAXSTR + 1];
static char sb[MAXSTR + 1];
static size_t na = 0;
static size_t nb = 0;

static int fail_here(const char *label) {
    print("F-077 INVARIANT", label);
    return 1;
}

static int check_one(string *s, char *shadow, size_t n, const char *tag) {
    size_t i;
    string_view tv;
    if (!s->valid()) {
        print("F-077 INVARIANT", tag, "valid");
        return 1;
    }
    if (s->size() != n || s->capacity() < n || strlen(s->c_str()) != n) {
        print("F-077 INVARIANT", tag, "extent");
        return 1;
    }
    for (i = 0; i < n; i++)
        if (s->at(i) != shadow[i]) {
            print("F-077 INVARIANT", tag, "at");
            return 1;
        }
    if (n > 0 && memcmp(s->c_str(), shadow, n) != 0) {
        print("F-077 INVARIANT", tag, "contents");
        return 1;
    }
    if (!s->is_valid_utf8()) {
        print("F-077 INVARIANT", tag, "utf8");
        return 1;
    }
    tv = s->to_view();
    if (tv.size() != n) {
        print("F-077 INVARIANT", tag, "view.size");
        return 1;
    }
    return 0;
}

static int check_invariants(void) {
    if (check_one(&g_a, sa, na, "a") != 0)
        return 1;
    if (check_one(&g_b, sb, nb, "b") != 0)
        return 1;
    return 0;
}

static size_t make_piece(unsigned int r, char *out, size_t cap) {
    size_t n = 1 + (size_t)(r % 5u);
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

    if (g_a.append("alpha") != 0 || g_b.append("beta") != 0)
        return fail_here("init");
    na = 5; memcpy(sa, "alpha", 5); sa[na] = '\0';
    nb = 4; memcpy(sb, "beta", 4); sb[nb] = '\0';
    if (check_invariants() != 0)
        return 1;

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        int which = (int)(r1 & 1u);
        size_t plen;

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);

        switch (kind) {
        case 0: /* bounded append to a */
        case 1: /* bounded append to b */
            plen = make_piece(r2, piece, sizeof(piece));
            if (which == 0) {
                if (na + plen > MAXSTR) { g_a.clear(); na = 0; sa[0] = '\0'; }
                if (g_a.append(piece) != 0) return fail_here("a.append");
                memcpy(sa + na, piece, plen); na += plen; sa[na] = '\0';
            } else {
                if (nb + plen > MAXSTR) { g_b.clear(); nb = 0; sb[0] = '\0'; }
                if (g_b.append(piece) != 0) return fail_here("b.append");
                memcpy(sb + nb, piece, plen); nb += plen; sb[nb] = '\0';
            }
            break;

        case 2: /* bounded reserve on a */
            if (g_a.reserve((size_t)(r2 % (2u * MAXSTR + 1u))) != 0)
                return fail_here("a.reserve");
            break;

        case 3: /* bounded reserve on b */
            if (g_b.reserve((size_t)(r2 % (2u * MAXSTR + 1u))) != 0)
                return fail_here("b.reserve");
            break;

        case 4: /* a.copy_from(b) */
            if (g_a.copy_from(&g_b) != 0) return fail_here("a.copy_from");
            na = nb; memcpy(sa, sb, nb + 1u);
            break;

        case 5: /* b.copy_from(a) */
            if (g_b.copy_from(&g_a) != 0) return fail_here("b.copy_from");
            nb = na; memcpy(sb, sa, na + 1u);
            break;

        case 6: /* self copy: documented no-op */
            if (which == 0) {
                if (g_a.copy_from(&g_a) != 0) return fail_here("a.copy_self");
            } else {
                if (g_b.copy_from(&g_b) != 0) return fail_here("b.copy_self");
            }
            break;

        case 7: /* bounded self-append of a suffix */
            if (which == 0) {
                if (na == 0) {
                    if (g_a.append("a") != 0) return fail_here("a.self.seed");
                    sa[0] = 'a'; na = 1; sa[na] = '\0';
                } else {
                    size_t keep = 1 + (size_t)(r2 % na), off = na - keep;
                    if (na + keep > MAXSTR) { off += na + keep - MAXSTR; keep = na - off; }
                    if (g_a.append(g_a.c_str() + off) != 0) return fail_here("a.self");
                    memcpy(sa + na, sa + off, keep); na += keep; sa[na] = '\0';
                }
            } else {
                if (nb == 0) {
                    if (g_b.append("b") != 0) return fail_here("b.self.seed");
                    sb[0] = 'b'; nb = 1; sb[nb] = '\0';
                } else {
                    size_t keep = 1 + (size_t)(r2 % nb), off = nb - keep;
                    if (nb + keep > MAXSTR) { off += nb + keep - MAXSTR; keep = nb - off; }
                    if (g_b.append(g_b.c_str() + off) != 0) return fail_here("b.self");
                    memcpy(sb + nb, sb + off, keep); nb += keep; sb[nb] = '\0';
                }
            }
            break;

        default: /* clear one string */
            if (which == 0) { g_a.clear(); na = 0; sa[0] = '\0'; }
            else           { g_b.clear(); nb = 0; sb[0] = '\0'; }
            break;
        }

        digest = mix(digest, (unsigned long long)na);
        digest = mix(digest, (unsigned long long)nb);
        digest = mix(digest, (unsigned long long)g_a.capacity());
        digest = mix(digest, (unsigned long long)g_b.capacity());
        if (check_invariants() != 0)
            return 1;
        cases++;
    }

    g_a.drop(); g_b.drop();
    g_a.drop(); g_b.drop();
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_strview_reserve_copy_digest", hex);
    print("f100_strview_reserve_copy:", cases, "cases ok");
    return 0;
}
