/* Native C+ replacement for tests/stdlib/coverage.py.
 *
 * This is the SAME static API-call-site heuristic as the Python tool, ported to
 * C+ so the report can be produced without a Python interpreter.  It is NOT
 * execution coverage and NOT a safety proof: see docs/stdlib/NATIVE_COVERAGE_TOOL.md.
 *
 * Method (mirrors coverage.py exactly):
 *   1. Enumerate the .hp headers under stdlib/ with shell `find` (output
 *      redirected to a temp file by cplus_system()).
 *   2. Parse public callables from each header with simple line scanning that
 *      emulates the Python SIG / CTOR / CLASS regexes.
 *   3. Enumerate the .cp files directly under tests/stdlib/, under
 *      tests/stdlib/multi_tu/ and under tests/stdlib/examples/ the same way,
 *      and concatenate their text.
 *   4. For each public entry, look for a name('(') token in that text.  A
 *      destructor `drop` is credited when its owning class name appears as a
 *      word; `*_drop` is credited when its owner name appears as a word.
 *   5. Print a per-library covered/total/percent table and fail if any
 *      library is below 80.0%.
 *
 * Build and run (from the repository root):
 *   ./build/c+ build --strict --sanitize=none tests/stdlib/coverage.cp \
 *       -o build/stdlib-tests/coverage_native
 *   build/stdlib-tests/coverage_native
 *
 * cplus_system() in the hardened libc is a thin namespaced wrapper around
 * POSIX system().  We provide the identical one-line wrapper locally so this
 * single-file tool links with the default C+ runtime and needs no extra -l
 * flags; see the documentation for the swap-in alternative.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define THRESHOLD 80.0

#define MAX_PATHS 2048
#define MAX_PATH_LEN 768
#define MAX_NAME 160
#define MAX_ENTRIES 8192
#define MAX_SRC_BUF (16u * 1024u * 1024u)
#define MAX_HDR_BUF (4u * 1024u * 1024u)
#define MAX_FAILS 128
#define MAX_MISSING 16384

static int cplus_system(const char *cmd) { return system(cmd); }

static char g_headers[MAX_PATHS][MAX_PATH_LEN];
static int g_nheaders = 0;
static char g_sources[MAX_PATHS][MAX_PATH_LEN];
static int g_nsources = 0;

static char g_src[MAX_SRC_BUF];
static char g_hdr[MAX_HDR_BUF];
static size_t g_src_bytes = 0;

typedef struct {
    char name[MAX_NAME];
    char cls[MAX_NAME];
} Entry;
static Entry g_entries[MAX_ENTRIES];
static int g_nentries = 0;

typedef struct {
    char name[MAX_NAME];
    double pct;
    char missing[MAX_MISSING];
} Failure;
static Failure g_fails[MAX_FAILS];
static int g_nfails = 0;

/* ---- small character helpers (Python \w / \s for ASCII inputs) ----------- */

static int is_ws(char c) {
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}
static int is_wordc(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') || c == '_';
}
static int is_alpha_(char c) {
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '_';
}
static const char *ltrim(const char *p) {
    while (is_ws(*p)) p++;
    return p;
}
static size_t rtrim_len(const char *p, size_t n) {
    while (n > 0 && is_ws(p[n - 1])) n--;
    return n;
}
static void copy_span(char *dst, size_t cap, const char *p, size_t n) {
    if (n >= cap) n = cap - 1;
    memcpy(dst, p, n);
    dst[n] = '\0';
}
static int path_cmp(const void *a, const void *b) {
    return strcmp((const char *)a, (const char *)b);
}

/* ---- shell enumeration: `find ... | LC_ALL=C sort` into a temp file ------ */

static int read_list(const char *find_cmd, char list[][MAX_PATH_LEN], int *count,
                     const char *tmp) {
    char cmd[2048];
    char line[MAX_PATH_LEN];
    FILE *f;
    int rc;

    snprintf(cmd, sizeof(cmd), "%s > \"%s\"", find_cmd, tmp);
    rc = cplus_system(cmd);
    if (rc != 0) return -1;
    f = fopen(tmp, "r");
    if (!f) return -1;
    while (fgets(line, sizeof(line), f)) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) line[--n] = '\0';
        if (n == 0) continue;
        if (*count >= MAX_PATHS) break;
        snprintf(list[*count], MAX_PATH_LEN, "%s", line);
        (*count)++;
    }
    fclose(f);
    qsort(list, (size_t)*count, MAX_PATH_LEN, path_cmp);
    return 0;
}

static void temp_path(char *out, size_t cap, const char *tag) {
    const char *d = getenv("TMPDIR");
    if (!d || !*d) d = "/tmp";
    snprintf(out, cap, "%s/cplus-coverage-%s-%ld.lst", d, tag, (long)getpid());
}

/* ---- whole-file reader --------------------------------------------------- */

static long read_into(const char *path, char *buf, size_t cap, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    size_t total = 0;
    if (!f) return -1;
    for (;;) {
        size_t want = cap - total - 1;
        size_t got;
        if (want == 0) break;
        got = fread(buf + total, 1, want, f);
        total += got;
        if (got < want) break;
    }
    fclose(f);
    buf[total] = '\0';
    *out_len = total;
    return 0; /* success is 0; only fopen failure is -1 */
}

static int gather_sources(void) {
    char tmp[MAX_PATH_LEN];
    size_t src_len = 0;
    int i;

    temp_path(tmp, sizeof(tmp), "src");
    if (read_list("find tests/stdlib -maxdepth 1 -type f -name '*.cp' -print | LC_ALL=C sort",
                  g_sources, &g_nsources, tmp) != 0 ||
        read_list("find tests/stdlib/multi_tu -maxdepth 1 -type f -name '*.cp' -print | LC_ALL=C sort",
                  g_sources, &g_nsources, tmp) != 0 ||
        read_list("find tests/stdlib/examples -maxdepth 1 -type f -name '*.cp' -print | LC_ALL=C sort",
                  g_sources, &g_nsources, tmp) != 0) {
        remove(tmp);
        return -1;
    }
    remove(tmp);
    qsort(g_sources, (size_t)g_nsources, MAX_PATH_LEN, path_cmp);

    g_src_bytes = 0;
    for (i = 0; i < g_nsources; i++) {
        size_t n = 0;
        if (src_len >= MAX_SRC_BUF - 1) break;
        if (read_into(g_sources[i], g_src + src_len,
                      MAX_SRC_BUF - src_len, &n) != 0)
            continue;
        src_len += n;
        g_src_bytes += n;
        if (src_len + 1 < MAX_SRC_BUF) {
            g_src[src_len++] = '\n';
            g_src[src_len] = '\0';
        }
    }
    return 0;
}

/* ---- lexical hit tests --------------------------------------------------- */

/* \bWORD\b -- word appears with non-word characters (or edges) on both sides. */
static int contains_word(const char *text, const char *word) {
    size_t n = strlen(word);
    const char *p = text;
    if (n == 0) return 0;
    while ((p = strstr(p, word)) != NULL) {
        int before = (p > text) ? is_wordc(p[-1]) : 0;
        int after = is_wordc(p[n]);
        if (!before && !after) return 1;
        p++;
    }
    return 0;
}

/* \bNAME\s*\( -- name followed by optional whitespace and an opening paren. */
static int contains_call(const char *text, const char *name) {
    size_t n = strlen(name);
    const char *p = text;
    if (n == 0) return 0;
    while ((p = strstr(p, name)) != NULL) {
        const char *q;
        if (p > text && is_wordc(p[-1])) { p++; continue; }
        q = p + n;
        while (is_ws(*q)) q++;
        if (*q == '(') return 1;
        p++;
    }
    return 0;
}

/* ---- header signature scanning (emulates coverage.py regexes) ------------ */

/* Matches `[^;{]*\)\s*\{\s*$` starting just after '('. */
static int valid_tail(const char *p) {
    while (*p != '\0' && *p != ';' && *p != '{') {
        if (*p == ')') {
            const char *q = p + 1;
            while (is_ws(*q)) q++;
            if (*q == '{') {
                q++;
                while (is_ws(*q)) q++;
                if (*q == '\0') return 1;
            }
        }
        p++;
    }
    return 0;
}

/* SIG: ^\s*(?:static\s+inline\s+)?[A-Za-z_][\w \t\*]*?\b([A-Za-z_]\w*)\s*
 *      \([^;{]*\)\s*\{\s*$
 * The optional `static inline` prefix need not be special-cased: the type
 * part already accepts words and spaces, so the same left-to-right boundary
 * scan finds the last identifier before '(' either way. */
static int match_sig(const char *line, char *name, size_t cap) {
    const char *p = ltrim(line);
    size_t len, i;
    if (!is_alpha_(*p)) return 0;
    len = strlen(p);
    for (i = 1; i < len; i++) {
        size_t k, j;
        if (is_wordc(p[i - 1]) == is_wordc(p[i])) continue; /* \b */
        for (k = 0; k < i; k++) {
            char c = p[k];
            if (!(is_wordc(c) || c == ' ' || c == '\t' || c == '*')) break;
        }
        if (k != i) continue;                 /* type part not [A-Za-z_][\w \t\*]* */
        if (!is_alpha_(p[i])) continue;       /* name must start with a letter/_ */
        j = i + 1;
        while (is_wordc(p[j])) j++;
        k = j;
        while (is_ws(p[k])) k++;
        if (p[k] != '(') continue;
        if (!valid_tail(p + k + 1)) continue;
        copy_span(name, cap, p + i, j - i);
        return 1;
    }
    return 0;
}

/* CTOR: ^\s*([A-Za-z_]\w*)\s*\([^;{]*\)\s*\{\s*$ */
static int match_ctor(const char *line, char *name, size_t cap) {
    const char *p = ltrim(line);
    size_t j, k;
    if (!is_alpha_(*p)) return 0;
    j = 1;
    while (is_wordc(p[j])) j++;
    k = j;
    while (is_ws(p[k])) k++;
    if (p[k] != '(') return 0;
    if (!valid_tail(p + k + 1)) return 0;
    copy_span(name, cap, p, j);
    return 1;
}

/* CLASS: ^\s*class\s+([A-Za-z_]\w*)\s*\{ */
static int match_class(const char *line, char *name, size_t cap) {
    const char *p = ltrim(line);
    size_t j, k;
    if (strncmp(p, "class", 5) != 0) return 0;
    p += 5;
    if (!is_ws(*p)) return 0;
    while (is_ws(*p)) p++;
    if (!is_alpha_(*p)) return 0;
    j = 1;
    while (is_wordc(p[j])) j++;
    k = j;
    while (is_ws(p[k])) k++;
    if (p[k] != '{') return 0;
    copy_span(name, cap, p, j);
    return 1;
}

static int is_skipped(const char *name) {
    static const char *skip[] = {"if", "for", "while", "switch", "do", "else", "return"};
    int i;
    for (i = 0; i < 7; i++)
        if (strcmp(name, skip[i]) == 0) return 1;
    if (strcmp(name, "main") == 0) return 1;
    if (strncmp(name, "stdlib_", 7) == 0) return 1;
    return 0;
}

/* Populate g_entries[] from the NUL-terminated lines in g_hdr. */
static void parse_header(void) {
    char cls[MAX_NAME];
    char name[MAX_NAME];
    char *p = g_hdr;
    cls[0] = '\0';
    g_nentries = 0;
    while (*p != '\0') {
        char *e = strchr(p, '\n');
        const char *t;
        if (e) *e = '\0';
        t = ltrim(p);
        if (*t != '#') {
            /* Python: CLASS first (with `continue`), then `};` close, then
             * SIG, then CTOR.  Reuse `name` as scratch for each attempt. */
            name[0] = '\0';
            if (match_class(p, name, sizeof(name))) {
                snprintf(cls, sizeof(cls), "%s", name);
            } else {
                size_t n = rtrim_len(t, strlen(t));
                if (cls[0] != '\0' && n == 2 && t[0] == '}' && t[1] == ';') {
                    cls[0] = '\0';
                } else {
                    if (!match_sig(p, name, sizeof(name)))
                        match_ctor(p, name, sizeof(name));
                    if (name[0] != '\0' && !is_skipped(name) &&
                        g_nentries < MAX_ENTRIES) {
                        snprintf(g_entries[g_nentries].name, MAX_NAME, "%s", name);
                        snprintf(g_entries[g_nentries].cls, MAX_NAME, "%s", cls);
                        g_nentries++;
                    }
                }
            }
        }
        if (!e) break;
        p = e + 1;
    }
}

/* ---- report -------------------------------------------------------------- */

static void add_failure(const char *name, double pct, const char *missing) {
    if (g_nfails >= MAX_FAILS) return;
    snprintf(g_fails[g_nfails].name, MAX_NAME, "%s", name);
    g_fails[g_nfails].pct = pct;
    snprintf(g_fails[g_nfails].missing, MAX_MISSING, "%s", missing);
    g_nfails++;
}

/* g_hdr is already filled and parsed by the caller.  Returns the percentage. */
static double report_header(const char *path) {
    const char *base = strrchr(path, '/');
    char name[MAX_NAME];
    char missing[MAX_MISSING];
    size_t mlen = 0;
    int covered = 0, i;
    double pct;
    (void)g_hdr;

    base = base ? base + 1 : path;
    snprintf(name, sizeof(name), "%s", base);

    missing[0] = '\0';
    for (i = 0; i < g_nentries; i++) {
        const char *n = g_entries[i].name;
        const char *c = g_entries[i].cls;
        int hit = 0;
        size_t nn = strlen(n);
        if (strcmp(n, "drop") == 0 && c[0] != '\0') {
            hit = contains_word(g_src, c);
        } else if (nn >= 5 && strcmp(n + nn - 5, "_drop") == 0) {
            char owner[MAX_NAME];
            copy_span(owner, sizeof(owner), n, nn - 5);
            hit = contains_word(g_src, owner);
        } else {
            hit = contains_call(g_src, n);
        }
        if (hit) {
            covered++;
        } else {
            char shown[MAX_NAME * 2];
            size_t slen;
            if (c[0] != '\0')
                snprintf(shown, sizeof(shown), "%s.%s", c, n);
            else
                snprintf(shown, sizeof(shown), "%s", n);
            slen = strlen(shown);
            if (mlen > 0 && mlen + 2 < sizeof(missing)) {
                missing[mlen++] = ',';
                missing[mlen++] = ' ';
            }
            if (mlen + slen >= sizeof(missing)) slen = sizeof(missing) - mlen - 1;
            if (slen > 0) {
                memcpy(missing + mlen, shown, slen);
                mlen += slen;
            }
            missing[mlen] = '\0';
        }
    }
    pct = 100.0 * (double)covered / (double)g_nentries;
    printf("%-18s %8d %8d %6.1f%%\n", name, covered, g_nentries, pct);
    if (pct < THRESHOLD) add_failure(name, pct, missing);
    return pct;
}

int main(int argc, char **argv) {
    char tmp[MAX_PATH_LEN];
    double worst = 100.0;
    double pct;
    int total_api;
    int i;

    /* Python locates ROOT from __file__.  Accept an optional repository root
     * argument; otherwise run from the repository root. */
    if (argc > 1 && chdir(argv[1]) != 0) {
        fprintf(stderr, "coverage: cannot chdir to %s\n", argv[1]);
        return 2;
    }

    temp_path(tmp, sizeof(tmp), "hdr");
    if (read_list("find stdlib -maxdepth 1 -type f -name '*.hp' -print | LC_ALL=C sort",
                  g_headers, &g_nheaders, tmp) != 0) {
        fprintf(stderr, "coverage: cannot enumerate stdlib/*.hp\n");
        remove(tmp);
        return 2;
    }
    remove(tmp);

    if (gather_sources() != 0) {
        fprintf(stderr, "coverage: cannot enumerate test sources\n");
        return 2;
    }
    /* Empty discovery is a hard error, never a green report. */
    if (g_nheaders == 0 || g_nsources == 0 || g_src_bytes == 0) {
        fprintf(stderr, "coverage: no headers (%d) or no test sources (%d, %zu bytes)\n",
                g_nheaders, g_nsources, g_src_bytes);
        return 2;
    }

    printf("%-18s %8s %8s %7s\n", "library", "covered", "total", "percent");
    printf("%-18s %8s %8s %7s\n", "------------------", "--------", "--------", "-------");

    total_api = 0;
    for (i = 0; i < g_nheaders; i++) {
        size_t hdr_len = 0;
        if (read_into(g_headers[i], g_hdr, MAX_HDR_BUF, &hdr_len) != 0) continue;
        parse_header();
        if (g_nentries == 0) continue; /* coverage.py skips headers with no api */
        total_api += g_nentries;
        pct = report_header(g_headers[i]);
        if (pct < worst) worst = pct;
    }
    if (total_api == 0) {
        fprintf(stderr, "coverage: no public APIs parsed from %d headers\n", g_nheaders);
        return 2;
    }

    printf("\n");
    if (g_nfails > 0) {
        for (i = 0; i < g_nfails; i++) {
            printf("BELOW %.0f%%: %s (%.1f%%)\n", THRESHOLD,
                   g_fails[i].name, g_fails[i].pct);
            printf("  not called by any test/example: %s\n", g_fails[i].missing);
        }
        return 1;
    }
    printf("all libraries >= %.0f%% API coverage (worst %.1f%%)\n", THRESHOLD, worst);
    return 0;
}
