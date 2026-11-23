/* Native C+ replacement for tests/libc/test_contract_consumption.py.
 *
 * Reports which [CPLUS-SAFE-*] libc diagnostic categories in
 * libc/contracts/compiler_libc_contracts.json are actually *consumed* by the
 * compiler, i.e. for which categories real `build/c+ check --strict` output
 * contains the literal tag.  A tag found only in a schema string, a test
 * fixture, or a different diagnostic family is a CAPABILITY GAP, never
 * compiler consumption.
 *
 * Method (mirrors the Python report):
 *   1. Parse the contract with stdlib/json.hp (yyjson), derive exact
 *      denominators and diagnostic categories.
 *   2. Optionally record which test files literally reference each tag.
 *   3. Run five fixed unsafe snippets via cplus_system() (the hardened libc
 *      system() wrapper from tests/native/cplus_test_native.hp) with stdout
 *      and stderr redirected to files, then scan the captured output for
 *      literal [CPLUS-...] tags.
 *   4. Write a machine-readable summary to build/native/contract_consumption.json
 *      using stdlib/file.hp and manual JSON escaping, print a compact JSON
 *      summary on stdout, and exit non-zero ONLY on execution failure.
 *
 * Capability gaps (missing tags) are reported as "GAP" / "capability_gap"
 * and do NOT change the exit status.  The Python script's FROZEN_BASELINE
 * drift check is intentionally NOT reproduced: a real compiler-behaviour
 * change must be reviewed by a human, not fail this evidence report.
 *
 * Build and run from the repository root:
 *   ./build/c+ build tests/libc/test_contract_consumption.cp --strict \
 *     --sanitize=none -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *     -Lbuild/libc -lcplus_hardened -o build/native/contract_consumption_test
 *   ./build/native/contract_consumption_test
 */

#include <ctype.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/json.hp"
#include "tests/native/cplus_test_native.hp"

#define CC_SCHEMA_VERSION 1
#define CC_CONTRACT "libc/contracts/compiler_libc_contracts.json"
#define CC_COMPILER "build/c+"
#define CC_COMPILER_CONTRACT "compiler/Analysis/libc_contracts.json"
#define CC_OUT_DIR "build/native"
#define CC_PROBE_DIR "build/native/probes"
#define CC_SUMMARY "build/native/contract_consumption.json"

#define CC_MAX_APIS 128
#define CC_MAX_CATS 32
#define CC_MAX_DIAGS 512
#define CC_MAX_PROBES 8
#define CC_NAME 128
#define CC_TAG 64
#define CC_DIAG 512
#define CC_LINE 4096
#define CC_BUF 65536

typedef struct {
    char tag[CC_TAG];
    int diagnostic_count;
    char api_names[CC_MAX_APIS][CC_NAME];
    int api_count;
    int probe_index;
    int emitted;
    char verdict[32];
    int capability_gap;
    int test_reference_exists;
    char refs_json[8192]; /* contents of a JSON string array, no brackets */
} cc_cat;

typedef struct {
    char name[CC_NAME];
    int diag_total;
    int cat_indices[CC_MAX_CATS];
    int cat_count;
} cc_api;

typedef struct {
    char api[CC_NAME];
    char diag[CC_DIAG];
    int tagged;
} cc_diag;

typedef struct {
    const char *category;
    const char *source_api;
    const char *snippet;
    const char *code;
} cc_probe_spec;

typedef struct {
    char category[CC_TAG];
    char source_api[CC_NAME];
    char snippet[CC_NAME];
    char declared[CC_DIAG];
    int declared_found;
    int status; /* 0 not_run, 1 run, 2 compiler_unavailable, 3 compiler_error */
    int returncode;
    char tags[CC_MAX_CATS][CC_TAG];
    int tag_count;
    int expected_tag_emitted;
    int rejected;
    char tagged_lines[8][CC_LINE];
    int tagged_line_count;
} cc_probe;

/* The same five categories/snippets as the Python report. */
static const cc_probe_spec cc_specs[5] = {
    {"[CPLUS-SAFE-FREE]", "free", "free_double_release.c",
     "#include <stdlib.h>\n"
     "int main(void) {\n"
     "    int *p = (int *)malloc(sizeof(int));\n"
     "    if (!p) return 1;\n"
     "    free(p);\n"
     "    free(p);\n"
     "    return 0;\n"
     "}\n"},
    {"[CPLUS-SAFE-LIFETIME]", "free", "use_after_free.c",
     "#include <stdlib.h>\n"
     "int main(void) {\n"
     "    int *p = (int *)malloc(sizeof(int));\n"
     "    if (!p) return 1;\n"
     "    free(p);\n"
     "    *p = 7;\n"
     "    return 0;\n"
     "}\n"},
    {"[CPLUS-SAFE-BOUNDS]", "memcpy", "constant_out_of_bounds.c",
     "int main(void) {\n"
     "    int a[3] = {1, 2, 3};\n"
     "    a[5] = 99;\n"
     "    return a[5];\n"
     "}\n"},
    {"[CPLUS-SAFE-CRT]", "strcpy", "string_copy_overflow.c",
     "#include <string.h>\n"
     "int main(void) {\n"
     "    char dst[4];\n"
     "    strcpy(dst, \"overflow-long\");\n"
     "    return 0;\n"
     "}\n"},
    {"[CPLUS-SAFE-REALLOC]", "realloc", "realloc_stale_alias.c",
     "#include <stdlib.h>\n"
     "int main(void) {\n"
     "    int *p = (int *)malloc(sizeof(int) * 4);\n"
     "    if (!p) return 1;\n"
     "    int *q = (int *)realloc(p, sizeof(int) * 8);\n"
     "    if (!q) return 1;\n"
     "    p[0] = 9;\n"
     "    free(q);\n"
     "    return 0;\n"
     "}\n"},
};
#define CC_N_SPECS ((int)(sizeof(cc_specs) / sizeof(cc_specs[0])))

static cc_cat g_cats[CC_MAX_CATS];
static int g_cat_count = 0;
static int g_cat_order[CC_MAX_CATS];
static cc_api g_apis[CC_MAX_APIS];
static int g_api_count = 0;
static cc_diag g_diags[CC_MAX_DIAGS];
static int g_diag_count = 0;
static char g_diag_api_names[CC_MAX_APIS][CC_NAME];
static int g_diag_api_count = 0;
static int g_diag_strings_total = 0;
static cc_probe g_probes[CC_MAX_PROBES];
static int g_probe_count = 0;
static char g_esc[CC_BUF];

/* ------------------------------------------------------------------ */
/* small utilities                                                     */
/* ------------------------------------------------------------------ */

static void cc_append(char *dst, size_t cap, const char *fmt, ...) {
    size_t n = strlen(dst);
    va_list ap;
    if (n >= cap) return;
    va_start(ap, fmt);
    (void)vsnprintf(dst + n, cap - n, fmt, ap);
    va_end(ap);
}

static const char *cc_esc(const char *s) {
    native_json_escape(s != (const char *)0 ? s : "", g_esc, sizeof(g_esc));
    return g_esc;
}

static void cc_str_copy(char *dst, size_t cap, const char *src) {
    if (cap == 0) return;
    (void)snprintf(dst, cap, "%s", src != (const char *)0 ? src : "");
}

static int cc_cat_find(const char *tag) {
    int i;
    for (i = 0; i < g_cat_count; i++)
        if (!strcmp(g_cats[i].tag, tag)) return i;
    return -1;
}

static int cc_api_find(const char *name) {
    int i;
    for (i = 0; i < g_api_count; i++)
        if (!strcmp(g_apis[i].name, name)) return i;
    return -1;
}

static void cc_cat_add_api(int ci, const char *name) {
    int i;
    for (i = 0; i < g_cats[ci].api_count; i++)
        if (!strcmp(g_cats[ci].api_names[i], name)) return;
    if (g_cats[ci].api_count < CC_MAX_APIS) {
        cc_str_copy(g_cats[ci].api_names[g_cats[ci].api_count], CC_NAME, name);
        g_cats[ci].api_count++;
    }
}

static void cc_api_add_cat(int ai, int ci) {
    int i;
    for (i = 0; i < g_apis[ai].cat_count; i++)
        if (g_apis[ai].cat_indices[i] == ci) return;
    if (g_apis[ai].cat_count < CC_MAX_CATS) {
        g_apis[ai].cat_indices[g_apis[ai].cat_count] = ci;
        g_apis[ai].cat_count++;
    }
}

static void cc_sort_strings(char arr[][CC_NAME], int n) {
    int i, j;
    for (i = 1; i < n; i++) {
        char tmp[CC_NAME];
        cc_str_copy(tmp, CC_NAME, arr[i]);
        j = i - 1;
        while (j >= 0 && strcmp(arr[j], tmp) > 0) {
            cc_str_copy(arr[j + 1], CC_NAME, arr[j]);
            j--;
        }
        cc_str_copy(arr[j + 1], CC_NAME, tmp);
    }
}

static void cc_sort_cats(void) {
    int i, j;
    for (i = 0; i < g_cat_count; i++) g_cat_order[i] = i;
    for (i = 1; i < g_cat_count; i++) {
        int tmp = g_cat_order[i];
        j = i - 1;
        while (j >= 0 && strcmp(g_cats[g_cat_order[j]].tag, g_cats[tmp].tag) > 0) {
            g_cat_order[j + 1] = g_cat_order[j];
            j--;
        }
        g_cat_order[j + 1] = tmp;
    }
}

/* TAG_RE:  \[CPLUS-SAFE-[A-Z]+\] */
static int cc_find_safe_tag(const char *s, char *out, size_t cap) {
    const char *p = strstr(s, "[CPLUS-SAFE-");
    const char *q;
    char body[CC_TAG];
    size_t n = 0;
    if (p == (const char *)0) return 0;
    q = p + 12; /* length of "[CPLUS-SAFE-" */
    while (*q >= 'A' && *q <= 'Z') {
        if (n + 1 < sizeof(body)) body[n++] = *q;
        q++;
    }
    if (n == 0 || *q != ']') return 0;
    body[n] = '\0';
    (void)snprintf(out, cap, "[CPLUS-SAFE-%s]", body);
    return 1;
}

/* ANY_TAG_RE:  \[CPLUS-[A-Z][A-Z0-9_-]*\] . Returns pointer after the tag. */
static const char *cc_find_any_tag(const char *from, char *out, size_t cap) {
    const char *p = from;
    for (;;) {
        const char *q;
        char body[CC_TAG];
        size_t n = 0;
        p = strstr(p, "[CPLUS-");
        if (p == (const char *)0) return (const char *)0;
        q = p + 7; /* length of "[CPLUS-" */
        if (*q >= 'A' && *q <= 'Z') {
            while (*q && ((*q >= 'A' && *q <= 'Z') || (*q >= '0' && *q <= '9') ||
                          *q == '_' || *q == '-')) {
                if (n + 1 < sizeof(body)) body[n++] = *q;
                q++;
            }
            if (*q == ']' && n > 0) {
                body[n] = '\0';
                (void)snprintf(out, cap, "[CPLUS-%s]", body);
                return q + 1;
            }
        }
        p += 1;
    }
}

static void cc_probe_add_tag(cc_probe *pr, const char *tag) {
    int i;
    for (i = 0; i < pr->tag_count; i++)
        if (!strcmp(pr->tags[i], tag)) return;
    if (pr->tag_count < CC_MAX_CATS) {
        cc_str_copy(pr->tags[pr->tag_count], CC_TAG, tag);
        pr->tag_count++;
    }
    if (!strcmp(tag, pr->category)) pr->expected_tag_emitted = 1;
}

static void cc_trim(char *dst, size_t cap, const char *src) {
    size_t a = 0, b;
    while (src[a] == ' ' || src[a] == '\t' || src[a] == '\r' || src[a] == '\n') a++;
    b = strlen(src);
    while (b > a && (src[b - 1] == ' ' || src[b - 1] == '\t' ||
                     src[b - 1] == '\r' || src[b - 1] == '\n')) b--;
    if (b > a && b - a >= cap) b = a + cap - 1;
    if (b <= a) {
        dst[0] = '\0';
        return;
    }
    (void)snprintf(dst, cap, "%.*s", (int)(b - a), src + a);
}

/* ------------------------------------------------------------------ */
/* contract parsing                                                    */
/* ------------------------------------------------------------------ */

static const char *cc_family_of(const char *name) {
    char base[CC_NAME];
    size_t len;
    cc_str_copy(base, CC_NAME, name);
    len = strlen(base);
    if (len >= 2 && base[len - 2] == '_' && base[len - 1] == 's') base[len - 2] = '\0';
    if (strstr(base, "alloc") != (char *)0) return "allocation";
    if (!strcmp(base, "memcpy") || !strcmp(base, "memmove") || !strcmp(base, "memset") ||
        !strcmp(base, "memcmp") || !strcmp(base, "memccpy")) return "memory";
    if (!strncmp(base, "str", 3) || strstr(base, "strcpy") != (char *)0 ||
        strstr(base, "strcat") != (char *)0) return "string";
    if (strstr(base, "printf") != (char *)0 || strstr(base, "scanf") != (char *)0) return "format";
    if (!strcmp(base, "fopen") || !strcmp(base, "fclose") || !strcmp(base, "fread") ||
        !strcmp(base, "fwrite") || !strcmp(base, "fflush") || !strcmp(base, "fseek")) return "stream";
    return "other";
}

static int cc_parse_contract(void) {
    error e = error();
    json doc = json_parse_file(CC_CONTRACT, &e);
    json_view root;
    json_view apis;
    size_t i, n;

    if (!doc.valid()) {
        fprintf(stderr, "execution failure: cannot parse contract %s\n", CC_CONTRACT);
        return 0;
    }

    root = doc.root();
    apis = root.member("apis");
    n = apis.size();
    if (n > CC_MAX_APIS) n = CC_MAX_APIS;

    for (i = 0; i < n; i++) {
        json_view av = apis.at(i);
        json_view name_view;
        const char *api_name = (const char *)0;
        json_view diags;
        size_t j, nd;
        int ai;
        /* The C+ rewriter lowers methods on a named receiver, not on the
         * temporary returned by member()/at(), so use explicit locals. */
        name_view = av.member("name");
        if (name_view.get_string(&api_name, &e) != STDLIB_OK || api_name == (const char *)0)
            continue;
        ai = cc_api_find(api_name);
        if (ai < 0 && g_api_count < CC_MAX_APIS) {
            ai = g_api_count++;
            cc_str_copy(g_apis[ai].name, CC_NAME, api_name);
        }
        diags = av.member("diagnostics");
        nd = diags.size();
        if (nd > (size_t)(CC_MAX_DIAGS - g_diag_count)) nd = (size_t)(CC_MAX_DIAGS - g_diag_count);
        for (j = 0; j < nd; j++) {
            json_view diag_view;
            const char *diag = (const char *)0;
            char tag[CC_TAG];
            diag_view = diags.at(j);
            if (diag_view.get_string(&diag, &e) != STDLIB_OK || diag == (const char *)0)
                continue;
            if (g_diag_count >= CC_MAX_DIAGS) break;
            cc_str_copy(g_diags[g_diag_count].api, CC_NAME, api_name);
            cc_str_copy(g_diags[g_diag_count].diag, CC_DIAG, diag);
            if (cc_find_safe_tag(diag, tag, sizeof(tag))) {
                int ci = cc_cat_find(tag);
                g_diags[g_diag_count].tagged = 1;
                if (ci < 0 && g_cat_count < CC_MAX_CATS) {
                    ci = g_cat_count++;
                    cc_str_copy(g_cats[ci].tag, CC_TAG, tag);
                    g_cats[ci].probe_index = -1;
                }
                if (ci >= 0) {
                    g_cats[ci].diagnostic_count++;
                    cc_cat_add_api(ci, api_name);
                }
                if (ai >= 0) cc_api_add_cat(ai, ci);
            } else {
                g_diags[g_diag_count].tagged = 0;
            }
            if (ai >= 0) g_apis[ai].diag_total++;
            g_diag_count++;
        }
    }

    g_diag_strings_total = g_diag_count;
    for (i = 0; i < (size_t)g_api_count; i++) {
        if (g_apis[i].diag_total > 0 && g_diag_api_count < CC_MAX_APIS) {
            cc_str_copy(g_diag_api_names[g_diag_api_count], CC_NAME, g_apis[i].name);
            g_diag_api_count++;
        }
    }
    cc_sort_strings(g_diag_api_names, g_diag_api_count);
    for (i = 0; i < (size_t)g_cat_count; i++)
        cc_sort_strings(g_cats[i].api_names, g_cats[i].api_count);
    cc_sort_cats();
    doc.drop();
    return 1;
}

/* Family counts are recomputed by the emitter via cc_family_of over g_apis. */

/* ------------------------------------------------------------------ */
/* helper: declared diagnostic lookup                                  */
/* ------------------------------------------------------------------ */

static void cc_find_declared(cc_probe *pr) {
    int i;
    for (i = 0; i < g_diag_count; i++) {
        if (strcmp(g_diags[i].api, pr->source_api)) continue;
        if (strstr(g_diags[i].diag, pr->category) != (char *)0) {
            cc_str_copy(pr->declared, CC_DIAG, g_diags[i].diag);
            pr->declared_found = 1;
            return;
        }
    }
    pr->declared_found = 0;
}

/* ------------------------------------------------------------------ */
/* probe execution (cplus_system with redirected stdout/stderr)        */
/* ------------------------------------------------------------------ */

static int cc_run_probe(cc_probe *pr, int index) {
    char snippet_path[512];
    char out_path[512];
    char err_path[512];
    char cmd[2048];
    char wrapped[3072];
    char out_buf[CC_BUF];
    char err_buf[CC_BUF];
    char combined[CC_BUF];
    const char *cursor;
    const char *line;
    int rc;

    if (access(CC_COMPILER, F_OK) != 0) {
        pr->status = 2; /* compiler_unavailable -> GAP */
        return 1;
    }

    (void)snprintf(snippet_path, sizeof(snippet_path), "%s/%s", CC_PROBE_DIR, pr->snippet);
    (void)snprintf(out_path, sizeof(out_path), "%s/probe_%d.out", CC_PROBE_DIR, index);
    (void)snprintf(err_path, sizeof(err_path), "%s/probe_%d.err", CC_PROBE_DIR, index);

    if (native_write(snippet_path, cc_specs[index].code) != 0) {
        pr->status = 3;
        return 0; /* execution failure */
    }

    (void)snprintf(cmd, sizeof(cmd), "%s check --strict \"%s\"", CC_COMPILER, snippet_path);
    /* Braces need surrounding spaces and a terminating ';' to form a valid
     * POSIX shell group; the shared native_run_to() helper does not do this,
     * so build the redirection command here while still using cplus_system. */
    (void)snprintf(wrapped, sizeof(wrapped), "{ %s ; } >\"%s\" 2>\"%s\"", cmd, out_path, err_path);
    rc = cplus_system(wrapped);

    /* native_read() measures each fread with strlen(), so the destination
     * buffers MUST be zero-filled: otherwise trailing bytes left by an earlier
     * probe are counted as part of the new file and leak stale diagnostics. */
    memset(out_buf, 0, sizeof(out_buf));
    memset(err_buf, 0, sizeof(err_buf));
    if (native_read(out_path, out_buf, sizeof(out_buf)) < 0 ||
        native_read(err_path, err_buf, sizeof(err_buf)) < 0) {
        pr->status = 3;
        return 0; /* execution failure */
    }
    (void)snprintf(combined, sizeof(combined), "%s%s", err_buf, out_buf);

    pr->returncode = rc;
    pr->rejected = rc != 0;
    pr->status = 1; /* run */

    /* Collect unique literal [CPLUS-...] tags in first-appearance order. */
    cursor = combined;
    for (;;) {
        char tag[CC_TAG];
        const char *after = cc_find_any_tag(cursor, tag, sizeof(tag));
        if (after == (const char *)0) break;
        cc_probe_add_tag(pr, tag);
        cursor = after;
    }

    /* Record up to 8 stripped lines that contain a literal tag. */
    line = combined;
    while (*line != '\0' && pr->tagged_line_count < 8) {
        char next[CC_LINE];
        const char *nl = strchr(line, '\n');
        char check[CC_TAG];
        if (nl == (const char *)0) {
            cc_str_copy(next, CC_LINE, line);
            line += strlen(line);
        } else {
            size_t len = (size_t)(nl - line);
            if (len >= sizeof(next)) len = sizeof(next) - 1;
            (void)snprintf(next, sizeof(next), "%.*s", (int)len, line);
            line = nl + 1;
        }
        if (cc_find_any_tag(next, check, sizeof(check)) != (const char *)0) {
            char stripped[CC_LINE];
            cc_trim(stripped, sizeof(stripped), next);
            cc_str_copy(pr->tagged_lines[pr->tagged_line_count], CC_LINE, stripped);
            pr->tagged_line_count++;
        }
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* verdicts and headline rollups                                       */
/* ------------------------------------------------------------------ */

static void cc_finalize(void) {
    int i;
    for (i = 0; i < g_cat_count; i++) {
        int pi = g_cats[i].probe_index;
        if (pi >= 0 && g_probes[pi].expected_tag_emitted) {
            cc_str_copy(g_cats[i].verdict, 32, "compiler_consumption");
            g_cats[i].capability_gap = 0;
            g_cats[i].emitted = 1;
        } else if (pi >= 0 || g_cats[i].test_reference_exists) {
            cc_str_copy(g_cats[i].verdict, 32, "schema_alignment_only");
            g_cats[i].capability_gap = 1;
            g_cats[i].emitted = 0;
        } else {
            cc_str_copy(g_cats[i].verdict, 32, "no_probe");
            g_cats[i].capability_gap = 1;
            g_cats[i].emitted = 0;
        }
    }
}

static int cc_headline_int(const char *key) {
    /* Headline values are computed inline by the emitters; this helper keeps
     * the single-source-of-truth requirement visible. */
    if (!strcmp(key, "contract_apis")) return g_api_count;
    if (!strcmp(key, "apis_with_diagnostics")) return g_diag_api_count;
    if (!strcmp(key, "apis_without_diagnostics")) return g_api_count - g_diag_api_count;
    if (!strcmp(key, "diagnostic_categories")) return g_cat_count;
    return 0;
}

/* ------------------------------------------------------------------ */
/* JSON emission helpers                                               */
/* ------------------------------------------------------------------ */

static void cc_emit_str_array(char *dst, size_t cap, char arr[][CC_NAME], int n) {
    int i;
    cc_append(dst, cap, "[");
    for (i = 0; i < n; i++) {
        if (i) cc_append(dst, cap, ",");
        cc_append(dst, cap, "\"%s\"", cc_esc(arr[i]));
    }
    cc_append(dst, cap, "]");
}

/* Build the JSON value for a probe's declared diagnostic. */
static void cc_declared_json(char *out, size_t cap, cc_probe *pr) {
    if (pr->declared_found)
        (void)snprintf(out, cap, "\"%s\"", cc_esc(pr->declared));
    else
        cc_str_copy(out, cap, "null");
}

int main(void) {
    char summary[CC_BUF];
    int i, j;
    int probes_run = 0, probes_with_tag = 0, probes_rejected = 0;
    int cat_consumed = 0, cat_gap = 0;
    int apis_with_output = 0, apis_schema_only = 0, apis_mixed = 0;
    int exec_failure = 0;

    summary[0] = '\0';

    if (!cc_parse_contract()) return 2; /* execution failure */

    /* Prepare output directory and per-tag test reference scan. */
    {
        char cmd[4096];
        (void)cplus_system("mkdir -p " CC_OUT_DIR " " CC_PROBE_DIR);
        for (i = 0; i < g_cat_count; i++) {
            int idx = g_cat_order[i];
            char refs_path[512];
            char refs_buf[16384];
            char esc[CC_NAME * 4 + 128];
            char linebuf[CC_NAME * 4 + 128];
            size_t k;
            int first = 1;
            (void)snprintf(refs_path, sizeof(refs_path), "%s/refs_%d.txt", CC_OUT_DIR, idx);
            (void)snprintf(cmd, sizeof(cmd),
                           "find tests/stdlib/compile_time tests/compile_fail "
                           "-type f \\( -name '*.json' -o -name '*.cp' -o -name '*.c' "
                           "-o -name '*.h' -o -name '*.hp' -o -name '*.err' -o -name '*.txt' "
                           "-o -name '*.md' -o -name '*.log' \\) -print0 2>/dev/null "
                           "| xargs -0 grep -lF '%s' 2>/dev/null > \"%s\"",
                           g_cats[idx].tag, refs_path);
            (void)cplus_system(cmd);
            memset(refs_buf, 0, sizeof(refs_buf)); /* native_read uses strlen */
            if (native_read(refs_path, refs_buf, sizeof(refs_buf)) >= 0 && refs_buf[0] != '\0')
                g_cats[idx].test_reference_exists = 1;
            g_cats[idx].refs_json[0] = '\0';
            linebuf[0] = '\0';
            for (k = 0; refs_buf[k] != '\0'; k++) {
                if (refs_buf[k] == '\n') {
                    if (linebuf[0] != '\0') {
                        native_json_escape(linebuf, esc, sizeof(esc));
                        cc_append(g_cats[idx].refs_json, sizeof(g_cats[idx].refs_json),
                                  "%s\"%s\"", first ? "" : ",", esc);
                        first = 0;
                        linebuf[0] = '\0';
                    }
                } else {
                    size_t lb = strlen(linebuf);
                    if (lb + 1 < sizeof(linebuf)) {
                        linebuf[lb] = refs_buf[k];
                        linebuf[lb + 1] = '\0';
                    }
                }
            }
            if (linebuf[0] != '\0') {
                native_json_escape(linebuf, esc, sizeof(esc));
                cc_append(g_cats[idx].refs_json, sizeof(g_cats[idx].refs_json),
                          "%s\"%s\"", first ? "" : ",", esc);
            }
        }
    }

    /* Run probes. */
    for (i = 0; i < CC_N_SPECS; i++) {
        cc_probe *pr = &g_probes[g_probe_count];
        int ci;
        cc_str_copy(pr->category, CC_TAG, cc_specs[i].category);
        cc_str_copy(pr->source_api, CC_NAME, cc_specs[i].source_api);
        cc_str_copy(pr->snippet, CC_NAME, cc_specs[i].snippet);
        cc_find_declared(pr);
        if (!cc_run_probe(pr, i)) exec_failure = 1;
        g_probe_count++;
        ci = cc_cat_find(pr->category);
        if (ci >= 0) g_cats[ci].probe_index = g_probe_count - 1;
        if (pr->status == 1) probes_run++;
        if (pr->expected_tag_emitted) probes_with_tag++;
        if (pr->rejected) probes_rejected++;
    }

    cc_finalize();

    for (i = 0; i < g_cat_count; i++) {
        if (!strcmp(g_cats[i].verdict, "compiler_consumption")) cat_consumed++;
        else cat_gap++;
    }

    /* Per-API rollups (project category evidence onto APIs that declare it). */
    for (i = 0; i < g_api_count; i++) {
        int emitted = 0, missing = 0;
        for (j = 0; j < g_apis[i].cat_count; j++) {
            int ci = g_apis[i].cat_indices[j];
            if (ci >= 0 && g_cats[ci].emitted) emitted++;
            else missing++;
        }
        if (emitted == 0 && missing == 0) continue;
        if (emitted && missing) apis_mixed++;
        else if (emitted) apis_with_output++;
        else apis_schema_only++;
    }

    /* ---------------- machine-readable report ---------------- */
    cc_append(summary, sizeof(summary),
              "{\n  \"schema_version\": %d,\n"
              "  \"purpose\": \"native C+ evidence of compiler consumption of libc diagnostic contracts\",\n"
              "  \"implementation\": \"stdlib/json.hp + cplus_system + stdlib/file.hp\",\n"
              "  \"branch_context\": {\n"
              "    \"branch\": \"agent/modern-stdlib\",\n"
              "    \"compiler_binary\": \"%s\",\n"
              "    \"compiler_contract_file\": \"%s\",\n"
              "    \"compiler_contract_file_present\": %d,\n"
              "    \"interpretation\": \"A category is compiler consumed ONLY when the exact tag appears in real output of build/c+ check --strict. Strings in the binary, schema files, or test fixtures are not evidence.\"\n"
              "  },\n",
              CC_SCHEMA_VERSION, CC_COMPILER, CC_COMPILER_CONTRACT,
              (int)(access(CC_COMPILER_CONTRACT, F_OK) == 0));

    cc_append(summary, sizeof(summary), "  \"contract_counts\": {\n"
              "    \"contract_apis\": %d,\n"
              "    \"apis_with_diagnostics\": %d,\n"
              "    \"apis_without_diagnostics\": %d,\n"
              "    \"apis_with_diagnostics_names\": ",
              cc_headline_int("contract_apis"), cc_headline_int("apis_with_diagnostics"),
              cc_headline_int("apis_without_diagnostics"));
    cc_emit_str_array(summary, sizeof(summary), g_diag_api_names, g_diag_api_count);
    cc_append(summary, sizeof(summary), ",\n    \"diagnostic_strings_total\": %d,\n"
              "    \"diagnostic_categories\": %d,\n"
              "    \"untagged_diagnostic_strings\": [",
              g_diag_strings_total, g_cat_count);
    {
        int first = 1;
        for (i = 0; i < g_diag_count; i++) {
            char e_api[CC_NAME * 4 + 64];
            char e_diag[CC_DIAG * 4 + 64];
            if (g_diags[i].tagged) continue;
            native_json_escape(g_diags[i].api, e_api, sizeof(e_api));
            native_json_escape(g_diags[i].diag, e_diag, sizeof(e_diag));
            cc_append(summary, sizeof(summary),
                      "%s{\"api\":\"%s\",\"diagnostic\":\"%s\"}",
                      first ? "" : ",", e_api, e_diag);
            first = 0;
        }
    }
    cc_append(summary, sizeof(summary), "],\n    \"by_category\": {");
    for (i = 0; i < g_cat_count; i++) {
        int idx = g_cat_order[i];
        cc_append(summary, sizeof(summary), "%s\n      \"%s\": {\"api_count\":%d,\"diagnostic_count\":%d,\"api_names\":",
                  i ? "," : "", cc_esc(g_cats[idx].tag), g_cats[idx].api_count,
                  g_cats[idx].diagnostic_count);
        cc_emit_str_array(summary, sizeof(summary), g_cats[idx].api_names, g_cats[idx].api_count);
        cc_append(summary, sizeof(summary), "}%s", i == g_cat_count - 1 ? "\n    }" : "");
    }
    if (g_cat_count == 0) cc_append(summary, sizeof(summary), "}");
    cc_append(summary, sizeof(summary), ",\n    \"by_family_derived\": {");
    {
        static const char *fam_names[] = {"allocation", "checked_variant", "format",
                                          "memory", "other", "stream", "string"};
        int first = 1;
        for (i = 0; i < 7; i++) {
            int cnt = 0;
            for (j = 0; j < g_api_count; j++)
                if (!strcmp(cc_family_of(g_apis[j].name), fam_names[i])) cnt++;
            if (cnt == 0) continue;
            cc_append(summary, sizeof(summary), "%s\"%s\":%d", first ? "" : ",",
                      fam_names[i], cnt);
            first = 0;
        }
    }
    cc_append(summary, sizeof(summary), "}\n  },\n");

    /* categories */
    cc_append(summary, sizeof(summary), "  \"categories\": {");
    for (i = 0; i < g_cat_count; i++) {
        int idx = g_cat_order[i];
        cc_cat *cat = &g_cats[idx];
        cc_append(summary, sizeof(summary), "%s\n    \"%s\": {\n"
                  "      \"api_count\": %d,\n      \"diagnostic_count\": %d,\n"
                  "      \"api_names\": ",
                  i ? "," : "", cc_esc(cat->tag), cat->api_count, cat->diagnostic_count);
        cc_emit_str_array(summary, sizeof(summary), cat->api_names, cat->api_count);
        cc_append(summary, sizeof(summary),
                  ",\n      \"test_reference_exists\": %d,\n"
                  "      \"test_files_referencing_tag\": [%s],\n"
                  "      \"compiler_probe_index\": %d,\n"
                  "      \"compiler_tagged_output\": %d,\n"
                  "      \"verdict\": \"%s\",\n"
                  "      \"capability_gap\": %d\n    }",
                  cat->test_reference_exists, cat->refs_json,
                  cat->probe_index, cat->emitted,
                  cc_esc(cat->verdict), cat->capability_gap);
    }
    cc_append(summary, sizeof(summary), "\n  },\n");

    /* probes */
    cc_append(summary, sizeof(summary), "  \"probes\": [");
    for (i = 0; i < g_probe_count; i++) {
        cc_probe *pr = &g_probes[i];
        char decl_json[CC_DIAG * 4 + 64];
        char e_cat[CC_TAG * 4 + 64];
        char e_api[CC_NAME * 4 + 64];
        char e_snip[CC_NAME * 4 + 64];
        /* cc_esc() uses one shared scratch buffer, so never call it more than
         * once per cc_append(); escape each value into its own local first. */
        cc_declared_json(decl_json, sizeof(decl_json), pr);
        native_json_escape(pr->category, e_cat, sizeof(e_cat));
        native_json_escape(pr->source_api, e_api, sizeof(e_api));
        native_json_escape(pr->snippet, e_snip, sizeof(e_snip));
        cc_append(summary, sizeof(summary), "%s\n    {\n"
                  "      \"category\": \"%s\",\n"
                  "      \"source_api\": \"%s\",\n"
                  "      \"snippet_file\": \"%s\",\n"
                  "      \"probe_kind\": \"%s\",\n"
                  "      \"declared_diagnostic\": %s,\n"
                  "      \"declared_diagnostic_found\": %d,\n"
                  "      \"status\": %d,\n"
                  "      \"returncode\": %d,\n"
                  "      \"rejected\": %d,\n"
                  "      \"tagged_diagnostics\": [",
                  i ? "," : "", e_cat, e_api, e_snip, e_snip,
                  decl_json, pr->declared_found,
                  pr->status, pr->returncode, pr->rejected);
        for (j = 0; j < pr->tag_count; j++)
            cc_append(summary, sizeof(summary), "%s\"%s\"", j ? "," : "", cc_esc(pr->tags[j]));
        cc_append(summary, sizeof(summary), "],\n      \"tagged_lines\": [");
        for (j = 0; j < pr->tagged_line_count; j++)
            cc_append(summary, sizeof(summary), "%s\"%s\"", j ? "," : "", cc_esc(pr->tagged_lines[j]));
        cc_append(summary, sizeof(summary), "],\n      \"expected_tag_emitted\": %d\n    }",
                  pr->expected_tag_emitted);
    }
    cc_append(summary, sizeof(summary), "\n  ],\n");

    /* gaps */
    cc_append(summary, sizeof(summary), "  \"gaps\": {\n    \"count\": %d,\n"
              "    \"categories\": [", cat_gap);
    {
        int first = 1;
        for (i = 0; i < g_cat_count; i++) {
            int idx = g_cat_order[i];
            if (!g_cats[idx].capability_gap) continue;
            cc_append(summary, sizeof(summary), "%s\"%s\"", first ? "" : ",",
                      cc_esc(g_cats[idx].tag));
            first = 0;
        }
    }
    cc_append(summary, sizeof(summary), "]\n  },\n");

    /* headline (same denominators as the Python report) */
    cc_append(summary, sizeof(summary),
              "  \"headline\": {\n"
              "    \"contract_apis\": %d,\n"
              "    \"apis_with_diagnostics\": \"%d/%d\",\n"
              "    \"apis_without_diagnostics\": \"%d/%d\",\n"
              "    \"diagnostic_categories\": %d,\n"
              "    \"categories_with_compiler_consumption_evidence\": \"%d/%d\",\n"
              "    \"categories_schema_alignment_only\": \"%d/%d\",\n"
              "    \"categories_without_probe\": \"0/%d\",\n"
              "    \"apis_with_any_tagged_output\": \"%d/%d\",\n"
              "    \"apis_schema_alignment_only\": \"%d/%d\",\n"
              "    \"apis_mixed_evidence\": \"%d/%d\",\n"
              "    \"probes_run\": \"%d/%d\",\n"
              "    \"probes_with_expected_tag\": \"%d/%d\",\n"
              "    \"probes_rejected_under_any_tag\": \"%d/%d\"\n"
              "  },\n"
              "  \"status\": \"%s\"\n}\n",
              g_api_count, g_diag_api_count, g_api_count,
              g_api_count - g_diag_api_count, g_api_count,
              g_cat_count, cat_consumed, g_cat_count, cat_gap, g_cat_count,
              g_cat_count, apis_with_output + apis_mixed, g_diag_api_count,
              apis_schema_only, g_diag_api_count, apis_mixed, g_diag_api_count,
              probes_run, g_probe_count, probes_with_tag, g_probe_count,
              probes_rejected, g_probe_count,
              exec_failure ? "execution_failure" : "ok");

    if (native_write(CC_SUMMARY, summary) != 0) {
        fprintf(stderr, "execution failure: cannot write %s\n", CC_SUMMARY);
        return 2;
    }

    /* ---------------- compact stdout summary ---------------- */
    {
        char compact[8192];
        compact[0] = '\0';
        cc_append(compact, sizeof(compact),
                  "{\"status\":\"%s\",\"contract_apis\":%d,"
                  "\"apis_with_diagnostics\":\"%d/%d\","
                  "\"diagnostic_categories\":%d,"
                  "\"categories_with_compiler_consumption\":\"%d/%d\","
                  "\"categories_gap\":\"%d/%d\","
                  "\"probes_run\":\"%d/%d\","
                  "\"probes_with_expected_tag\":\"%d/%d\","
                  "\"gaps\":[",
                  exec_failure ? "execution_failure" : "ok", g_api_count,
                  g_diag_api_count, g_api_count, g_cat_count,
                  cat_consumed, g_cat_count, cat_gap, g_cat_count,
                  probes_run, g_probe_count, probes_with_tag, g_probe_count);
        {
            int first = 1;
            for (i = 0; i < g_cat_count; i++) {
                int idx = g_cat_order[i];
                if (!g_cats[idx].capability_gap) continue;
                cc_append(compact, sizeof(compact), "%s\"%s\"", first ? "" : ",",
                          cc_esc(g_cats[idx].tag));
                first = 0;
            }
        }
        cc_append(compact, sizeof(compact), "]}\n");
        fputs(compact, stdout);
    }

    return exec_failure ? 2 : 0;
}
