/* Native C+ runner for the 100-configuration fuzz corpus.
 *
 * This is the C+ counterpart of run.py.  It discovers every f100_* harness
 * under tests/crt/fuzz and tests/stdlib/fuzz at run time, builds each one with
 * the host compiler (C) or the repository C+ driver (.cp), runs it under a
 * 30-second bound and writes the same build/fuzz100 layout as run.py:
 * summary.json, per-case result.json, captured logs and built binaries.
 *
 * Command execution uses the hardened libc cplus_system() wrapper; all file
 * I/O uses the C+ standard library stdlib/file.hp RAII handle.  The runner is
 * intentionally self-contained: it only depends on headers from this repo and
 * on build/libc/libcplus_hardened.a for cplus_system().
 *
 * Build and run with:  sh tests/fuzz100/run_native.sh [options]
 */
#ifndef F100_RUN_CP
#define F100_RUN_CP

#include <ctype.h>
#include <dirent.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"

extern int cplus_system(const char *cmd);

#define TIMEOUT_SECONDS 30
#define TAIL_CHARS 4000
#define MAX_PATH 2048
#define MAX_DEPTH 16

static const char *STATUSES[] = {"PASS", "FAIL", "BUILD_FAILURE", "TIMEOUT",
                                 "SKIPPED", "BLOCKED"};
#define N_STATUSES 6

/* ------------------------------------------------------------------ */
/* Small dynamic string buffer                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char *d;
    size_t n;
    size_t cap;
} Buf;

static void buf_init(Buf *b) {
    b->cap = 4096;
    b->d = (char *)malloc(b->cap);
    b->n = 0;
    if (b->d != NULL)
        b->d[0] = '\0';
}

static void buf_need(Buf *b, size_t extra) {
    if (b->n + extra + 1 <= b->cap)
        return;
    size_t nc = b->cap;
    while (nc < b->n + extra + 1)
        nc *= 2;
    /* Copy into a fresh allocation instead of realloc: the C+ strict checker
     * cannot prove realloc alias state, but this sequence has explicit
     * success/failure handling for every owner. */
    char *nd = (char *)malloc(nc);
    if (nd == NULL)
        return;
    size_t keep = b->n < b->cap ? b->n : b->cap;
    memcpy(nd, b->d, keep);
    nd[keep] = '\0';
    free(b->d);
    b->d = nd;
    b->cap = nc;
}

static void buf_addn(Buf *b, const char *s, size_t k) {
    buf_need(b, k);
    if (b->d == NULL)
        return;
    memcpy(b->d + b->n, s, k);
    b->n += k;
    b->d[b->n] = '\0';
}

static void buf_add(Buf *b, const char *s) {
    if (s != NULL)
        buf_addn(b, s, strlen(s));
}

/* Literal-format append.  The format string is always written literally at
 * the call site, so the strict FFI checker sees a constant format and a
 * bounded destination. */
#define buf_addf(b, ...)                                                       \
    do {                                                                       \
        char _f100_tmp[8192];                                                  \
        int _f100_n = snprintf(_f100_tmp, sizeof(_f100_tmp), __VA_ARGS__);      \
        if (_f100_n > 0) {                                                     \
            size_t _f100_k = (size_t)_f100_n;                                  \
            if (_f100_k >= sizeof(_f100_tmp))                                  \
                _f100_k = sizeof(_f100_tmp) - 1;                               \
            buf_addn((b), _f100_tmp, _f100_k);                                  \
        }                                                                      \
    } while (0)

static void ind(Buf *b, int n) {
    while (n-- > 0)
        buf_add(b, " ");
}

/* ------------------------------------------------------------------ */
/* String / path helpers                                                */
/* ------------------------------------------------------------------ */

static char *xstrdup(const char *s) {
    if (s == NULL)
        return NULL;
    size_t k = strlen(s) + 1;
    char *p = (char *)malloc(k);
    if (p != NULL)
        memcpy(p, s, k);
    return p;
}

typedef struct {
    char **v;
    int n;
    int cap;
} StrList;

static void sl_init(StrList *l) {
    l->v = NULL;
    l->n = 0;
    l->cap = 0;
}

static void sl_add(StrList *l, char *s) {
    if (l->n == l->cap) {
        int nc = l->cap == 0 ? 16 : l->cap * 2;
        size_t bytes = (size_t)nc * sizeof(char *);
        char **nv = (char **)malloc(bytes);
        if (nv == NULL)
            return;
        size_t keep = (size_t)l->n * sizeof(char *);
        if (keep > 0)
            memcpy(nv, l->v, keep);
        free(l->v);
        l->v = nv;
        l->cap = nc;
    }
    l->v[l->n++] = s;
}

static int cmp_strp(const void *a, const void *b) {
    const char *sa = *(const char *const *)a;
    const char *sb = *(const char *const *)b;
    return strcmp(sa, sb);
}

static char *path_join(const char *a, const char *b) {
    size_t la = strlen(a), lb = strlen(b);
    int need_slash = 0;
    if (la > 0) {
        char a_last;
        __builtin_memcpy(&a_last, a + (la - 1), sizeof(a_last));
        need_slash = a_last != '/';
    }
    char *p = (char *)malloc(la + (size_t)need_slash + lb + 1);
    if (p == NULL)
        return NULL;
    memcpy(p, a, la);
    if (need_slash)
        p[la++] = '/';
    memcpy(p + la, b, lb + 1);
    return p;
}

static char *path_rel_to_root(const char *root, const char *full) {
    size_t n = strlen(root);
    int is_child = 0;
    if (strncmp(full, root, n) == 0) {
        char next;
        __builtin_memcpy(&next, full + n, sizeof(next));
        is_child = next == '/';
    }
    if (is_child)
        return xstrdup(full + n + 1);
    return xstrdup(full);
}

static int is_regular(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

static int is_executable(const char *p) {
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode) && access(p, X_OK) == 0;
}

static int ends_with(const char *s, const char *suffix) {
    size_t a = strlen(s), b = strlen(suffix);
    return a >= b && strcmp(s + a - b, suffix) == 0;
}

static char *basename_of(const char *p) {
    const char *slash = strrchr(p, '/');
    return xstrdup(slash != NULL ? slash + 1 : p);
}

static char *stem_of(const char *name) {
    const char *dot = strrchr(name, '.');
    if (dot == NULL)
        return xstrdup(name);
    size_t k = (size_t)(dot - name);
    char *s = (char *)malloc(k + 1);
    memcpy(s, name, k);
    s[k] = '\0';
    return s;
}

static int str_contains_ci(const char *hay, const char *needle) {
    if (hay == NULL || needle == NULL)
        return 0;
    size_t nl = strlen(needle);
    if (nl == 0)
        return 1;
    for (; *hay != '\0'; hay++) {
        size_t i = 0;
        while (i < nl) {
            char h, d;
            __builtin_memcpy(&h, hay + i, sizeof(h));
            __builtin_memcpy(&d, needle + i, sizeof(d));
            if (tolower((unsigned char)h) != tolower((unsigned char)d))
                break;
            i++;
        }
        if (i == nl)
            return 1;
    }
    return 0;
}

static void upper_inplace(char *s) {
    for (; *s != '\0'; s++)
        *s = (char)toupper((unsigned char)*s);
}

/* ------------------------------------------------------------------ */
/* File I/O through stdlib/file.hp                                     */
/* ------------------------------------------------------------------ */

static char *read_whole_file(const char *path) {
    error e = error();
    file f = file(path, "r");
    if (!f.is_open())
        return xstrdup("");
    FILE *fp = f.native();
    if (fp == NULL)
        return xstrdup("");
    if (fseek(fp, 0, SEEK_END) != 0)
        return xstrdup("");
    long sz = ftell(fp);
    if (sz < 0)
        sz = 0;
    if (fseek(fp, 0, SEEK_SET) != 0)
        return xstrdup("");
    char *out = (char *)malloc((size_t)sz + 1);
    if (out == NULL)
        return xstrdup("");
    size_t got = fread(out, 1, (size_t)sz, fp);
    out[got] = '\0';
    return out;
}

static void write_whole_file(const char *path, const char *text) {
    error e = error();
    file f = file(path, "w");
    if (!f.is_open())
        return;
    f.write_bytes(text, strlen(text), &e);
}

static char *tail_of(const char *text) {
    size_t k = strlen(text);
    if (k <= TAIL_CHARS)
        return xstrdup(text);
    char *p = (char *)malloc(TAIL_CHARS + 1);
    memcpy(p, text + k - TAIL_CHARS, TAIL_CHARS);
    p[TAIL_CHARS] = '\0';
    return p;
}

static void mkdir_p(const char *path) {
    if (path == NULL)
        return;
    char first;
    __builtin_memcpy(&first, path, sizeof(first));
    if (first == '\0')
        return;
    size_t n = strlen(path);
    char *tmp = (char *)malloc(n + 1);
    memcpy(tmp, path, n + 1);
    for (size_t i = 1; i <= n; i++) {
        char c;
        __builtin_memcpy(&c, tmp + i, sizeof(c));
        if (c == '/' || c == '\0') {
            char nul = '\0';
            __builtin_memcpy(tmp + i, &nul, sizeof(nul));
            char first;
            __builtin_memcpy(&first, tmp, sizeof(first));
            if (first != '\0')
                mkdir(tmp, 0777);
            __builtin_memcpy(tmp + i, &c, sizeof(c));
        }
    }
    free(tmp);
}

/* ------------------------------------------------------------------ */
/* Shell quoting (mirrors python shlex.join closely enough)            */
/* ------------------------------------------------------------------ */

static int shlex_plain(const char *s) {
    if (s == NULL)
        return 0;
    char first;
    __builtin_memcpy(&first, s, sizeof(first));
    if (first == '\0')
        return 0;
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') ||
                 strchr("_@%+=:,./-", (int)c) != NULL;
        if (!ok)
            return 0;
    }
    return 1;
}

static void sh_quote(Buf *b, const char *s) {
    buf_add(b, "'");
    for (; s != NULL && *s != '\0'; s++) {
        if (*s == '\'')
            buf_add(b, "'\\''");
        else
            buf_addn(b, s, 1);
    }
    buf_add(b, "'");
}

/* Join already-built argv-like tokens into a shell command. */
static char *join_tokens(StrList *tokens) {
    Buf b;
    buf_init(&b);
    for (int i = 0; i < tokens->n; i++) {
        if (i > 0)
            buf_add(&b, " ");
        if (shlex_plain(tokens->v[i]))
            buf_add(&b, tokens->v[i]);
        else
            sh_quote(&b, tokens->v[i]);
    }
    return b.d;
}

/* ------------------------------------------------------------------ */
/* Globals                                                             */
/* ------------------------------------------------------------------ */

static const char *g_root = ".";
static char g_clang[MAX_PATH];
static char g_cplus[MAX_PATH];
static char g_timeout[MAX_PATH];
static int g_clang_ok = 0;
static int g_cplus_ok = 0;
static int g_dry_run = 0;
static int g_json_stdout = 0;
static StrList g_configs;
static char g_out_dir[MAX_PATH];
static char g_case_dir[MAX_PATH];
static char g_bin_dir[MAX_PATH];

/* ------------------------------------------------------------------ */
/* Executable resolution                                               */
/* ------------------------------------------------------------------ */

static char *which(const char *name) {
    if (strchr(name, '/') != NULL)
        return is_executable(name) ? xstrdup(name) : NULL;
    const char *path = getenv("PATH");
    if (path == NULL)
        return NULL;
    Buf b;
    buf_init(&b);
    char *dup = xstrdup(path);
    char *save = dup;
    char *part = dup;
    while (part != NULL) {
        char *colon = strchr(part, ':');
        if (colon != NULL)
            *colon = '\0';
        b.n = 0;
        if (b.d != NULL)
            b.d[0] = '\0';
        if (*part != '\0')
            buf_addf(&b, "%s/%s", part, name);
        else
            buf_addf(&b, "./%s", name);
        if (is_executable(b.d)) {
            char *found = xstrdup(b.d);
            free(dup);
            return found;
        }
        part = colon != NULL ? colon + 1 : NULL;
    }
    free(dup);
    return NULL;
}

static char *resolve_exe(const char *value) {
    if (value == NULL)
        return NULL;
    char first;
    __builtin_memcpy(&first, value, sizeof(first));
    if (first == '\0')
        return NULL;
    if (strchr(value, '/') != NULL)
        return is_executable(value) ? xstrdup(value) : NULL;
    return which(value);
}

static void find_timeout(void) {
    g_timeout[0] = '\0';
    const char *env = getenv("F100_TIMEOUT");
    if (env != NULL && env[0] != '\0') {
        char *p = resolve_exe(env);
        if (p != NULL) {
            snprintf(g_timeout, sizeof(g_timeout), "%s", p);
            return;
        }
    }
    const char *cands[] = {"/usr/bin/timeout", "/opt/homebrew/bin/gtimeout",
                           "/usr/local/bin/gtimeout", NULL};
    for (int i = 0; cands[i] != NULL; i++) {
        if (is_executable(cands[i])) {
            snprintf(g_timeout, sizeof(g_timeout), "%s", cands[i]);
            return;
        }
    }
    char *p = which("timeout");
    if (p == NULL)
        p = which("gtimeout");
    if (p != NULL)
        snprintf(g_timeout, sizeof(g_timeout), "%s", p);
}

/* ------------------------------------------------------------------ */
/* Case model                                                          */
/* ------------------------------------------------------------------ */

typedef struct {
    char *name;
    char *path;
    char *slug;
    char *config;
    char kind[16];
    int json_case;
    char *skip_reason;
    char *block_reason;

    int build_rc;
    int build_timed_out;
    char *build_spawn_error;
    double build_duration;
    char *build_stdout;
    char *build_stderr;
    char *build_command;

    int run_rc;
    int run_timed_out;
    char *run_spawn_error;
    double run_duration;
    char *run_stdout;
    char *run_stderr;
    char *run_command;

    char status[16];
    char stage[16];
    char *reason;
    char logs[4][MAX_PATH];
} Case;

static void case_init(Case *c) {
    memset(c, 0, sizeof(*c));
    c->build_rc = 0;
    c->run_rc = 0;
}

/* ------------------------------------------------------------------ */
/* Discovery                                                           */
/* ------------------------------------------------------------------ */

static void walk_dir(const char *full, const char *rel, StrList *out,
                     int depth) {
    if (depth > MAX_DEPTH)
        return;
    DIR *d = opendir(full);
    if (d == NULL)
        return;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
            continue;
        char *child_full = path_join(full, de->d_name);
        char rel_first;
        __builtin_memcpy(&rel_first, rel, sizeof(rel_first));
        char *child_rel = rel_first != '\0' ? path_join(rel, de->d_name)
                                            : xstrdup(de->d_name);
        struct stat st;
        int isdir = stat(child_full, &st) == 0 && S_ISDIR(st.st_mode);
        if (isdir) {
            walk_dir(child_full, child_rel, out, depth + 1);
        } else if (is_regular(child_full) &&
                   strncmp(de->d_name, "f100_", 5) == 0) {
            sl_add(out, child_rel);
            child_rel = NULL;
        }
        free(child_full);
        free(child_rel);
    }
    closedir(d);
}

static StrList discover(void) {
    const char *dirs[] = {"tests/crt/fuzz", "tests/stdlib/fuzz", NULL};
    StrList found;
    sl_init(&found);
    for (int i = 0; dirs[i] != NULL; i++) {
        char *base = path_join(g_root, dirs[i]);
        /* Seed the relative prefix with the search directory itself so each
         * discovered path is relative to the repository root (as in run.py). */
        walk_dir(base, dirs[i], &found, 0);
        free(base);
    }
    if (found.n > 1)
        qsort(found.v, (size_t)found.n, sizeof(char *), cmp_strp);
    return found;
}

/* ------------------------------------------------------------------ */
/* Config / kind / slug parsing                                        */
/* ------------------------------------------------------------------ */

static char *parse_config(const char *name) {
    char *stem = stem_of(name);
    const char *p = stem;
    if (strncasecmp(p, "f100", 4) == 0) {
        p += 4;
        if (*p == '_' || *p == '.' || *p == '-')
            p++;
    }
    for (const char *q = p; q[0] != '\0' && q[1] != '\0' && q[2] != '\0'; q++) {
        if ((*q != 'F' && *q != 'f') || !isdigit((unsigned char)q[1]) ||
            !isdigit((unsigned char)q[2]) || !isdigit((unsigned char)q[3]))
            continue;
        if (q > p && isalnum((unsigned char)q[-1]))
            continue;
        if (q[4] != '\0' && isalnum((unsigned char)q[4]))
            continue;
        char *out = (char *)malloc(5);
        out[0] = 'F';
        out[1] = q[1];
        out[2] = q[2];
        out[3] = q[3];
        out[4] = '\0';
        free(stem);
        return out;
    }
    free(stem);
    return NULL;
}

static void kind_of(const char *name, char *out, size_t cap) {
    if (ends_with(name, ".c"))
        snprintf(out, cap, "c");
    else if (ends_with(name, ".cp"))
        snprintf(out, cap, "cplus");
    else
        snprintf(out, cap, "unsupported");
}

static char *slug_of(const char *rel) {
    size_t n = strlen(rel);
    char *s = (char *)malloc(n + 1);
    for (size_t i = 0; i < n; i++) {
        unsigned char c;
        __builtin_memcpy(&c, rel + i, sizeof(c));
        int ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                 (c >= '0' && c <= '9') || c == '_' || c == '-';
        s[i] = ok ? (char)c : '_';
    }
    s[n] = '\0';
    return s;
}

static int wants_json_flags(const char *name, const char *source) {
    return str_contains_ci(name, "json") || str_contains_ci(source, "json.hp") ||
           str_contains_ci(source, "yyjson");
}

static int selected(const Case *c) {
    if (g_configs.n == 0)
        return 1;
    char *hay = xstrdup(c->name);
    upper_inplace(hay);
    int ok = 0;
    for (int i = 0; i < g_configs.n; i++) {
        if (strstr(hay, g_configs.v[i]) != NULL) {
            ok = 1;
            break;
        }
    }
    free(hay);
    return ok;
}

static void normalize_configs(StrList *raw) {
    sl_init(&g_configs);
    for (int i = 0; i < raw->n; i++) {
        char *dup = xstrdup(raw->v[i]);
        char *save = dup;
        char *part = dup;
        while (part != NULL) {
            char *comma = strchr(part, ',');
            if (comma != NULL)
                *comma = '\0';
            while (*part == ' ' || *part == '\t')
                part++;
            char *end = part + strlen(part);
            while (end > part && (end[-1] == ' ' || end[-1] == '\t'))
                *--end = '\0';
            if (*part != '\0' && strcasecmp(part, "all") != 0) {
                char *tok;
                if (strlen(part) == 3 && isdigit((unsigned char)part[0]) &&
                    isdigit((unsigned char)part[1]) &&
                    isdigit((unsigned char)part[2])) {
                    tok = (char *)malloc(5);
                    tok[0] = 'F';
                    tok[1] = part[0];
                    tok[2] = part[1];
                    tok[3] = part[2];
                    tok[4] = '\0';
                } else {
                    tok = xstrdup(part);
                    upper_inplace(tok);
                }
                int exists = 0;
                for (int j = 0; j < g_configs.n; j++) {
                    if (strcmp(g_configs.v[j], tok) == 0) {
                        exists = 1;
                        break;
                    }
                }
                if (!exists)
                    sl_add(&g_configs, tok);
                else
                    free(tok);
            }
            part = comma != NULL ? comma + 1 : NULL;
        }
        free(save);
    }
    if (g_configs.n > 1)
        qsort(g_configs.v, (size_t)g_configs.n, sizeof(char *), cmp_strp);
}

/* ------------------------------------------------------------------ */
/* Build / run                                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    int rc;
    int timed_out;
    char *spawn_error;
    double duration;
    char *stdout_text;
    char *stderr_text;
} Step;

static void json_flags(StrList *toks) {
    char *a = path_join(g_root, "third_party/yyjson");
    char *b = path_join(g_root, "build/ext");
    size_t ia_cap = strlen(a) + 3;
    char *ia = (char *)malloc(ia_cap);
    snprintf(ia, ia_cap, "-I%s", a);
    size_t lb_cap = strlen(b) + 3;
    char *lb = (char *)malloc(lb_cap);
    snprintf(lb, lb_cap, "-L%s", b);
    sl_add(toks, ia);
    sl_add(toks, lb);
    sl_add(toks, xstrdup("-lyyjson"));
    free(a);
    free(b);
}

static char *block_reason_for(const Case *c) {
    if (strcmp(c->kind, "c") == 0 && !g_clang_ok)
        return (char *)"C compiler is not executable: /usr/bin/clang";
    if (strcmp(c->kind, "cplus") == 0 && !g_cplus_ok) {
        Buf b;
        buf_init(&b);
        buf_addf(&b, "C+ driver is not executable: %s/build/c+", g_root);
        return b.d;
    }
    if (c->json_case) {
        char *h = path_join(g_root, "third_party/yyjson/yyjson.h");
        char *a = path_join(g_root, "build/ext/libyyjson.a");
        int mh = !is_regular(h), ma = !is_regular(a);
        free(h);
        free(a);
        if (mh || ma) {
            Buf b;
            buf_init(&b);
            buf_add(&b, "missing yyjson prerequisite(s): ");
            int first = 1;
            if (mh) {
                buf_add(&b, "third_party/yyjson/yyjson.h");
                first = 0;
            }
            if (ma) {
                if (!first)
                    buf_add(&b, ", ");
                buf_add(&b, "build/ext/libyyjson.a");
            }
            return b.d;
        }
    }
    return NULL;
}

static const char *cplus_fallback_path(void);

static char *make_build_command(const Case *c, const char *out_bin) {
    char *full_src = path_join(g_root, c->path);
    StrList toks;
    sl_init(&toks);
    if (strcmp(c->kind, "c") == 0) {
        sl_add(&toks, xstrdup(g_clang_ok ? g_clang : "/usr/bin/clang"));
        sl_add(&toks, xstrdup("-std=c11"));
        sl_add(&toks, xstrdup("-O1"));
        sl_add(&toks, xstrdup("-g"));
        sl_add(&toks, xstrdup("-DCPLUS_FUZZ_SMOKE_MAIN"));
        const char *incs[] = {"", "libc", "libc/include", "libc/install-include",
                              "stdlib/safe", "third_party/safeclib/include",
                              "third_party/intel-safestringlib/include", NULL};
        for (int i = 0; incs[i] != NULL; i++) {
            char *p = incs[i][0] != '\0' ? path_join(g_root, incs[i])
                                         : xstrdup(g_root);
            size_t flag_cap = strlen(p) + 3;
            char *flag = (char *)malloc(flag_cap);
            snprintf(flag, flag_cap, "-I%s", p);
            sl_add(&toks, flag);
            free(p);
        }
        sl_add(&toks, full_src);
        const char *arcs[] = {"build/libc/libcplus_hardened.a",
                              "build/safe/libcplus_safecrt.a", NULL};
        for (int i = 0; arcs[i] != NULL; i++) {
            char *p = path_join(g_root, arcs[i]);
            if (is_regular(p))
                sl_add(&toks, p);
            else
                free(p);
        }
        sl_add(&toks, xstrdup("-pthread"));
        sl_add(&toks, xstrdup("-lm"));
    } else {
        sl_add(&toks, xstrdup(g_cplus_ok ? g_cplus : cplus_fallback_path()));
        sl_add(&toks, xstrdup("build"));
        sl_add(&toks, full_src);
        sl_add(&toks, xstrdup("--strict"));
        sl_add(&toks, xstrdup("--sanitize=none"));
    }
    if (c->json_case)
        json_flags(&toks);
    sl_add(&toks, xstrdup("-o"));
    sl_add(&toks, xstrdup(out_bin));
    char *cmd = join_tokens(&toks);
    return cmd;
}

static const char *cplus_fallback_path(void) {
    static char buf[MAX_PATH];
    snprintf(buf, sizeof(buf), "%s/build/c+", g_root);
    return buf;
}

static Step run_step(const char *display_cmd, const char *out_log,
                     const char *err_log) {
    Step s;
    s.rc = 0;
    s.timed_out = 0;
    s.spawn_error = NULL;
    s.duration = 0.0;
    s.stdout_text = NULL;
    s.stderr_text = NULL;
    Buf cmd;
    buf_init(&cmd);
    if (g_timeout[0] != '\0') {
        buf_add(&cmd, g_timeout);
        buf_addf(&cmd, " %d ", TIMEOUT_SECONDS);
    }
    buf_add(&cmd, display_cmd);
    buf_add(&cmd, " > ");
    sh_quote(&cmd, out_log);
    buf_add(&cmd, " 2> ");
    sh_quote(&cmd, err_log);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);
    int raw = cplus_system(cmd.d);
    clock_gettime(CLOCK_MONOTONIC, &t1);
    s.duration = (double)(t1.tv_sec - t0.tv_sec) +
                 (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;

    int rc;
    if (WIFEXITED(raw))
        rc = WEXITSTATUS(raw);
    else if (WIFSIGNALED(raw))
        rc = -WTERMSIG(raw);
    else
        rc = raw;
    /* Some timeout implementations report 128+signal as a normal exit. */
    if (rc >= 129 && rc <= 192)
        rc = -(rc - 128);
    s.timed_out = rc == 124;
    s.rc = rc;
    s.stdout_text = read_whole_file(out_log);
    s.stderr_text = read_whole_file(err_log);
    return s;
}

/* ------------------------------------------------------------------ */
/* JSON emission                                                       */
/* ------------------------------------------------------------------ */

static void jstring(Buf *b, const char *s) {
    if (s == NULL) {
        buf_add(b, "null");
        return;
    }
    buf_add(b, "\"");
    for (; *s != '\0'; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"': buf_add(b, "\\\""); break;
        case '\\': buf_add(b, "\\\\"); break;
        case '\n': buf_add(b, "\\n"); break;
        case '\r': buf_add(b, "\\r"); break;
        case '\t': buf_add(b, "\\t"); break;
        case '\b': buf_add(b, "\\b"); break;
        case '\f': buf_add(b, "\\f"); break;
        default:
            if (c < 0x20)
                buf_addf(b, "\\u%04x", (unsigned)c);
            else
                buf_addn(b, (const char *)&c, 1);
        }
    }
    buf_add(b, "\"");
}

static char *fmt_double3(double v) {
    char tmp[64];
    snprintf(tmp, sizeof(tmp), "%.3f", v);
    size_t n = strlen(tmp);
    if (strchr(tmp, '.') != NULL) {
        while (n > 0 && tmp[n - 1] == '0')
            n--;
        if (n > 0 && tmp[n - 1] == '.')
            n++; /* keep one digit: "0.0" */
        tmp[n] = '\0';
    }
    return xstrdup(tmp);
}

static void key_str(Buf *b, int indn, const char *key, const char *val,
                    int comma) {
    ind(b, indn);
    buf_add(b, "\"");
    buf_add(b, key);
    buf_add(b, "\": ");
    jstring(b, val);
    if (comma)
        buf_add(b, ",");
    buf_add(b, "\n");
}

static void key_int(Buf *b, int indn, const char *key, int val, int comma) {
    ind(b, indn);
    buf_addf(b, "\"%s\": %d", key, val);
    if (comma)
        buf_add(b, ",");
    buf_add(b, "\n");
}

static void key_bool(Buf *b, int indn, const char *key, int val, int comma) {
    ind(b, indn);
    buf_addf(b, "\"%s\": %s", key, val ? "true" : "false");
    if (comma)
        buf_add(b, ",");
    buf_add(b, "\n");
}

static void key_double(Buf *b, int indn, const char *key, double val,
                       int comma) {
    char *s = fmt_double3(val);
    ind(b, indn);
    buf_addf(b, "\"%s\": %s", key, s);
    free(s);
    if (comma)
        buf_add(b, ",");
    buf_add(b, "\n");
}

static void emit_step(Buf *b, int base, const char *name, int rc,
                      int timed_out, char *spawn_error, double duration,
                      char *stdout_text, char *stderr_text, int comma) {
    ind(b, base);
    buf_addf(b, "\"%s\": {\n", name);
    int f = base + 2;
    key_int(b, f, "rc", rc, 1);
    key_bool(b, f, "timed_out", timed_out, 1);
    key_str(b, f, "spawn_error", spawn_error, 1);
    key_double(b, f, "duration_s", duration, 1);
    char *so = tail_of(stdout_text != NULL ? stdout_text : "");
    char *se = tail_of(stderr_text != NULL ? stderr_text : "");
    key_str(b, f, "stdout_tail", so, 1);
    key_str(b, f, "stderr_tail", se, 0);
    free(so);
    free(se);
    ind(b, base);
    buf_add(b, "}");
    if (comma)
        buf_add(b, ",");
    buf_add(b, "\n");
}

static void emit_case(Buf *b, const Case *c, int base) {
    ind(b, base);
    buf_add(b, "{\n");
    int f = base + 2;
    key_str(b, f, "name", c->name, 1);
    key_str(b, f, "path", c->path, 1);
    key_str(b, f, "slug", c->slug, 1);
    key_str(b, f, "config", c->config, 1);
    key_str(b, f, "kind", c->kind, 1);
    key_bool(b, f, "json_case", c->json_case, 1);
    key_str(b, f, "skip_reason", c->skip_reason, 1);

    ind(b, f);
    buf_add(b, "\"logs\": {\n");
    key_str(b, f + 2, "build.stdout", c->logs[0], 1);
    key_str(b, f + 2, "build.stderr", c->logs[1], 1);
    key_str(b, f + 2, "run.stdout", c->logs[2], 1);
    key_str(b, f + 2, "run.stderr", c->logs[3], 0);
    ind(b, f);
    buf_add(b, "},\n");

    key_str(b, f, "build_command", c->build_command, 1);
    key_str(b, f, "run_command", c->run_command, 1);
    if (c->build_rc == 0 && c->build_stdout == NULL && c->build_duration == 0.0) {
        /* A skipped case: build/run are null.  Distinguish from a real
         * successful build by checking whether a command was recorded. */
    }
    if (c->build_command == NULL) {
        ind(b, f);
        buf_add(b, "\"build\": null,\n");
    } else {
        emit_step(b, f, "build", c->build_rc, c->build_timed_out,
                  c->build_spawn_error, c->build_duration, c->build_stdout,
                  c->build_stderr, 1);
    }
    if (c->run_command == NULL) {
        ind(b, f);
        buf_add(b, "\"run\": null,\n");
    } else {
        emit_step(b, f, "run", c->run_rc, c->run_timed_out, c->run_spawn_error,
                  c->run_duration, c->run_stdout, c->run_stderr, 1);
    }
    key_str(b, f, "status", c->status, 1);
    key_str(b, f, "stage", c->stage, 1);
    key_str(b, f, "reason", c->reason, 0);
    ind(b, base);
    buf_add(b, "}");
}

/* ------------------------------------------------------------------ */
/* CLI                                                                 */
/* ------------------------------------------------------------------ */

static const char *numstr(int v);

static void usage(void) {
    printf("usage: run_native [--config Fnnn] [--dry-run] [--json-stdout] "
           "[--root PATH]\n");
}

static int parse_args(int argc, char **argv, StrList *raw_configs) {
    memset(raw_configs, 0, sizeof(*raw_configs));
    const char *root_override = NULL;
    for (int i = 1; i < argc; i++) {
        const char *arg;
        __builtin_memcpy(&arg, argv + i, sizeof(arg));
        if (strcmp(arg, "--help") == 0 || strcmp(arg, "-h") == 0) {
            usage();
            return 2; /* caller treats 2 with help printed as exit 0 */
        } else if (strcmp(arg, "--dry-run") == 0) {
            g_dry_run = 1;
        } else if (strcmp(arg, "--json-stdout") == 0) {
            g_json_stdout = 1;
        } else if (strcmp(arg, "--config") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --config requires an argument\n");
                return 1;
            }
            i++;
            __builtin_memcpy(&arg, argv + i, sizeof(arg));
            sl_add(raw_configs, xstrdup(arg));
        } else if (strncmp(arg, "--config=", 9) == 0) {
            sl_add(raw_configs, xstrdup(arg + 9));
        } else if (strcmp(arg, "--root") == 0) {
            if (i + 1 >= argc) {
                fprintf(stderr, "error: --root requires an argument\n");
                return 1;
            }
            i++;
            __builtin_memcpy(&arg, argv + i, sizeof(arg));
            root_override = arg;
        } else if (strncmp(arg, "--root=", 7) == 0) {
            root_override = arg + 7;
        } else {
            fprintf(stderr, "error: unrecognized argument: %s\n", arg);
            usage();
            return 1;
        }
    }
    const char *env_root = getenv("F100_ROOT");
    if (root_override != NULL)
        g_root = root_override;
    else if (env_root != NULL && env_root[0] != '\0')
        g_root = env_root;
    else {
        char cwd[MAX_PATH];
        g_root = getcwd(cwd, sizeof(cwd)) != NULL ? xstrdup(cwd) : ".";
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Main                                                                */
/* ------------------------------------------------------------------ */

int main(int argc, char **argv) {
    StrList raw_configs;
    int prc = parse_args(argc, argv, &raw_configs);
    if (prc == 2)
        return 0;
    if (prc != 0)
        return 2;
    normalize_configs(&raw_configs);

    memset(g_clang, 0, sizeof(g_clang));
    memset(g_cplus, 0, sizeof(g_cplus));
    const char *clang_env = getenv("F100_CLANG");
    char *clang_res = resolve_exe(clang_env != NULL && clang_env[0] != '\0'
                                      ? clang_env
                                      : "/usr/bin/clang");
    g_clang_ok = clang_res != NULL;
    snprintf(g_clang, sizeof(g_clang), "%s",
             clang_res != NULL ? clang_res : "/usr/bin/clang");
    free(clang_res);

    Buf cplus_default;
    buf_init(&cplus_default);
    const char *cplus_env = getenv("CPLUS_BIN");
    if (cplus_env == NULL || cplus_env[0] == '\0')
        buf_addf(&cplus_default, "%s/build/c+", g_root);
    else
        buf_add(&cplus_default, cplus_env);
    char *cplus_res = resolve_exe(cplus_default.d);
    g_cplus_ok = cplus_res != NULL;
    snprintf(g_cplus, sizeof(g_cplus), "%s",
             cplus_res != NULL ? cplus_res : cplus_default.d);
    free(cplus_res);

    find_timeout();

    /* Environment passed to children (set on this process so it is inherited). */
    if (getenv("CPLUS_CLANG") == NULL)
        setenv("CPLUS_CLANG", "/usr/bin/clang", 0);
    const char *oldpath = getenv("PATH");
    Buf newpath;
    buf_init(&newpath);
    buf_add(&newpath, "/usr/bin:/bin:/usr/sbin:/sbin:/opt/homebrew/bin:");
    if (oldpath != NULL)
        buf_add(&newpath, oldpath);
    setenv("PATH", newpath.d, 1);

    /* Output directories. */
    snprintf(g_out_dir, sizeof(g_out_dir), "%s/build/fuzz100", g_root);
    snprintf(g_case_dir, sizeof(g_case_dir), "%s/cases", g_out_dir);
    snprintf(g_bin_dir, sizeof(g_bin_dir), "%s/bin", g_out_dir);
    mkdir_p(g_case_dir);
    mkdir_p(g_bin_dir);

    StrList paths = discover();
    int n = paths.n;
    Case *cases = (Case *)calloc((size_t)(n > 0 ? n : 1), sizeof(Case));
    int any_match = 0;

    for (int i = 0; i < n; i++) {
        Case *c = &cases[i];
        case_init(c);
        c->path = xstrdup(paths.v[i]);
        c->name = basename_of(c->path);
        kind_of(c->name, c->kind, sizeof(c->kind));
        c->config = parse_config(c->name);
        c->slug = slug_of(c->path);

        char *full_src = path_join(g_root, c->path);
        char *source = read_whole_file(full_src);
        /* run.py tests path.stem (extension removed), not the full basename,
         * so f100_thread_configs.json is NOT treated as a JSON case. */
        char *stem = stem_of(c->name);
        c->json_case = wants_json_flags(stem, source);
        free(stem);
        free(source);
        free(full_src);

        if (selected(c))
            any_match = 1;
        else
            c->skip_reason = xstrdup("filtered out by --config");
        if (c->skip_reason == NULL && strcmp(c->kind, "unsupported") == 0) {
            const char *dot = strrchr(c->name, '.');
            Buf r;
            buf_init(&r);
            buf_add(&r, "unsupported harness extension: ");
            buf_add(&r, dot != NULL ? dot : "<none>");
            c->skip_reason = r.d;
        }
        if (c->skip_reason == NULL && g_dry_run)
            c->skip_reason = xstrdup("dry-run (no build or run performed)");
    }

    /* Execute cases. */
    for (int i = 0; i < n; i++) {
        Case *c = &cases[i];
        char *case_path = path_join(g_case_dir, c->slug);
        mkdir_p(case_path);

        const char *log_names[4] = {"build.stdout.log", "build.stderr.log",
                                    "run.stdout.log", "run.stderr.log"};
        char *log_full[4];
        for (int k = 0; k < 4; k++) {
            log_full[k] = path_join(case_path, log_names[k]);
            Buf rel;
            buf_init(&rel);
            buf_addf(&rel, "build/fuzz100/cases/%s/%s", c->slug, log_names[k]);
            snprintf(c->logs[k], sizeof(c->logs[k]), "%s", rel.d);
        }

        char *full_src = path_join(g_root, c->path);
        if (c->skip_reason == NULL && !is_regular(full_src))
            c->skip_reason =
                xstrdup("source file disappeared after discovery (concurrent edit)");
        free(full_src);

        strcpy(c->status, "SKIPPED");
        strcpy(c->stage, "discovery");
        c->reason = c->skip_reason != NULL ? xstrdup(c->skip_reason) : NULL;

        if (c->skip_reason == NULL) {
            c->block_reason = block_reason_for(c);
            char *out_bin = path_join(g_bin_dir, c->slug);
            Buf bin_full;
            buf_init(&bin_full);
            buf_addf(&bin_full, "%s.bin", out_bin);
            free(out_bin);
            char *bin_path = bin_full.d;

            c->build_command = make_build_command(c, bin_path);
            char *cmd_file = path_join(case_path, "build.command.txt");
            Buf cmd_text;
            buf_init(&cmd_text);
            buf_add(&cmd_text, c->build_command);
            buf_add(&cmd_text, "\n");
            write_whole_file(cmd_file, cmd_text.d);
            free(cmd_file);

            Step build = run_step(c->build_command, log_full[0], log_full[1]);
            c->build_rc = build.rc;
            c->build_timed_out = build.timed_out;
            c->build_spawn_error = build.spawn_error;
            c->build_duration = build.duration;
            c->build_stdout = build.stdout_text;
            c->build_stderr = build.stderr_text;

            if (build.rc == 0 && !build.timed_out && build.spawn_error == NULL) {
                StrList runtoks;
                sl_init(&runtoks);
                sl_add(&runtoks, xstrdup(bin_path));
                c->run_command = join_tokens(&runtoks);
                char *run_cmd_file = path_join(case_path, "run.command.txt");
                Buf run_text;
                buf_init(&run_text);
                buf_add(&run_text, c->run_command);
                buf_add(&run_text, "\n");
                write_whole_file(run_cmd_file, run_text.d);
                free(run_cmd_file);

                Step run = run_step(c->run_command, log_full[2], log_full[3]);
                c->run_rc = run.rc;
                c->run_timed_out = run.timed_out;
                c->run_spawn_error = run.spawn_error;
                c->run_duration = run.duration;
                c->run_stdout = run.stdout_text;
                c->run_stderr = run.stderr_text;
            }

            /* Classification, first match wins. */
            if (c->block_reason != NULL) {
                strcpy(c->status, "BLOCKED");
                strcpy(c->stage, "prerequisite");
                c->reason = xstrdup(c->block_reason);
            } else if (build.spawn_error != NULL) {
                strcpy(c->status, "BLOCKED");
                strcpy(c->stage, "build");
                c->reason = build.spawn_error;
            } else if (build.timed_out) {
                strcpy(c->status, "TIMEOUT");
                strcpy(c->stage, "build");
                Buf r;
                buf_init(&r);
                buf_addf(&r, "build exceeded %d s", TIMEOUT_SECONDS);
                c->reason = r.d;
            } else if (build.rc != 0) {
                strcpy(c->status, "BUILD_FAILURE");
                strcpy(c->stage, "build");
                Buf r;
                buf_init(&r);
                buf_addf(&r, "compiler exited %d", build.rc);
                c->reason = r.d;
            } else if (c->run_command == NULL) {
                strcpy(c->status, "BLOCKED");
                strcpy(c->stage, "run");
                c->reason = xstrdup("binary could not be started");
            } else if (c->run_spawn_error != NULL) {
                strcpy(c->status, "BLOCKED");
                strcpy(c->stage, "run");
                c->reason = c->run_spawn_error;
            } else if (c->run_timed_out) {
                strcpy(c->status, "TIMEOUT");
                strcpy(c->stage, "run");
                Buf r;
                buf_init(&r);
                buf_addf(&r, "run exceeded %d s", TIMEOUT_SECONDS);
                c->reason = r.d;
            } else if (c->run_rc == 0) {
                strcpy(c->status, "PASS");
                strcpy(c->stage, "run");
                c->reason = xstrdup("build ok, run exited 0");
            } else {
                strcpy(c->status, "FAIL");
                strcpy(c->stage, "run");
                Buf r;
                buf_init(&r);
                buf_addf(&r, "build ok, run exited %d", c->run_rc);
                c->reason = r.d;
            }
            free(bin_path);
        }

        /* Per-case result.json (same field order as run.py). */
        Buf result;
        buf_init(&result);
        emit_case(&result, c, 0);
        buf_add(&result, "\n");
        char *result_file = path_join(case_path, "result.json");
        write_whole_file(result_file, result.d);
        free(result_file);

        for (int k = 0; k < 4; k++)
            free(log_full[k]);
        free(case_path);
    }

    /* Counts and summary. */
    int counts[N_STATUSES];
    for (int j = 0; j < N_STATUSES; j++)
        counts[j] = 0;
    for (int i = 0; i < n; i++) {
        for (int j = 0; j < N_STATUSES; j++) {
            if (strcmp(cases[i].status, STATUSES[j]) == 0) {
                counts[j]++;
                break;
            }
        }
    }
    int executed = n - counts[4]; /* SKIPPED */

    Buf summary;
    buf_init(&summary);
    buf_add(&summary, "{\n");
    key_int(&summary, 2, "schema", 1, 1);
    key_str(&summary, 2, "root", g_root, 1);
    key_str(&summary, 2, "runner", "tests/fuzz100/run.cp", 1);
    key_int(&summary, 2, "timeout_s", TIMEOUT_SECONDS, 1);
    /* filter_configs */
    buf_add(&summary, "  \"filter_configs\": [");
    for (int i = 0; i < g_configs.n; i++) {
        if (i > 0)
            buf_add(&summary, ", ");
        jstring(&summary, g_configs.v[i]);
    }
    buf_add(&summary, "],\n");
    key_bool(&summary, 2, "dry_run", g_dry_run, 1);
    key_int(&summary, 2, "discovered", n, 1);
    key_int(&summary, 2, "executed", executed, 1);
    buf_add(&summary, "  \"counts\": {\n");
    for (int j = 0; j < N_STATUSES; j++) {
        key_int(&summary, 4, STATUSES[j], counts[j], j + 1 < N_STATUSES);
    }
    buf_add(&summary, "  },\n");
    key_str(&summary, 2, "case_logs_root", "build/fuzz100/cases", 1);
    key_str(&summary, 2, "binary_root", "build/fuzz100/bin", 1);
    buf_add(&summary, "  \"cases\": [\n");
    for (int i = 0; i < n; i++) {
        emit_case(&summary, &cases[i], 4);
        if (i + 1 < n)
            buf_add(&summary, ",");
        buf_add(&summary, "\n");
    }
    buf_add(&summary, "  ]\n}\n");

    char *summary_file = path_join(g_out_dir, "summary.json");
    write_whole_file(summary_file, summary.d);
    free(summary_file);

    int exit_code = (counts[1] || counts[2] || counts[3] || counts[5]) ? 1 : 0;

    if (g_json_stdout) {
        fputs(summary.d, stdout);
        return exit_code;
    }

    printf("fuzz100: discovered %d, executed %d\n", n, executed);
    if (g_configs.n > 0) {
        Buf joined;
        buf_init(&joined);
        for (int i = 0; i < g_configs.n; i++) {
            if (i > 0)
                buf_add(&joined, ",");
            buf_add(&joined, g_configs.v[i]);
        }
        printf("fuzz100: filter --config %s\n", joined.d);
        if (!any_match)
            printf("fuzz100: WARNING -- no discovered harness matched the "
                   "--config filter\n");
    }
    if (g_dry_run)
        printf("fuzz100: DRY RUN -- no build or run performed\n");
    if (n == 0)
        printf("fuzz100: NO HARNESS DISCOVERED (corpus may still be being "
               "written); zero cases is not a pass\n");

    char header[256];
    snprintf(header, sizeof(header), "%-8s %-34s %-6s %-5s %-13s %-11s %-7s",
             "config", "case", "kind", "json", "status", "stage", "b/r");
    printf("%s\n", header);
    for (size_t k = 0; k < strlen(header); k++)
        putchar('-');
    putchar('\n');
    for (int i = 0; i < n; i++) {
        Case *c = &cases[i];
        char br[64];
        snprintf(br, sizeof(br), "%s/%s",
                 c->build_command != NULL ? numstr(c->build_rc) : "-",
                 c->run_command != NULL ? numstr(c->run_rc) : "-");
        char name[35];
        snprintf(name, sizeof(name), "%.34s", c->name);
        printf("%-8s %-34s %-6s %-5s %-13s %-11s %-7s\n",
               c->config != NULL ? c->config : "-", name, c->kind,
               c->json_case ? "yes" : "no", c->status,
               c->stage[0] != '\0' ? c->stage : "-", br);
    }
    for (size_t k = 0; k < strlen(header); k++)
        putchar('-');
    putchar('\n');
    printf("fuzz100: ");
    const char *lower[] = {"pass", "fail", "build_failure", "timeout",
                           "skipped", "blocked"};
    for (int j = 0; j < N_STATUSES; j++) {
        if (j > 0)
            printf(", ");
        printf("%s=%d", lower[j], counts[j]);
    }
    printf("\n");
    printf("fuzz100: JSON summary: build/fuzz100/summary.json\n");
    printf("fuzz100: logs: build/fuzz100/cases\n");

    return exit_code;
}

/* Small helper used by the table (defined after main to keep main readable). */
static const char *numstr(int v) {
    static char slots[8][32];
    static int idx = 0;
    char *s = slots[idx++ % 8];
    snprintf(s, 32, "%d", v);
    return s;
}

#endif /* F100_RUN_CP */
