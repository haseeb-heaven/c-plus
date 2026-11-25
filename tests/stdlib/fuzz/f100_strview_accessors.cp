/* F-078 - deterministic fuzz config: string accessors against a shadow.
 *
 * Bounded content is generated from an alphabet that includes the ',' field
 * separator.  at/substr/count_fields/field/find/equals/compare and the borrowed
 * view are checked against an independent fixed-size byte shadow after each
 * operation.  A mismatch or crash is a library defect.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define CASES 1200
#define MAXSTR 48
#define OUTSZ 8
#define NOPS 7

static const char ALPHA[] = "abcde,,.,-0123";
static const char *NEEDLES[3] = { "a", ",", "xyz" };

static unsigned long long rng_state = 0xF078C0DEULL;

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

static int fail_here(const char *label) {
    print("F-078 INVARIANT", label);
    return 1;
}

static int check_base(void) {
    string_view tv;
    if (!g_s.valid() || g_s.size() != sn || g_s.capacity() < sn ||
        strlen(g_s.c_str()) != sn)
        return fail_here("extent");
    if (sn > 0 && memcmp(g_s.c_str(), sh, sn) != 0)
        return fail_here("contents");
    if (!g_s.is_valid_utf8())
        return fail_here("utf8");
    tv = g_s.to_view();
    if (tv.size() != sn)
        return fail_here("view.size");
    if (!tv.equals(sh))
        return fail_here("view.equals");
    return 0;
}

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

/* Independent field lookup on the shadow. Returns 0 on success. */
static int shadow_field(size_t index, char *out, size_t out_size) {
    size_t current = 0, start = 0, i;
    for (i = 0; i <= sn; i++) {
        if (i == sn || sh[i] == ',') {
            if (current == index) {
                size_t n = i - start;
                size_t k;
                if (n + 1 > out_size)
                    n = out_size - 1;
                for (k = 0; k < n; k++)
                    out[k] = sh[start + k];
                out[n] = '\0';
                return 0;
            }
            current++;
            start = i + 1;
        }
    }
    return -1;
}

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    char piece[8];
    char out[OUTSZ];
    char expected[OUTSZ];
    char hex[24];
    int i;
    int cases = 0;

    if (g_s.append("a,b,c") != 0)
        return fail_here("init");
    sn = 5; memcpy(sh, "a,b,c", 5); sh[sn] = '\0';
    if (check_base() != 0)
        return 1;

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int kind = (int)(r0 % (unsigned int)NOPS);
        int rc;
        size_t at_idx, start, nsub, exp_n, k;
        size_t fields = 1;
        const char *hit;
        long pos;

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)kind);

        switch (kind) {
        case 0: /* bounded append of separator-bearing content */
            {
                size_t plen = make_piece(r1, piece, sizeof(piece));
                if (sn + plen > MAXSTR) { g_s.clear(); sn = 0; sh[0] = '\0'; }
                if (g_s.append(piece) != 0) return fail_here("append");
                memcpy(sh + sn, piece, plen); sn += plen; sh[sn] = '\0';
                digest = mix(digest, (unsigned long long)plen);
            }
            break;

        case 1: /* clear + append */
            g_s.clear(); sn = 0; sh[0] = '\0';
            {
                size_t plen = make_piece(r2, piece, sizeof(piece));
                if (g_s.append(piece) != 0) return fail_here("clear.append");
                memcpy(sh, piece, plen); sn = plen; sh[sn] = '\0';
                digest = mix(digest, (unsigned long long)plen);
            }
            break;

        case 2: /* at(): in-range byte and documented at(size()) == '\0' */
            at_idx = (size_t)r1 % (sn + 1u);
            rc = (unsigned char)g_s.at(at_idx);
            {
                int want = at_idx < sn ? (unsigned char)sh[at_idx] : 0;
                if (rc != want) return fail_here("at");
                digest = mix(digest, (unsigned long long)rc);
            }
            break;

        case 3: /* bounded substr with truncation */
            start = (size_t)r1 % (sn + 1u);
            nsub = (size_t)(r2 % (unsigned int)(OUTSZ + 2u));
            memset(out, 'Z', OUTSZ);
            if (g_s.substr(start, nsub, out, sizeof(out)) != 0)
                return fail_here("substr.rc");
            exp_n = sn - start;
            if (exp_n > nsub) exp_n = nsub;
            if (exp_n + 1u > OUTSZ) exp_n = OUTSZ - 1u;
            for (k = 0; k < exp_n; k++)
                if (out[k] != sh[start + k]) return fail_here("substr.bytes");
            if (out[exp_n] != '\0') return fail_here("substr.term");
            digest = mix(digest, (unsigned long long)exp_n);
            break;

        case 4: /* count_fields + field, including a missing field index */
            for (k = 0; k < sn; k++)
                if (sh[k] == ',') fields++;
            if (g_s.count_fields(',') != fields)
                return fail_here("count_fields");
            {
                size_t idx = (size_t)r2 % (fields + 2u);
                memset(out, 'Z', OUTSZ);
                rc = g_s.field(',', idx, out, sizeof(out));
                if (idx >= fields) {
                    if (rc != -1) return fail_here("field.absent.rc");
                } else {
                    if (rc != 0) return fail_here("field.rc");
                    if (shadow_field(idx, expected, sizeof(expected)) != 0)
                        return fail_here("field.shadow");
                    if (strcmp(out, expected) != 0)
                        return fail_here("field.bytes");
                }
                digest = mix(digest, (unsigned long long)(rc + 2));
            }
            break;

        case 5: /* find(): covered and uncovered needles */
            {
                size_t ni = (size_t)r2 % 3u;
                hit = strstr(sh, NEEDLES[ni]);
                pos = hit == NULL ? -1L : (long)(hit - sh);
                if (g_s.find(NEEDLES[ni]) != pos)
                    return fail_here("find");
                digest = mix(digest, (unsigned long long)(pos + 2L));
            }
            break;

        default: /* equals / compare against the shadow and a mutation */
            if (!g_s.equals(sh)) return fail_here("equals.same");
            if (g_s.compare(sh) != 0) return fail_here("compare.same");
            piece[0] = sn > 0 && sh[0] != 'z' ? 'z' : 'y';
            piece[1] = '\0';
            if (sn == 1 && sh[0] == piece[0]) piece[0] = 'q';
            if (g_s.equals(piece) == 1) return fail_here("equals.diff");
            digest = mix(digest, (unsigned long long)sn);
            break;
        }

        digest = mix(digest, (unsigned long long)sn);
        digest = mix(digest, (unsigned long long)g_s.capacity());
        if (check_base() != 0)
            return 1;
        cases++;
    }

    g_s.drop(); g_s.drop();
    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_strview_accessors_digest", hex);
    print("f100_strview_accessors:", cases, "cases ok");
    return 0;
}
