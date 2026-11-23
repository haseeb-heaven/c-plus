/* F-080 - deterministic fuzz config: stale string_view lifecycle.
 *
 * A borrowed view is captured from a live string and then kept across
 * append / reserve / clear / drop / reinitialise.  The stale view is checked
 * ONLY by its stored pointer (as an integer) and its stored size: it is never
 * dereferenced and neither equals() nor is_valid_utf8() is called on it.
 * Fresh views are used normally to confirm the owner's current extent.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define CASES 1200
#define MAXSTR 48
#define NOPS 7

static const char ALPHA[] = "abcdefghijklmnopqrstuvwxyz0123456789";

static unsigned long long rng_state = 0xF080C0DEULL;

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
static char sh[MAXSTR + 1];
static size_t sn = 0;

static string_view g_stale;
static size_t g_stale_size = 0;
static uintptr_t g_stale_addr = 0;
static int g_have_stale = 0;

static int fail_here(const char *label) {
    print("F-080 INVARIANT", label);
    return 1;
}

static int check_owner(void) {
    size_t i;
    string_view cur;
    if (sn > 0) {
        if (!g_s.valid() || g_s.size() != sn || g_s.capacity() < sn ||
            strlen(g_s.c_str()) != sn)
            return fail_here("owner.extent");
        for (i = 0; i < sn; i++)
            if (g_s.at(i) != sh[i]) return fail_here("owner.at");
        if (memcmp(g_s.c_str(), sh, sn) != 0) return fail_here("owner.bytes");
    }
    cur = g_s.to_view();
    if (cur.size() != sn) return fail_here("fresh.size");
    if (sn > 0 && cur.data() == (const char *)0)
        return fail_here("fresh.null");
    return 0;
}

/* Stale view: compare stored scalar fields only.  No dereference, ever. */
static int check_stale(void) {
    if (!g_have_stale)
        return 0;
    if (g_stale.size() != g_stale_size)
        return fail_here("stale.size.changed");
    if ((uintptr_t)g_stale.data() != g_stale_addr)
        return fail_here("stale.ptr.changed");
    return 0;
}

static size_t make_piece(unsigned int r, char *out, size_t cap) {
    size_t n = 1 + (size_t)(r % 5u);
    size_t i;
    if (n + 1 > cap) n = cap - 1;
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

    if (g_s.append("seed") != 0) return fail_here("init");
    sn = 4; memcpy(sh, "seed", 4); sh[sn] = '\0';
    if (check_owner() != 0) return 1;

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        size_t plen;

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);

        switch (kind) {
        case 0: /* capture a borrowed view from the live string */
            {
                string_view cur = g_s.to_view();
                g_stale = cur;
                g_stale_size = cur.size();
                g_stale_addr = (uintptr_t)cur.data();
                g_have_stale = 1;
                digest = mix(digest, (unsigned long long)g_stale_size);
                digest = mix(digest, (unsigned long long)(g_stale_addr != 0));
            }
            break;

        case 1: /* append; any reallocation is invisible to the old view */
            plen = make_piece(r1, piece, sizeof(piece));
            if (sn + plen > MAXSTR) { g_s.clear(); sn = 0; sh[0] = '\0'; }
            if (g_s.append(piece) != 0) return fail_here("append");
            memcpy(sh + sn, piece, plen); sn += plen; sh[sn] = '\0';
            digest = mix(digest, (unsigned long long)plen);
            break;

        case 2: /* reserve may move the owner's buffer */
            if (g_s.reserve((size_t)(r1 % (2u * MAXSTR + 1u))) != 0)
                return fail_here("reserve");
            break;

        case 3: /* clear: fresh view size becomes 0, stale size does not */
            g_s.clear();
            sn = 0;
            sh[0] = '\0';
            break;

        case 4: /* drop + reinitialise through append (documented path) */
            g_s.drop();
            {
                string_view after = g_s.to_view();
                const char *empty = g_s.c_str();
                if (after.size() != 0 || empty[0] != '\0')
                    return fail_here("after.drop");
            }
            if (g_s.append("re") != 0) return fail_here("reinit.append");
            sn = 2; memcpy(sh, "re", 2); sh[sn] = '\0';
            break;

        case 5: /* zero-window / null-data views: accessors only */
            {
                string_view zv = string_view((const char *)0, 0);
                string_view nz = string_view((const char *)0, 3);
                if (zv.size() != 0 || zv.data() != (const char *)0 ||
                    nz.size() != 3 || nz.data() != (const char *)0)
                    return fail_here("null.view.accessors");
                digest = mix(digest, (unsigned long long)(zv.size() + nz.size()));
            }
            break;

        default: /* explicit stale re-check point */
            digest = mix(digest, (unsigned long long)g_have_stale);
            break;
        }

        digest = mix(digest, (unsigned long long)sn);
        digest = mix(digest, (unsigned long long)g_s.capacity());
        if (check_stale() != 0) return 1;
        if (check_owner() != 0) return 1;
        cases++;
    }

    g_s.drop();
    g_s.drop();
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_strview_stale_digest", hex);
    print("f100_strview_stale:", cases, "cases ok");
    return 0;
}
