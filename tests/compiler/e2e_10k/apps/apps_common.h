/* Deterministic core routines for the app/game pattern scenarios.
 * All state is local and seeded, so repeated runs produce identical bytes. */
#ifndef E2E10K_APPS_COMMON_H
#define E2E10K_APPS_COMMON_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "safe_c.h"
#include "safe_string.h"
#include "safe_memory.h"

static unsigned int app_rnd(unsigned int *s) {
    unsigned int x = *s;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    *s = x;
    return x;
}
static int app_pick(unsigned int *s, int n) {
    if (n <= 0) return 0;
    return (int)(app_rnd(s) % (unsigned)n);
}

static void app_hex(unsigned long long v, char hexout[static 17]) {
    static const char h[] = "0123456789abcdef";
    int i;
    for (i = 15; i >= 0; --i) { hexout[15 - i] = h[(v >> (i * 4)) & 15ULL]; }
    hexout[16] = 0;
}
static unsigned long long app_hash(const void *p, size_t n) {
    const unsigned char *b = (const unsigned char *)p;
    unsigned long long h = 1469598103934665603ULL;
    size_t i;
    for (i = 0; i < n; ++i) { h ^= b[i]; h *= 1099511628211ULL; }
    return h;
}
static void app_boardstr(const char *b, int len, char *out) {
    int i;
    for (i = 0; i < len; ++i) out[i] = b[i] ? b[i] : '.';
    out[len] = 0;
}
static int app_winner(const char *b) {
    static const int w[8][3] = {{0,1,2},{3,4,5},{6,7,8},{0,3,6},{1,4,7},
                               {2,5,8},{0,4,8},{2,4,6}};
    int i;
    for (i = 0; i < 8; ++i)
        if (b[w[i][0]] && b[w[i][0]] == b[w[i][1]] &&
            b[w[i][0]] == b[w[i][2]]) return b[w[i][0]] == 'X' ? 1 : 2;
    return 0;
}
static int app_free_cell(const char *b, unsigned int *s) {
    int i, k;
    for (k = 0; k < 64; ++k) { i = app_pick(s, 9); if (!b[i]) return i; }
    for (i = 0; i < 9; ++i) if (!b[i]) return i;
    return -1;
}
static void app_tictactoe(unsigned int seed, char *line, int *winner, int *moves) {
    char b[9];
    unsigned int s = seed + 1u;
    int i, p = 0, w = 0, m = 0;
    (void)memset_s(b, sizeof(b), 0, sizeof(b));
    for (i = 0; i < 9; ++i) {
        int cell = app_free_cell(b, &s);
        if (cell < 0) break;
        b[cell] = p ? 'O' : 'X';
        ++m;
        w = app_winner(b);
        if (w) break;
        p ^= 1;
    }
    app_boardstr(b, 9, line);
    *winner = w; *moves = m;
}

static void app_merge_row(int *r, int n, int *score) {
    int out[4], k = 0, i;
    (void)memset_s(out, sizeof(out), 0, sizeof(out));
    for (i = 0; i < n; ++i) {
        if (!r[i]) continue;
        if (k > 0 && out[k - 1] == r[i]) {
            out[k - 1] *= 2; *score += out[k - 1];
        } else out[k++] = r[i];
    }
    for (i = 0; i < n; ++i) r[i] = out[i];
}
static void app_move2048(int g[4][4], int dir, int *score) {
    int i, j;
    if (dir == 0 || dir == 1) {
        for (i = 0; i < 4; ++i) {
            int r[4];
            for (j = 0; j < 4; ++j) r[j] = g[i][dir ? 3 - j : j];
            app_merge_row(r, 4, score);
            for (j = 0; j < 4; ++j) g[i][dir ? 3 - j : j] = r[j];
        }
    } else {
        for (j = 0; j < 4; ++j) {
            int r[4];
            for (i = 0; i < 4; ++i) r[i] = g[dir == 2 ? i : 3 - i][j];
            app_merge_row(r, 4, score);
            for (i = 0; i < 4; ++i) g[dir == 2 ? i : 3 - i][j] = r[i];
        }
    }
}
static int app_play2048(unsigned int seed, int nmoves, int *score) {
    unsigned int s = seed + 7u;
    int g[4][4], i, sc = 0;
    (void)memset_s(g, sizeof(g), 0, sizeof(g));
    for (i = 0; i < 16; ++i) g[i / 4][i % 4] = (app_pick(&s, 4) == 0) ? 2 : 0;
    for (i = 0; i < nmoves; ++i) {
        app_move2048(g, app_pick(&s, 4), &sc);
        g[app_pick(&s, 4)][app_pick(&s, 4)] = 2;
    }
    *score = sc;
    return (int)(app_hash(g, sizeof(g)) & 0x7fffffffL);
}
static void app_snake(unsigned int seed, int nsteps, int pos[static 2], int *score) {
    unsigned int s = seed + 11u;
    int x = 4, y = 4, dx = 1, dy = 0, sc = 0, i;
    for (i = 0; i < nsteps; ++i) {
        int k = app_pick(&s, 8);
        if (k < 2) { int t = dx; dx = -dy; dy = t; }
        else if (k < 4) { int t = dx; dx = dy; dy = -t; }
        x = (x + dx + 8) % 8; y = (y + dy + 8) % 8;
        if ((x + y) % 7 == 0) ++sc;
    }
    pos[0] = x; pos[1] = y; *score = sc;
}
static int app_calc(unsigned int seed, int *a, int *b, int *op) {
    unsigned int s = seed + 3u;
    char expr[64], fmt[16];
    int x, y, k, r = 0;
    *a = x = app_pick(&s, 90) + 10;
    *b = y = app_pick(&s, 40) + 3;
    *op = k = app_pick(&s, 4);
    (void)snprintf_s(fmt, sizeof(fmt), "%%d%c%%d", "+-*/"[k]);
    (void)snprintf_s(expr, sizeof(expr), fmt, x, y);
    switch (k) {
        case 0: r = x + y; break;
        case 1: r = x - y; break;
        case 2: r = x * y; break;
        case 3: if (y == 0) r = -1; else r = x / y; break;
        default: break;
    }
    (void)expr;
    return r;
}
static void app_line_edit(unsigned int seed, char *out, size_t cap) {
    unsigned int s = seed + 5u;
    char b[32];
    int i;
    (void)memset_s(b, sizeof(b), 0, sizeof(b));
    for (i = 0; i < 8; ++i) {
        int k = app_pick(&s, 4);
        if (k == 0 && b[0]) {
            size_t n = strlen(b);
            b[n - 1] = 0;
        } else if (k == 1) {
            (void)memset_s(b, sizeof(b), 0, sizeof(b));
        } else {
            char c = (char)('a' + app_pick(&s, 26));
            char one[2];
            one[0] = c; one[1] = 0;
            if (strlen(b) + 1 < sizeof(b))
                (void)strcat_s(b, sizeof(b), one);
        }
    }
    (void)strncpy_s(out, cap, b, strlen(b));
}
static unsigned long long app_hash_case(unsigned int seed, char *text, size_t cap) {
    unsigned int s = seed + 13u;
    int k = app_pick(&s, 5);
    if (k == 0) (void)snprintf_s(text, cap, "%d", app_pick(&s, 100000));
    else if (k == 1) (void)snprintf_s(text, cap, "%x", app_rnd(&s));
    else if (k == 2) (void)snprintf_s(text, cap, "%s-%d", "key", app_pick(&s, 999));
    else if (k == 3) (void)snprintf_s(text, cap, "%c%c%c", 'a' + app_pick(&s, 26),
                                     'A' + app_pick(&s, 26), '0' + app_pick(&s, 10));
    else (void)snprintf_s(text, cap, "%.2f", (double)(app_pick(&s, 1000)) / 7.0);
    return app_hash(text, strlen(text));
}
static int app_json_fields(unsigned int seed, int *num, char *word, size_t cap) {
    static const char *docs[] = {
        "{\"a\":12,\"b\":\"red\"}", "{\"a\":-7,\"b\":\"blue\"}",
        "{\"a\":305,\"b\":\"green\"}", "{\"a\":0,\"b\":\"x\"}",
        "{\"a\":88,\"b\":\"longer\"}", "{\"a\":42,\"b\":\"q\"}"
    };
    const char *d = docs[app_pick(&seed, 6)];
    int r;
    (void)memset_s(word, cap, 0, cap);
    r = sscanf_s(d, "{\"a\":%d,\"b\":\"%31s\"}", num, word);
    return r;
}
#endif /* E2E10K_APPS_COMMON_H */
