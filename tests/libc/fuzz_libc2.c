/* Deterministic randomized harness (round 2) for additional C+ libc slice
 * entry points: strndup, strtok_r, strsep, memmem, strverscmp, strcasestr,
 * strchrnul, div/ldiv/llabs/atoll, wcslen/wcscpy/wcscmp, isascii/toascii.
 *
 * Uses a fixed seed, in-bounds / defined inputs only, and asserts only
 * invariants (no crash, bounded results, comparator/reflexivity laws) rather
 * than exact host-vs-slice equality for unspecified behaviour. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <wchar.h>
#include "cplus_libc.h"

/* Deterministic linear-congruential RNG (same generator as fuzz_libc.c). */
static unsigned st = 0xC0FFEEu;
static unsigned R(void) { st = st * 1664525u + 1013904223u; return st; }

int main(void) {
    for (int it = 0; it < 5000; it++) {
        int j, k;

        /* ---------- cplus_strndup ---------- */
        char s[48];
        int slen = (int)(R() % 47u);
        for (j = 0; j < slen; j++) s[j] = (char)('a' + (int)(R() % 26u));
        s[slen] = '\0';
        size_t cap = (size_t)(R() % 64u);
        char *dup = cplus_strndup(s, cap);
        assert(dup != NULL);
        size_t want = ((size_t)slen < cap) ? (size_t)slen : cap;
        assert(strlen(dup) == want);
        assert(strncmp(dup, s, want) == 0);
        assert(dup[want] == '\0');
        free(dup);
        char *dup0 = cplus_strndup(s, 0);      /* n == 0 -> empty copy */
        assert(dup0 != NULL && dup0[0] == '\0');
        free(dup0);

        /* ---------- build a comma-delimited token buffer ---------- */
        char buf[64];
        int fields = 1 + (int)(R() % 8u);
        k = 0;
        for (j = 0; j < fields; j++) {
            int m;
            int fl = 1 + (int)(R() % 5u);      /* non-empty fields only */
            for (m = 0; m < fl; m++) buf[k++] = (char)('a' + (int)(R() % 26u));
            if (j + 1 < fields) buf[k++] = ',';
        }
        buf[k] = '\0';

        /* ---------- cplus_strtok_r ---------- */
        char tk[64];
        strcpy(tk, buf);
        char *save = NULL;
        char *tok = cplus_strtok_r(tk, ",", &save);
        int ntok = 0;
        while (tok != NULL) {
            assert(strchr(tok, ',') == NULL);  /* delimiters consumed */
            ntok++;
            assert(ntok <= 64);                /* bounded token count */
            tok = cplus_strtok_r(NULL, ",", &save);
        }
        assert(ntok >= 1 && ntok <= fields);

        /* ---------- cplus_strsep ---------- */
        char sb[64];
        strcpy(sb, buf);
        char *cursor = sb;
        char *sep_tok;
        int nsep = 0;
        while ((sep_tok = cplus_strsep(&cursor, ",")) != NULL) {
            assert(strchr(sep_tok, ',') == NULL);
            nsep++;
            assert(nsep <= 64);
        }
        assert(nsep >= 1);

        /* ---------- cplus_memmem ---------- */
        unsigned char hay[64], needle[8];
        int hlen = 8 + (int)(R() % 40u);
        int nlen = 1 + (int)(R() % 5u);
        for (j = 0; j < hlen; j++) hay[j] = (unsigned char)('A' + (int)(R() % 26u));
        for (j = 0; j < nlen; j++) needle[j] = (unsigned char)('A' + (int)(R() % 26u));
        int pos = (int)(R() % (unsigned)(hlen - nlen + 1));
        for (j = 0; j < nlen; j++) hay[pos + j] = needle[j];
        void *hit = cplus_memmem(hay, (size_t)hlen, needle, (size_t)nlen);
        assert(hit != NULL);
        assert(memcmp(hit, needle, (size_t)nlen) == 0);
        assert((unsigned char *)hit >= hay);
        assert((unsigned char *)hit + nlen <= hay + hlen);
        assert(cplus_memmem(hay, (size_t)hlen, needle, 0) == (void *)hay);

        /* ---------- cplus_strcasestr ---------- */
        char hay2[64], nee2[8];
        int h2len = 1 + (int)(R() % 50u);
        int n2len = 1 + (int)(R() % 5u);
        for (j = 0; j < h2len; j++) hay2[j] = (char)('a' + (int)(R() % 26u));
        hay2[h2len] = '\0';
        for (j = 0; j < n2len; j++) nee2[j] = (char)('a' + (int)(R() % 26u));
        nee2[n2len] = '\0';
        char *cs = cplus_strcasestr(hay2, nee2);
        if (cs != NULL) assert(cplus_strncasecmp(cs, nee2, (size_t)n2len) == 0);
        assert(cplus_strcasestr(hay2, "") == hay2);   /* empty needle -> hay */

        /* ---------- cplus_strchrnul ---------- */
        char hn[40];
        int hnlen = (int)(R() % 38u);
        for (j = 0; j < hnlen; j++) hn[j] = (char)('a' + (int)(R() % 26u));
        hn[hnlen] = '\0';
        int ch = (R() % 4u == 0u) ? 0 : (int)('a' + (int)(R() % 26u));
        char *cp = cplus_strchrnul(hn, ch);
        assert(cp >= hn && cp <= hn + strlen(hn));
        if (ch == 0) assert(*cp == '\0');
        else assert(*cp == (char)ch || *cp == '\0');

        /* ---------- cplus_strverscmp: reflexivity + antisymmetry ---------- */
        char v1[24], v2[24];
        int l1 = (int)(R() % 20u);
        int l2 = (int)(R() % 20u);
        for (j = 0; j < l1; j++) v1[j] = "0123456789abc"[R() % 13u];
        v1[l1] = '\0';
        for (j = 0; j < l2; j++) v2[j] = "0123456789abc"[R() % 13u];
        v2[l2] = '\0';
        assert(cplus_strverscmp(v1, v1) == 0);
        {
            int ab = cplus_strverscmp(v1, v2);
            int ba = cplus_strverscmp(v2, v1);
            assert((ab == 0 && ba == 0) || (ab > 0 && ba < 0) || (ab < 0 && ba > 0));
        }

        /* ---------- cplus_div / cplus_ldiv ---------- */
        int num = (int)(R() % 2001u) - 1000;       /* bounded: no overflow */
        int den = 1 + (int)(R() % 999u);           /* nonzero */
        if (R() & 1u) den = -den;
        div_t dq = cplus_div(num, den);
        assert(dq.quot == num / den);
        assert(dq.rem == num % den);

        long lnum = (long)((int)(R() % 2001u) - 1000);
        long lden = (long)(1 + (int)(R() % 999u));
        if (R() & 1u) lden = -lden;
        ldiv_t lq = cplus_ldiv(lnum, lden);
        assert(lq.quot == lnum / lden);
        assert(lq.rem == lnum % lden);

        /* ---------- cplus_llabs / cplus_atoll ---------- */
        long long lv = (long long)((int)(R() % 200001u) - 100000);
        assert(cplus_llabs(lv) >= 0);
        assert(cplus_llabs(lv) == (lv < 0 ? -lv : lv));
        char ns[24];
        long long av = (long long)((int)(R() % 2000001u) - 1000000);
        snprintf(ns, sizeof ns, "%lld", av);
        assert(cplus_atoll(ns) == av);

        /* ---------- cplus_wcslen / cplus_wcscpy / cplus_wcscmp ---------- */
        wchar_t wa[16], wb[16];
        int wl = (int)(R() % 14u);
        for (j = 0; j < wl; j++) wa[j] = (wchar_t)(L'a' + (int)(R() % 26u));
        wa[wl] = L'\0';
        assert(cplus_wcslen(wa) == (size_t)wl);
        assert(cplus_wcscpy(wb, wa) == wb);
        assert(cplus_wcscmp(wb, wa) == 0);
        assert(cplus_wcscmp(wa, wa) == 0);
        assert(cplus_wcslen(wb) == cplus_wcslen(wa));

        /* ---------- cplus_isascii / cplus_toascii ---------- */
        int ci = (int)(R() % 512u) - 128;          /* -128 .. 383 */
        int isa = cplus_isascii(ci);
        assert(isa == 0 || isa == 1);
        assert((isa != 0) == (ci >= 0 && ci < 128));
        int ta = cplus_toascii(ci);
        assert(ta >= 0 && ta <= 127);
        assert(ta == (ci & 0x7f));
    }
    puts("fuzz_libc2: ok");
    return 0;
}
