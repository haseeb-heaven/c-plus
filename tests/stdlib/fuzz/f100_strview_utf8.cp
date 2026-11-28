/* F-079 - deterministic fuzz config: UTF-8 validation on byte arrays.
 *
 * Generates bounded arbitrary / structured byte arrays with a fixed seed and
 * compares three independent validators:
 *   1. stdlib/string.hp::stdlib_utf8_validate(data, len)
 *   2. string_view(data, len).is_valid_utf8()
 *   3. a self-contained reference decoder (code-point accumulation, different
 *      control flow from the header's branch table)
 * Any disagreement, crash or sanitizer report is a real library defect.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

#define CASES 1200
#define MAXBYTES 12
#define NOPS 5

static unsigned long long rng_state = 0xF079C0DEULL;

static unsigned int rnd(void) {
    rng_state = rng_state * 6364136223846793005ULL +
                1442695040888963407ULL;
    return (unsigned int)(rng_state >> 33);
}

#define FNV_PRIME 1099511628211ULL

static unsigned long long mix(unsigned long long d, unsigned long long v) {
    return (d ^ v) * FNV_PRIME;
}

/* Independent strict UTF-8 oracle: decode the code point explicitly. */
static int ref_utf8(const unsigned char *s, size_t n) {
    size_t i = 0;
    while (i < n) {
        unsigned char c = s[i];
        unsigned long cp;
        int need;
        size_t k;
        if (c < 0x80u) { i++; continue; }
        if (c >= 0xC2u && c <= 0xDFu) { cp = (unsigned long)(c & 0x1Fu); need = 1; }
        else if (c >= 0xE0u && c <= 0xEFu) { cp = (unsigned long)(c & 0x0Fu); need = 2; }
        else if (c >= 0xF0u && c <= 0xF4u) { cp = (unsigned long)(c & 0x07u); need = 3; }
        else return 0;
        if (n - i - 1u < (size_t)need) return 0;
        for (k = 1; k <= (size_t)need; k++) {
            unsigned char cc = s[i + k];
            if (cc < 0x80u || cc > 0xBFu) return 0;
            cp = (cp << 6) | (unsigned long)(cc & 0x3Fu);
        }
        if (need == 1 && cp < 0x80u) return 0;
        if (need == 2 && cp < 0x800u) return 0;
        if (need == 3 && cp < 0x10000u) return 0;
        if (cp >= 0xD800u && cp <= 0xDFFFu) return 0;
        if (cp > 0x10FFFFu) return 0;
        i += (size_t)need + 1u;
    }
    return 1;
}

/* Fixed vectors: valid max, NUL, overlong, surrogate, out-of-range, truncated,
 * stray continuation, CRLF-as-ASCII. */
static const unsigned char FIXED[8][5] = {
    { 0xF4, 0x8F, 0xBF, 0xBF, 0 },
    { 0x61, 0x00, 0x62, 0, 0 },
    { 0xC0, 0x80, 0, 0, 0 },
    { 0xED, 0xA0, 0x80, 0, 0 },
    { 0xF4, 0x90, 0x80, 0x80, 0 },
    { 0xE2, 0x82, 0, 0, 0 },
    { 0x80, 0, 0, 0, 0 },
    { 0x0D, 0x0A, 0, 0, 0 }
};
static const size_t FIXED_LEN[8] = { 4, 3, 2, 3, 4, 2, 1, 2 };

int main(void) {
    unsigned long long digest = 1469598103934665603ULL;
    unsigned char buf[MAXBYTES];
    char hex[24];
    int i;
    int cases = 0;

    for (i = 0; i < CASES; i++) {
        unsigned int r0 = rnd();
        unsigned int r1 = rnd();
        unsigned int r2 = rnd();
        int mode = (int)(r0 % (unsigned int)NOPS);
        size_t len = 0;
        size_t k;
        int a, b, c;
        string_view view;

        digest = mix(digest, (unsigned long long)i);
        digest = mix(digest, (unsigned long long)mode);

        memset(buf, 0, MAXBYTES);
        switch (mode) {
        case 0: /* raw bytes, mostly invalid */
            len = (size_t)(r1 % (unsigned int)(MAXBYTES + 1u));
            for (k = 0; k < len; k++)
                buf[k] = (unsigned char)((r2 >> (4u * (k % 4u))) & 0xFFu);
            break;

        case 1: /* valid encoding of a bounded code point */
            {
                unsigned long cp = (unsigned long)(r1 % 0x110000u);
                if (cp >= 0xD800u && cp <= 0xDFFFu)
                    cp = 0x41u;
                if (cp < 0x80u) {
                    buf[0] = (unsigned char)cp; len = 1;
                } else if (cp < 0x800u) {
                    buf[0] = (unsigned char)(0xC0u | (cp >> 6));
                    buf[1] = (unsigned char)(0x80u | (cp & 0x3Fu)); len = 2;
                } else if (cp < 0x10000u) {
                    buf[0] = (unsigned char)(0xE0u | (cp >> 12));
                    buf[1] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
                    buf[2] = (unsigned char)(0x80u | (cp & 0x3Fu)); len = 3;
                } else {
                    buf[0] = (unsigned char)(0xF0u | (cp >> 18));
                    buf[1] = (unsigned char)(0x80u | ((cp >> 12) & 0x3Fu));
                    buf[2] = (unsigned char)(0x80u | ((cp >> 6) & 0x3Fu));
                    buf[3] = (unsigned char)(0x80u | (cp & 0x3Fu)); len = 4;
                }
            }
            break;

        case 2: /* a valid ASCII/UTF-8 prefix with one injected raw byte */
            len = 1 + (size_t)(r1 % 6u);
            for (k = 0; k < len; k++)
                buf[k] = (unsigned char)('a' + (unsigned char)(k % 7u));
            buf[r2 % len] = (unsigned char)(0x80u + (r2 % 0x80u));
            break;

        case 3: /* ASCII (embedded NUL included): always well formed */
            len = (size_t)(r1 % (unsigned int)(MAXBYTES + 1u));
            for (k = 0; k < len; k++)
                buf[k] = (unsigned char)((r2 >> (4u * (k % 4u))) & 0x7Fu);
            break;

        default: /* fixed tricky vectors, cyclically */
            {
                size_t vi = (size_t)(r1 % 8u);
                len = FIXED_LEN[vi];
                for (k = 0; k < len; k++)
                    buf[k] = FIXED[vi][k];
            }
            break;
        }

        a = stdlib_utf8_validate((const char *)buf, len);
        view = string_view((const char *)buf, len);
        b = view.is_valid_utf8();
        c = ref_utf8(buf, len);

        if (a != b || a != c) {
            print("F-079 INVARIANT utf8 case", i, "mode", mode,
                  "len", (int)len, "a", a, "b", b, "c", c);
            return 1;
        }
        if (view.size() != len || view.data() != (const char *)buf) {
            print("F-079 INVARIANT view", i);
            return 1;
        }

        digest = mix(digest, (unsigned long long)len);
        for (k = 0; k < len; k++)
            digest = mix(digest, (unsigned long long)buf[k]);
        digest = mix(digest, (unsigned long long)a);
        cases++;
    }

    snprintf(hex, sizeof(hex), "%016llx", digest);
    print("f100_strview_utf8_digest", hex);
    print("f100_strview_utf8:", cases, "cases ok");
    return 0;
}
