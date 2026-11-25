/* sort_tool.cp -- deterministic stable sorter with explicit refusal gates.
 *
 * NOT an interactive tool.  All input is fixed argv.  No clock, no network,
 * no user paths, no shelling out.  Identical argv yields byte-identical stdout.
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/data_2/sort_tool.cp \
 *       --strict --sanitize=none -I. -o examples/expanded/data_2/.run/sort_tool
 *
 * Usage:
 *   sort_tool int asc  <n1> <n2> ...
 *   sort_tool int desc <n1> <n2> ...
 *   sort_tool str asc  <s1> <s2> ...
 *   sort_tool str desc <s1> <s2> ...
 *
 * Ordering is STABLE: equal keys keep their original input index, which is
 * printed as `src`.  Oversized lists, oversized tokens, non-integers, integer
 * overflow, empty tokens and non-printable bytes are REFUSED (exit 2).
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define ST_MAX_ITEMS 24
#define ST_MAX_TOKEN 24
#define ST_MAX_INT_DIGITS 7
#define ST_INT_LIMIT 1000000

static const char *st_arg(char **argv, int i) {
    const char *v = NULL;
    if (i <= 0)
        return NULL;
    __builtin_memcpy(&v, argv + i, sizeof(v));
    return v;
}

/* Uniform deterministic refusal: one stdout line carrying the stdlib error
 * code, exit 2, empty stderr. */
static int st_refuse(const char *app, const char *reason, cplus_exit_code code) {
    error e = error();
    e.set(code, reason);
    printf("%s error code=%d reason=%s\n", app, e.code(), reason);
    return 2;
}

static int st_printable(const char *text) {
    string s = string(text);
    size_t i;
    size_t n = s.size();
    if (n == 0)
        return 0;
    for (i = 0; i < n; i++) {
        unsigned char c = (unsigned char)s.at(i);
        if (c < 0x21 || c > 0x7e)
            return 0;
    }
    return 1;
}

/* -1 invalid character, -2 magnitude overflow, 0 on success. */
static int st_parse_int(const char *arg, int *out) {
    string s = string(arg);
    size_t n = s.size();
    size_t i = 0;
    int neg = 0;
    long val = 0;
    if (n == 0 || n > ST_MAX_INT_DIGITS + 1)
        return -2;
    if (s.at(0) == '-') {
        neg = 1;
        i = 1;
    }
    if (i >= n)
        return -1;
    for (; i < n; i++) {
        char c = s.at(i);
        if (c < '0' || c > '9')
            return -1;
        val = val * 10 + (long)(c - '0');
        if (val > ST_INT_LIMIT)
            return -2;
    }
    *out = neg ? (int)(-val) : (int)val;
    return 0;
}

/* Strict "a must come before b" under the selected order.  Equal keys return
 * 0 in BOTH directions, which is what makes the insertion sort stable. */
static int st_less(int mode_int, int desc, const int *values,
                   char texts[ST_MAX_ITEMS][ST_MAX_TOKEN], int a, int b) {
    int c = 0;
    if (mode_int) {
        if (values[a] < values[b])
            c = -1;
        else if (values[a] > values[b])
            c = 1;
    } else {
        int raw = strcmp(texts[a], texts[b]);
        if (raw < 0)
            c = -1;
        else if (raw > 0)
            c = 1;
    }
    return desc ? (c > 0) : (c < 0);
}

int main(int argc, char **argv) {
    const char *mode;
    const char *order;
    int mode_int = 0;
    int desc = 0;
    int n;
    int i;
    int cmp_count = 0;
    int sorted = 1;
    int stable = 1;
    int fp = 0;
    int values[ST_MAX_ITEMS];
    char texts[ST_MAX_ITEMS][ST_MAX_TOKEN];
    int perm[ST_MAX_ITEMS];
    vector src = vector();

    if (argc < 4)
        return st_refuse("SORT", "usage", CPLUS_EXIT_UNSUPPORTED);

    mode = st_arg(argv, 1);
    order = st_arg(argv, 2);
    if (mode == NULL || order == NULL)
        return st_refuse("SORT", "null-arg", CPLUS_EXIT_NULL_POINTER);

    if (strcmp(mode, "int") == 0)
        mode_int = 1;
    else if (strcmp(mode, "str") != 0)
        return st_refuse("SORT", "bad-mode", CPLUS_EXIT_UNSUPPORTED);

    if (strcmp(order, "asc") == 0)
        desc = 0;
    else if (strcmp(order, "desc") == 0)
        desc = 1;
    else
        return st_refuse("SORT", "bad-order", CPLUS_EXIT_UNSUPPORTED);

    n = argc - 3;
    if (n < 1)
        return st_refuse("SORT", "empty-list", CPLUS_EXIT_UNSUPPORTED);
    if (n > ST_MAX_ITEMS)
        return st_refuse("SORT", "too-many-items", CPLUS_EXIT_BOUNDS);

    for (i = 0; i < n; i++) {
        const char *arg = st_arg(argv, i + 3);
        if (arg == NULL)
            return st_refuse("SORT", "null-item", CPLUS_EXIT_NULL_POINTER);
        if (mode_int) {
            int v = 0;
            int r = st_parse_int(arg, &v);
            if (r == -1)
                return st_refuse("SORT", "invalid-integer", CPLUS_EXIT_UNSUPPORTED);
            if (r == -2)
                return st_refuse("SORT", "integer-overflow", CPLUS_EXIT_BOUNDS);
            values[i] = v;
        } else {
            string tok = string(arg);
            size_t len = tok.size();
            size_t k;
            if (len == 0)
                return st_refuse("SORT", "empty-token", CPLUS_EXIT_UNSUPPORTED);
            if (len >= ST_MAX_TOKEN)
                return st_refuse("SORT", "token-too-long", CPLUS_EXIT_BOUNDS);
            for (k = 0; k < len; k++)
                texts[i][k] = tok.at(k);
            texts[i][len] = '\0';
            if (!st_printable(texts[i]))
                return st_refuse("SORT", "invalid-string", CPLUS_EXIT_UNSUPPORTED);
        }
        if (src.push_back(i) != 0)
            return st_refuse("SORT", "alloc-failed", CPLUS_EXIT_RESOURCE);
    }

    for (i = 0; i < n; i++)
        perm[i] = i;

    /* Stable insertion sort over the permutation, never over the payload. */
    for (i = 1; i < n; i++) {
        int p = perm[i];
        int j = i;
        while (j > 0) {
            cmp_count++;
            if (!st_less(mode_int, desc, values, texts, p, perm[j - 1]))
                break;
            perm[j] = perm[j - 1];
            j--;
        }
        perm[j] = p;
    }

    for (i = 1; i < n; i++) {
        int prev = perm[i - 1];
        int cur = perm[i];
        if (st_less(mode_int, desc, values, texts, cur, prev))
            sorted = 0;
        if (!st_less(mode_int, desc, values, texts, cur, prev) &&
            !st_less(mode_int, desc, values, texts, prev, cur)) {
            if (src.at((size_t)prev) > src.at((size_t)cur))
                stable = 0;
        }
    }

    printf("SORT app=sort_tool mode=%s order=%s n=%d max_items=%d max_token=%d\n",
           mode_int ? "int" : "str", desc ? "desc" : "asc", n,
           ST_MAX_ITEMS, ST_MAX_TOKEN);
    if (mode_int) {
        for (i = 0; i < n; i++)
            printf("SORT in %d=%d\n", i, values[i]);
        for (i = 0; i < n; i++) {
            int at = perm[i];
            printf("SORT out %d=%d src=%d\n", i, values[at], src.at((size_t)at));
            fp += values[at] * (i + 1);
        }
    } else {
        for (i = 0; i < n; i++)
            printf("SORT in %d=%s\n", i, texts[i]);
        for (i = 0; i < n; i++) {
            int at = perm[i];
            printf("SORT out %d=%s src=%d\n", i, texts[at], src.at((size_t)at));
            fp += (int)(unsigned char)texts[at][0] * (i + 1);
        }
    }
    printf("SORT check sorted=%d stable=%d cmp=%d\n", sorted, stable, cmp_count);
    printf("SORT fp=%d\n", fp);
    printf("SORT done\n");

    src.drop();
    return 0;
}
