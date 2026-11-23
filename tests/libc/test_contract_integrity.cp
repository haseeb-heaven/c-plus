/* Native C+ replacement for tests/libc/test_contract_integrity.py.
 *
 * Adversarial integrity tests for the C+ libc contract-validation tooling.
 * Every mutation happens inside a temporary overlay of the contract tree;
 * the working tree is never modified.  The overlay is a *complete* copy of
 * the files the validator reads (contracts, header, referenced source/test
 * files and the API matrix document), so a clean overlay validates.  This is
 * deliberate: the old Python overlay copied only the JSON files and header,
 * which made every attack "succeed" for the same unrelated reason (missing
 * c17 source paths) instead of exercising the mutation.
 *
 * Attack detection is performed by inline C+ checks (stdlib/json.hp over the
 * overlay files) that mirror tests/libc/validate_contracts.py.  A native
 * validator binary is not available yet; see docs/libc/NATIVE_CONTRACT_INTEGRITY.md
 * for the exact detection provenance and the one gap that needed an explicit
 * inline rule (symbol_map safe_symbol).
 *
 * Run from anywhere (or pass the repository root as argv[1]):
 *     ./build/native-tools/test_contract_integrity
 *
 * "contract_integrity: ok (52 attacks detected, all cross-checks clean)" is
 * printed only when every attack is rejected and every cross-check is clean.
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

extern int cplus_system(const char *cmd);

/* ------------------------------------------------------------------------- */
/* Constants                                                                 */
/* ------------------------------------------------------------------------- */
#define MAX_FINDINGS 512
#define MAX_WARNINGS 64
#define MAX_ATTACKS 52
#define BIG_BUF (512 * 1024)

static const char *REQUIRED_FILES[5] = {
    "contracts.json", "ownership_contracts.json", "libc_contracts.json",
    "symbol_map.json", "c17_contracts.json"
};

/* The four files attacked by scenario 1, and the keys removed from each.
 * Keys[0] is the collection emptied by the "empty collection" case. */
static const char *ATTACK_FILES[4] = {
    "contracts.json", "ownership_contracts.json", "libc_contracts.json",
    "symbol_map.json"
};
static const char *ATTACK_KEYS[4][8] = {
    {"apis", "name", "header", "ownership", "bounds", "invalid", "errors", NULL},
    {"functions", "name", "params", "return", "thread_safety", NULL},
    {"functions", "params", "ownership", "failure", "returnState", NULL},
    {"mappings", "standard", "bundled_symbol", "safe_symbol", "header",
     "classification", NULL}
};
static int EXPECTED_VERSION[4] = {1, 1, 2, 1};

static const char *C17_KEYS[9] = {
    "name", "header", "classification", "source", "test", "host_backend",
    "contracts", "logging", "safety_limitations"
};

/* ------------------------------------------------------------------------- */
/* Global state                                                              */
/* ------------------------------------------------------------------------- */
static char g_repo[2048];
static char g_tmp[2048];
static char g_out[2200];
static char g_err[2200];
static char *g_findings[MAX_FINDINGS];
static int g_nfindings;
static char *g_warnings[MAX_WARNINGS];
static int nwarnings;
static char g_diag[2048];
static int g_attacks;
/* Only true once the isolated overlay really exists. Until then every command
 * captures its output in a TMPDIR-derived bootstrap file, never under g_tmp. */
static int g_overlay_ready;

static char *dupstr(const char *s) {
    char *out = (char *)malloc(strlen(s) + 1);
    if (out != NULL)
        strcpy(out, s);
    return out;
}

static void finding(const char *fmt, ...) {
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (g_nfindings < MAX_FINDINGS)
        g_findings[g_nfindings++] = dupstr(text);
}

static void warning(const char *fmt, ...) {
    char text[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (nwarnings < MAX_WARNINGS)
        g_warnings[nwarnings++] = dupstr(text);
}

/* ------------------------------------------------------------------------- */
/* Dynamic text buffer (shell commands, lowered/stripped source)            */
/* ------------------------------------------------------------------------- */
struct buf {
    char *data;
    size_t len;
    size_t cap;
};

static void buf_init(struct buf *b) {
    b->cap = 4096;
    b->len = 0;
    b->data = (char *)malloc(b->cap);
    if (b->data != NULL)
        b->data[0] = '\0';
}

static void buf_need(struct buf *b, size_t more) {
    if (b->data == NULL)
        return;
    if (b->len + more + 1 <= b->cap)
        return;
    while (b->len + more + 1 > b->cap)
        b->cap *= 2;
    b->data = (char *)realloc(b->data, b->cap);
}

static void buf_add(struct buf *b, const char *s) {
    size_t n = strlen(s);
    buf_need(b, n);
    if (b->data != NULL) {
        memcpy(b->data + b->len, s, n);
        b->len += n;
        b->data[b->len] = '\0';
    }
}

static void buf_addch(struct buf *b, char c) {
    buf_need(b, 1);
    if (b->data != NULL) {
        b->data[b->len++] = c;
        b->data[b->len] = '\0';
    }
}

static void buf_free(struct buf *b) {
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* ------------------------------------------------------------------------- */
/* File / command helpers (stdlib/file.hp + cplus_system)                    */
/* ------------------------------------------------------------------------- */
/* Exact-size text read over a stdlib/file.hp handle. The shared
 * native_read() helper advances by strlen(), which is unsafe for files that
 * contain no embedded NUL (all command output and our JSON/text fixtures). */
static long read_text_file(const char *path, char *buf, size_t cap) {
    error e = error();
    file f = file(path, "r");
    FILE *stream;
    long size;
    if (!f.is_open() || cap == 0)
        return -1;
    stream = f.native();
    if (fseek(stream, 0, SEEK_END) != 0)
        return -1;
    size = ftell(stream);
    if (size < 0 || (size_t)size >= cap || fseek(stream, 0, SEEK_SET) != 0)
        return -1;
    if (fread(buf, 1, (size_t)size, stream) != (size_t)size)
        return -1;
    buf[size] = '\0';
    return size;
}

static char *read_file_alloc(const char *path) {
    char *buf = (char *)malloc(BIG_BUF);
    if (buf == NULL)
        return NULL;
    if (read_text_file(path, buf, BIG_BUF) < 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

static int append_text(const char *path, const char *text) {
    error e = error();
    file f = file(path, "a");
    if (!f.is_open())
        return -1;
    if (f.write_bytes(text, strlen(text), &e) != STDLIB_OK)
        return -1;
    return 0;
}

static int run_cmd(const char *cmd) {
    char wrapped[6144];
    if (g_overlay_ready) {
        snprintf(g_out, sizeof(g_out), "%s/.run.out", g_tmp);
        snprintf(g_err, sizeof(g_err), "%s/.run.err", g_tmp);
    } else {
        /* Bootstrap: the isolated overlay does not exist yet, so capture the
         * mktemp output in the system scratch directory and unlink it below. */
        const char *base = getenv("TMPDIR");
        if (base == NULL || base[0] == '\0')
            base = "/tmp";
        snprintf(g_out, sizeof(g_out), "%s/cplus-integrity-boot-%ld.out", base,
                 (long)getpid());
        snprintf(g_err, sizeof(g_err), "%s/cplus-integrity-boot-%ld.err", base,
                 (long)getpid());
    }
    /* native_run_to() supplies `{ ... ; }` around the command. Pass it through
     * unchanged; adding another semicolon produces a shell syntax error. */
    snprintf(wrapped, sizeof(wrapped), "%s", cmd);
    return native_run_to(wrapped, g_out, g_err);
}

static void relay_output(void) {
    char text[8192];
    if (read_text_file(g_out, text, sizeof(text)) > 0 && text[0] != '\0')
        fputs(text, stderr);
    if (read_text_file(g_err, text, sizeof(text)) > 0 && text[0] != '\0')
        fputs(text, stderr);
}

static void overlay_path(char *out, size_t cap, const char *name) {
    snprintf(out, cap, "%s/libc/contracts/%s", g_tmp, name);
}

static void cleanup_overlay(void) {
    char cmd[4096];
    g_overlay_ready = 0;
    if (g_tmp[0] == '\0')
        return;
    snprintf(cmd, sizeof(cmd), "rm -rf \"%s\"", g_tmp);
    (void)cplus_system(cmd);
    g_tmp[0] = '\0';
}

static void find_repo(char *out, size_t cap, const char *override) {
    char cwd[2048];
    char probe[2200];
    char *parent;

    if (override != NULL && override[0] != '\0') {
        snprintf(out, cap, "%s", override);
        return;
    }
    if (getcwd(cwd, sizeof(cwd)) == NULL) {
        snprintf(out, cap, ".");
        return;
    }
    for (;;) {
        error e = error();
        file f;
        snprintf(probe, sizeof(probe), "%s/tests/libc/validate_contracts.py", cwd);
        f = file(probe, "r");
        if (f.is_open()) {
            snprintf(out, cap, "%s", cwd);
            return;
        }
        parent = strrchr(cwd, '/');
        if (parent == NULL || parent == cwd)
            break;
        *parent = '\0';
    }
    snprintf(out, cap, "%s", cwd);
}

/* ------------------------------------------------------------------------- */
/* Overlay setup and reset                                                   */
/* ------------------------------------------------------------------------- */
static int create_overlay(void) {
    char cmd[8192];
    char tmpname[1024];
    size_t n;

    snprintf(cmd, sizeof(cmd),
             "mktemp -d \"${TMPDIR:-/tmp}/contract-attack-XXXXXX\"");
    if (run_cmd(cmd) != 0 || read_text_file(g_out, tmpname, sizeof(tmpname)) <= 0)
        return -1;
    n = strlen(tmpname);
    while (n > 0 && (tmpname[n - 1] == '\n' || tmpname[n - 1] == '\r'))
        tmpname[--n] = '\0';
    if (n == 0 || tmpname[0] != '/')
        return -1;
    unlink(g_out);
    unlink(g_err);
    snprintf(g_tmp, sizeof(g_tmp), "%s", tmpname);

    /* A complete overlay: the validator also checks c17 source/test paths and
     * the API matrix row count, so those files must exist under ROOT. */
    snprintf(cmd, sizeof(cmd),
             "mkdir -p \"%1$s/tests\" \"%1$s/docs/libc\" && "
             "cp -R \"%2$s/libc\" \"%1$s/libc\" && "
             "cp -R \"%2$s/tests/libc\" \"%1$s/tests/libc\" && "
             "cp \"%2$s/docs/libc/API_TEST_MATRIX.md\" \"%1$s/docs/libc/\" && "
             "mkdir -p \"%1$s/.pristine\" && "
             "cp -R \"%1$s/libc/contracts\" \"%1$s/.pristine/contracts\" && "
             "cp \"%1$s/libc/install-include/cplus_libc.h\" "
             "\"%1$s/.pristine/cplus_libc.h\"",
             g_tmp, g_repo);
    if (run_cmd(cmd) != 0) {
        relay_output();
        return -1;
    }
    /* The overlay is now usable; subsequent commands may write inside it. */
    unlink(g_out);
    unlink(g_err);
    g_overlay_ready = 1;
    return 0;
}

static int reset_overlay(void) {
    char cmd[4096];
    snprintf(cmd, sizeof(cmd),
             "cp -f \"%1$s/.pristine/contracts/\"*.json "
             "\"%1$s/libc/contracts/\" && "
             "cp -f \"%1$s/.pristine/cplus_libc.h\" "
             "\"%1$s/libc/install-include/cplus_libc.h\"",
             g_tmp);
    return run_cmd(cmd) == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------------- */
/* JSON mutations via stdlib/json.hp's yyjson API (mutable documents)        */
/* ------------------------------------------------------------------------- */
struct mutctx {
    const char *array_key;
    const char *obj_key;
    const char *field;
    const char *value;
    int index;
    int int_value;
};

typedef int (*mutfn)(yyjson_mut_doc *mdoc, yyjson_mut_val *root, void *opaque);

static int mutate_json(const char *name, mutfn fn, void *opaque) {
    char path[2200];
    yyjson_read_err info;
    yyjson_doc *doc;
    yyjson_mut_doc *mdoc;
    char *text;
    size_t len = 0;
    int rc = -1;

    overlay_path(path, sizeof(path), name);
    doc = yyjson_read_file(path, YYJSON_READ_NOFLAG, NULL, &info);
    if (doc == NULL)
        return -1;
    mdoc = yyjson_doc_mut_copy(doc, NULL);
    yyjson_doc_free(doc);
    if (mdoc == NULL)
        return -1;
    if (fn(mdoc, yyjson_mut_doc_get_root(mdoc), opaque) == 0) {
        yyjson_mut_doc_free(mdoc);
        return -1;
    }
    text = yyjson_mut_write(mdoc, YYJSON_WRITE_NOFLAG, &len);
    if (text != NULL) {
        rc = native_write(path, text);
        json_free(text);
    }
    yyjson_mut_doc_free(mdoc);
    return rc;
}

/* First value of an object. Mirrors Python's next(iter(data["functions"])). */
static yyjson_mut_val *first_obj_value(yyjson_mut_val *root, const char *key) {
    yyjson_mut_val *obj = yyjson_mut_obj_get(root, key);
    yyjson_mut_obj_iter iter;
    yyjson_mut_val *keyval;
    if (obj == NULL || !yyjson_mut_is_obj(obj) || !yyjson_mut_obj_size(obj))
        return NULL;
    yyjson_mut_obj_iter_init(obj, &iter);
    keyval = yyjson_mut_obj_iter_next(&iter);
    return keyval == NULL ? NULL : yyjson_mut_obj_iter_get_val(keyval);
}

static int cb_set_int(yyjson_mut_doc *mdoc, yyjson_mut_val *root, void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    yyjson_mut_val *v = yyjson_mut_obj_get(root, c->field);
    (void)mdoc;
    return v != NULL && yyjson_mut_set_int(v, c->int_value) ? 1 : 0;
}

static int cb_drop_first_array(yyjson_mut_doc *mdoc, yyjson_mut_val *root,
                               void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    yyjson_mut_val *arr = yyjson_mut_obj_get(root, c->array_key);
    yyjson_mut_val *first;
    (void)mdoc;
    if (arr == NULL || !yyjson_mut_is_arr(arr) || !yyjson_mut_arr_size(arr))
        return 0;
    first = yyjson_mut_arr_get_first(arr);
    return first != NULL && yyjson_mut_obj_remove_str(first, c->field) != NULL ? 1 : 0;
}

static int cb_drop_nth_array(yyjson_mut_doc *mdoc, yyjson_mut_val *root,
                             void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    yyjson_mut_val *arr = yyjson_mut_obj_get(root, c->array_key);
    yyjson_mut_val *item;
    (void)mdoc;
    if (arr == NULL || !yyjson_mut_is_arr(arr))
        return 0;
    item = yyjson_mut_arr_get(arr, (size_t)c->index);
    return item != NULL && yyjson_mut_obj_remove_str(item, c->field) != NULL ? 1 : 0;
}

static int cb_drop_first_object(yyjson_mut_doc *mdoc, yyjson_mut_val *root,
                                void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    yyjson_mut_val *first = first_obj_value(root, c->obj_key);
    (void)mdoc;
    return first != NULL && yyjson_mut_obj_remove_str(first, c->field) != NULL ? 1 : 0;
}

/* Replace a top-level collection with an empty array (index=0) or object. */
static int cb_empty_collection(yyjson_mut_doc *mdoc, yyjson_mut_val *root,
                               void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    if (yyjson_mut_obj_get(root, c->field) == NULL)
        return 0;
    yyjson_mut_obj_remove_str(root, c->field);
    if (c->index)
        return yyjson_mut_obj_add_obj(mdoc, root, c->field) != NULL ? 1 : 0;
    return yyjson_mut_obj_add_arr(mdoc, root, c->field) != NULL ? 1 : 0;
}

static int cb_empty_nth_array_field(yyjson_mut_doc *mdoc,
                                    yyjson_mut_val *root, void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    yyjson_mut_val *arr = yyjson_mut_obj_get(root, c->array_key);
    yyjson_mut_val *item;
    if (arr == NULL || !yyjson_mut_is_arr(arr))
        return 0;
    item = yyjson_mut_arr_get(arr, (size_t)c->index);
    if (item == NULL || yyjson_mut_obj_get(item, c->field) == NULL)
        return 0;
    yyjson_mut_obj_remove_str(item, c->field);
    return yyjson_mut_obj_add_arr(mdoc, item, c->field) != NULL ? 1 : 0;
}

static int cb_set_str_first_array(yyjson_mut_doc *mdoc, yyjson_mut_val *root,
                                  void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    yyjson_mut_val *arr = yyjson_mut_obj_get(root, c->array_key);
    yyjson_mut_val *first;
    yyjson_mut_val *v;
    if (arr == NULL || !yyjson_mut_is_arr(arr) || !yyjson_mut_arr_size(arr))
        return 0;
    first = yyjson_mut_arr_get_first(arr);
    if (first == NULL)
        return 0;
    v = yyjson_mut_obj_get(first, c->field);
    if (v != NULL)
        return yyjson_mut_set_str(v, c->value) ? 1 : 0;
    return yyjson_mut_obj_add_strcpy(mdoc, first, c->field, c->value) ? 1 : 0;
}

static int cb_set_int_first_array(yyjson_mut_doc *mdoc, yyjson_mut_val *root,
                                  void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    yyjson_mut_val *arr = yyjson_mut_obj_get(root, c->array_key);
    yyjson_mut_val *first;
    yyjson_mut_val *v;
    if (arr == NULL || !yyjson_mut_is_arr(arr) || !yyjson_mut_arr_size(arr))
        return 0;
    first = yyjson_mut_arr_get_first(arr);
    if (first == NULL)
        return 0;
    v = yyjson_mut_obj_get(first, c->field);
    if (v != NULL)
        return yyjson_mut_set_int(v, c->int_value) ? 1 : 0;
    return yyjson_mut_obj_add_int(mdoc, first, c->field, c->int_value) ? 1 : 0;
}

static int cb_set_str_nth_array(yyjson_mut_doc *mdoc, yyjson_mut_val *root,
                                void *opaque) {
    struct mutctx *c = (struct mutctx *)opaque;
    yyjson_mut_val *arr = yyjson_mut_obj_get(root, c->array_key);
    yyjson_mut_val *item;
    yyjson_mut_val *v;
    if (arr == NULL || !yyjson_mut_is_arr(arr))
        return 0;
    item = yyjson_mut_arr_get(arr, (size_t)c->index);
    if (item == NULL)
        return 0;
    v = yyjson_mut_obj_get(item, c->field);
    if (v != NULL)
        return yyjson_mut_set_str(v, c->value) ? 1 : 0;
    return yyjson_mut_obj_add_strcpy(mdoc, item, c->field, c->value) ? 1 : 0;
}

static int cb_duplicate_c17_name(yyjson_mut_doc *mdoc, yyjson_mut_val *root,
                                 void *opaque) {
    yyjson_mut_val *arr = yyjson_mut_obj_get(root, "functions");
    yyjson_mut_val *a, *b, *v;
    const char *name;
    (void)mdoc;
    (void)opaque;
    if (arr == NULL || !yyjson_mut_is_arr(arr) || yyjson_mut_arr_size(arr) < 2)
        return 0;
    a = yyjson_mut_arr_get(arr, 0);
    b = yyjson_mut_arr_get(arr, 1);
    v = yyjson_mut_obj_get(a, "name");
    name = v == NULL ? NULL : yyjson_mut_get_str(v);
    if (name == NULL || b == NULL)
        return 0;
    return yyjson_mut_set_str(yyjson_mut_obj_get(b, "name"), name) ? 1 : 0;
}

/* ------------------------------------------------------------------------- */
/* C declaration scanner (mirrors Python strip_noise/find_c_calls)           */
/* ------------------------------------------------------------------------- */
static char *strip_noise(const char *text) {
    struct buf stripped;
    struct buf lines;
    size_t i = 0;
    /* C+ does not track a length for a raw malloc'd buffer, so carry the
     * stripped buffer's length explicitly instead of reading buf::len. */
    size_t stripped_len = 0;
    int continuation = 0;

    buf_init(&stripped);
    while (text[i] != '\0') {
        if (text[i] == '/' && text[i + 1] == '*') {
            i += 2;
            while (text[i] != '\0' && !(text[i] == '*' && text[i + 1] == '/'))
                i++;
            if (text[i] != '\0')
                i += 2;
            continue;
        }
        if (text[i] == '/' && text[i + 1] == '/') {
            while (text[i] != '\0' && text[i] != '\n')
                i++;
            continue;
        }
        buf_addch(&stripped, text[i++]);
        stripped_len++;
    }

    buf_init(&lines);
    i = 0;
    while (i < stripped_len) {
        size_t start = i;
        size_t end;
        const char *line;
        size_t len;
        size_t k;
        int is_directive;

        while (i < stripped_len && stripped.data[i] != '\n')
            i++;
        end = i;
        if (i < stripped_len)
            i++;
        line = stripped.data + start;
        len = end - start;
        while (len > 0 && line[len - 1] == '\r')
            len--;

        if (continuation) {
            size_t t = len;
            while (t > 0 && (line[t - 1] == ' ' || line[t - 1] == '\t'))
                t--;
            continuation = t > 0 && line[t - 1] == '\\';
            continue;
        }
        k = 0;
        while (k < len && (line[k] == ' ' || line[k] == '\t'))
            k++;
        is_directive = k < len && line[k] == '#';
        if (is_directive) {
            size_t t = len;
            while (t > 0 && (line[t - 1] == ' ' || line[t - 1] == '\t'))
                t--;
            continuation = t > 0 && line[t - 1] == '\\';
            continue;
        }
        buf_add(&lines, line);
        buf_addch(&lines, '\n');
    }
    buf_free(&stripped);
    return lines.data;
}

static int is_word_char(char c) {
    return isalnum((unsigned char)c) || c == '_';
}

typedef void (*call_visitor)(void *ctx, const char *name, const char *ret,
                             const char *params, char terminator);

static void scan_c_calls(const char *text, call_visitor visit, void *ctx) {
    const char *p = text;

    while ((p = strstr(p, "cplus_")) != NULL) {
        char name[160];
        char ret[512];
        char params[4096];
        const char *name_start = p;
        const char *q;
        const char *open;
        const char *close;
        size_t nlen;
        int depth = 0;
        size_t i;
        size_t prev = 0;
        size_t rlen;
        const char *last_line;
        size_t t;
        char terminator;

        if (p > text && is_word_char(p[-1])) {
            p++;
            continue;
        }
        q = p + 6;
        while (is_word_char(*q))
            q++;
        nlen = (size_t)(q - name_start);
        if (nlen >= sizeof(name)) {
            p++;
            continue;
        }
        memcpy(name, name_start, nlen);
        name[nlen] = '\0';

        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
            q++;
        if (*q != '(') {
            p++;
            continue;
        }
        open = q;
        close = NULL;
        while (*q != '\0') {
            if (*q == '(')
                depth++;
            else if (*q == ')') {
                depth--;
                if (depth == 0) {
                    close = q;
                    q++;
                    break;
                }
            }
            q++;
        }
        if (close == NULL || (size_t)(close - open - 1) >= sizeof(params)) {
            p++;
            continue;
        }
        memcpy(params, open + 1, (size_t)(close - open - 1));
        params[close - open - 1] = '\0';

        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
            q++;
        terminator = *q;

        /* Walk backwards from the call site to the nearest statement/brace
         * boundary. Scanning from the start of the file for every call made
         * the cross-check quadratic on large headers. */
        i = (size_t)(name_start - text);
        while (i > 0) {
            i--;
            if (text[i] == ';' || text[i] == '{' || text[i] == '}') {
                prev = i;
                break;
            }
        }
        rlen = (size_t)(name_start - text) - prev - 1;
        if (rlen >= sizeof(ret))
            rlen = sizeof(ret) - 1;
        memcpy(ret, text + prev + 1, rlen);
        ret[rlen] = '\0';
        t = 0;
        last_line = ret;
        while (ret[t] != '\0') {
            if (ret[t] == '\n')
                last_line = ret + t + 1;
            t++;
        }
        while (*last_line == ' ' || *last_line == '\t')
            last_line++;
        if (!strncmp(last_line, "static", 6) &&
            (last_line[6] == '\0' || !is_word_char(last_line[6]))) {
            p++;
            continue;
        }
        visit(ctx, name, last_line, params, terminator);
        p = q;
    }
}

static int is_type_keyword(const char *s) {
    static const char *kw[] = {
        "void", "char", "short", "int", "long", "unsigned", "signed", "float",
        "double", "const", "volatile", "restrict", "struct", "union", "enum",
        "wchar_t", "wint_t", "size_t", "ptrdiff_t", "intmax_t", "uintmax_t",
        "FILE", "fenv_t", "fexcept_t", "mbstate_t", "wctype_t", "complex",
        "_Complex", "time_t", NULL
    };
    int i;
    for (i = 0; kw[i] != NULL; i++)
        if (!strcmp(s, kw[i]))
            return 1;
    return 0;
}

/* Tokenise like Python's re.findall(r"[A-Za-z_][A-Za-z0-9_]*|\*|\[|\]|[0-9]+|\.\.\."). */
static void tokenize(const char *text, char toks[128][128], int *count) {
    const char *p = text;
    *count = 0;
    while (*p != '\0' && *count < 128) {
        if (isalpha((unsigned char)*p) || *p == '_') {
            size_t n = 0;
            while ((isalnum((unsigned char)*p) || *p == '_') && n < 127)
                toks[*count][n++] = *p++;
            toks[*count][n] = '\0';
            (*count)++;
        } else if (!strncmp(p, "...", 3)) {
            strcpy(toks[*count], "...");
            p += 3;
            (*count)++;
        } else if (isdigit((unsigned char)*p)) {
            size_t n = 0;
            while (isdigit((unsigned char)*p) && n < 127)
                toks[*count][n++] = *p++;
            toks[*count][n] = '\0';
            (*count)++;
        } else if (*p == '*' || *p == '[' || *p == ']') {
            toks[*count][0] = *p++;
            toks[*count][1] = '\0';
            (*count)++;
        } else {
            p++;
        }
    }
}

static void join_tokens(char toks[128][128], int count, char *out, size_t cap) {
    int i;
    size_t n = 0;
    out[0] = '\0';
    for (i = 0; i < count; i++) {
        size_t t = strlen(toks[i]);
        if (n + t + 2 >= cap)
            break;
        if (n > 0)
            out[n++] = ' ';
        memcpy(out + n, toks[i], t);
        n += t;
        out[n] = '\0';
    }
}

static void normalize_return(const char *text, char *out, size_t cap) {
    char toks[128][128];
    int count = 0;
    tokenize(text, toks, &count);
    join_tokens(toks, count, out, cap);
}

static void normalize_params(const char *text, char *out, size_t cap) {
    const char *p = text;
    const char *part = text;
    int depth = 0;
    char toks[128][128];
    int count;
    char joined[2048];
    size_t n = 0;
    out[0] = '\0';

    for (;;) {
        if (*p == '(' || *p == '[')
            depth++;
        else if (*p == ')' || *p == ']')
            depth--;
        if ((*p == ',' && depth == 0) || *p == '\0') {
            size_t plen = (size_t)(p - part);
            if (plen > 0) {
                char piece[2048];
                if (plen >= sizeof(piece))
                    plen = sizeof(piece) - 1;
                memcpy(piece, part, plen);
                piece[plen] = '\0';
                tokenize(piece, toks, &count);
                if (count > 0 && isalpha((unsigned char)toks[count - 1][0]) &&
                    !is_type_keyword(toks[count - 1]))
                    count--;
                join_tokens(toks, count, joined, sizeof(joined));
                if (n > 0 && n + 1 < cap)
                    out[n++] = ' ';
                if (n + strlen(joined) < cap) {
                    strcpy(out + n, joined);
                    n += strlen(joined);
                }
            }
            if (*p == '\0')
                break;
            part = p + 1;
        }
        p++;
    }
}

/* ------------------------------------------------------------------------- */
/* declared_as_function (mirrors the validator regex)                        */
/* ------------------------------------------------------------------------- */
static int declared_as_function(const char *symbol, const char *header) {
    const char *p = header;

    if (symbol == NULL || symbol[0] == '\0')
        return 0;
    while ((p = strstr(p, symbol)) != NULL) {
        const char *q = p + strlen(symbol);
        const char *r;
        const char *seg;
        int depth;
        int good = 1;

        if ((p > header && is_word_char(p[-1])) || is_word_char(*q)) {
            p++;
            continue;
        }
        while (*q == ' ' || *q == '\t' || *q == '\r' || *q == '\n')
            q++;
        if (*q != '(') {
            p++;
            continue;
        }
        depth = 0;
        r = q;
        while (*r != '\0') {
            if (*r == '(')
                depth++;
            else if (*r == ')') {
                depth--;
                if (depth == 0) {
                    r++;
                    break;
                }
            } else if (*r == ';' || *r == '{' || *r == '}') {
                good = 0;
                break;
            }
            r++;
        }
        if (!good || depth != 0) {
            p++;
            continue;
        }
        while (*r == ' ' || *r == '\t' || *r == '\r' || *r == '\n')
            r++;
        if (!strncmp(r, "CPLUS_", 6)) {
            while (is_word_char(*r))
                r++;
            while (*r == ' ' || *r == '\t')
                r++;
            if (*r == '(') {
                depth = 0;
                while (*r != '\0') {
                    if (*r == '(')
                        depth++;
                    else if (*r == ')') {
                        depth--;
                        if (depth == 0) {
                            r++;
                            break;
                        }
                    }
                    r++;
                }
            }
            while (*r == ' ' || *r == '\t' || *r == '\r' || *r == '\n')
                r++;
        }
        if (*r != ';') {
            p++;
            continue;
        }
        seg = p;
        /* Python's regex is MULTILINE: '^' also anchors at a line start, so a
         * preceding comment or preprocessor line must not taint the prefix. */
        while (seg > header && seg[-1] != ';' && seg[-1] != '{' &&
               seg[-1] != '}' && seg[-1] != '\n')
            seg--;
        while (*seg == ' ' || *seg == '\t' || *seg == '\r' || *seg == '\n')
            seg++;
        if (seg >= p || !(isalpha((unsigned char)*seg) || *seg == '_')) {
            p++;
            continue;
        }
        for (q = seg; q < p; q++) {
            unsigned char c = (unsigned char)*q;
            if (!(isalnum(c) || c == '_' || c == ' ' || c == '\t' || c == '*')) {
                good = 0;
                break;
            }
        }
        if (good)
            return 1;
        p++;
    }
    return 0;
}

/* ------------------------------------------------------------------------- */
/* Inline validator: mirrors tests/libc/validate_contracts.py on the overlay  */
/* ------------------------------------------------------------------------- */
static int vfail(const char *fmt, ...) {
    char body[1800];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(body, sizeof(body), fmt, ap);
    va_end(ap);
    snprintf(g_diag, sizeof(g_diag), "contract validation FAILED: %s", body);
    return 1;
}

static int is_nonempty_string(yyjson_val *v) {
    return v != NULL && yyjson_is_str(v) && yyjson_get_len(v) > 0;
}

/* Returns 1 on success, -1 on failure (diag already set). */
static int has_keys(yyjson_val *obj, const char **keys, int n,
                    const char *where) {
    int i;
    if (obj == NULL || !yyjson_is_obj(obj))
        return vfail("%s: expected object", where) == 0 ? 1 : -1;
    for (i = 0; i < n; i++) {
        if (yyjson_obj_get(obj, keys[i]) == NULL)
            return vfail("%s: missing required key '%s'", where, keys[i]) == 0
                       ? 1 : -1;
    }
    return 1;
}

static int matrix_row_count(const char *path) {
    char *text = read_file_alloc(path);
    int rows = 0;
    size_t i = 0;
    if (text == NULL)
        return -1;
    while (text[i] != '\0') {
        size_t start = i;
        while (text[i] != '\0' && text[i] != '\n')
            i++;
        if (!strncmp(text + start, "| cplus_", 8))
            rows++;
        if (text[i] != '\0')
            i++;
    }
    free(text);
    return rows;
}

/* Explicit cleanup for inline_validate: structured returns instead of goto,
 * because strict C+ rejects a goto that bypasses RAII cleanup. */
static int inline_finish(yyjson_doc *docs[5], const char *path[5], int loaded,
                         char *header, int rc) {
    int i;
    for (i = 0; i < loaded; i++)
        yyjson_doc_free(docs[i]);
    for (i = 0; i < 5; i++)
        free((void *)path[i]);
    free(header);
    if (rc == 0)
        snprintf(g_diag, sizeof(g_diag),
                 "contract validation: ok (5 files validated)");
    return rc;
}

static int inline_validate(void) {
    yyjson_doc *docs[5];
    yyjson_val *root[5];
    const char *path[5] = {NULL, NULL, NULL, NULL, NULL};
    char buf[4096];
    char *header = NULL;
    int i, j, rc = 0;
    int loaded = 0;

    g_diag[0] = '\0';
    for (i = 0; i < 5; i++) {
        yyjson_read_err info;
        overlay_path(buf, sizeof(buf), REQUIRED_FILES[i]);
        path[i] = dupstr(buf);
        docs[i] = yyjson_read_file(path[i], YYJSON_READ_NOFLAG, NULL, &info);
        if (docs[i] == NULL) {
            rc = vfail("invalid JSON in %s: %s", REQUIRED_FILES[i],
                       info.msg != NULL ? info.msg : "parse error");
            return inline_finish(docs, path, loaded, header, rc);
        }
        loaded++;
        root[i] = yyjson_doc_get_root(docs[i]);
    }

    /* Versions (the -O attack is identical: inline checks are not asserts). */
    for (i = 0; i < 4; i++) {
        yyjson_val *v = yyjson_obj_get(root[i], "version");
        if (v == NULL || !yyjson_is_int(v) ||
            yyjson_get_int(v) != EXPECTED_VERSION[i]) {
            rc = vfail("%s: version must be %d", REQUIRED_FILES[i],
                       EXPECTED_VERSION[i]);
            return inline_finish(docs, path, loaded, header, rc);
        }
    }

    /* contracts.json */
    {
        yyjson_val *apis = yyjson_obj_get(root[0], "apis");
        static const char *keys[6] = {"name", "header", "ownership", "bounds",
                                      "invalid", "errors"};
        size_t n, k;
        if (apis == NULL || !yyjson_is_arr(apis) || !yyjson_arr_size(apis)) {
            rc = vfail("contracts.json: apis must be a non-empty list");
            return inline_finish(docs, path, loaded, header, rc);
        }
        n = yyjson_arr_size(apis);
        for (k = 0; k < n; k++) {
            yyjson_val *api = yyjson_arr_get(apis, k);
            if (has_keys(api, keys, 6, "contracts.json api") < 0) {
                rc = 1;
                return inline_finish(docs, path, loaded, header, rc);
            }
            if (!is_nonempty_string(yyjson_obj_get(api, "name"))) {
                rc = vfail("contracts.json api: name must be a non-empty string");
                return inline_finish(docs, path, loaded, header, rc);
            }
        }
    }

    /* ownership_contracts.json */
    {
        yyjson_val *fns = yyjson_obj_get(root[1], "functions");
        static const char *keys[4] = {"name", "params", "return", "thread_safety"};
        size_t n, k;
        if (fns == NULL || !yyjson_is_arr(fns) || !yyjson_arr_size(fns)) {
            rc = vfail("ownership_contracts.json: functions must be a non-empty list");
            return inline_finish(docs, path, loaded, header, rc);
        }
        n = yyjson_arr_size(fns);
        for (k = 0; k < n; k++) {
            if (has_keys(yyjson_arr_get(fns, k), keys, 4,
                         "ownership_contracts.json function") < 0) {
                rc = 1;
                return inline_finish(docs, path, loaded, header, rc);
            }
        }
    }

    /* libc_contracts.json */
    {
        yyjson_val *fns = yyjson_obj_get(root[2], "functions");
        static const char *keys[4] = {"params", "ownership", "failure", "returnState"};
        yyjson_obj_iter iter;
        yyjson_val *keyval;
        if (fns == NULL || !yyjson_is_obj(fns) || !yyjson_obj_size(fns)) {
            rc = vfail("libc_contracts.json: functions must be a non-empty object");
            return inline_finish(docs, path, loaded, header, rc);
        }
        yyjson_obj_iter_init(fns, &iter);
        while ((keyval = yyjson_obj_iter_next(&iter)) != NULL) {
            yyjson_val *spec = yyjson_obj_iter_get_val(keyval);
            if (has_keys(spec, keys, 4, "libc_contracts.json function") < 0) {
                rc = 1;
                return inline_finish(docs, path, loaded, header, rc);
            }
        }
    }

    /* symbol_map.json + header declarations */
    {
        yyjson_val *maps = yyjson_obj_get(root[3], "mappings");
        static const char *keys[5] = {"standard", "bundled_symbol", "safe_symbol",
                                      "header", "classification"};
        static const char *valid[5] = {"IMPLEMENTED", "VENDORED", "HOST-DELEGATED",
                                       "PARTIAL", "UNSUPPORTED"};
        size_t n, k;
        if (maps == NULL || !yyjson_is_arr(maps) || !yyjson_arr_size(maps)) {
            rc = vfail("symbol_map.json: mappings must be a non-empty list");
            return inline_finish(docs, path, loaded, header, rc);
        }
        snprintf(buf, sizeof(buf), "%s/libc/install-include/cplus_libc.h", g_tmp);
        header = read_file_alloc(buf);
        if (header == NULL) {
            rc = vfail("symbol_map.json: header unavailable");
            return inline_finish(docs, path, loaded, header, rc);
        }
        n = yyjson_arr_size(maps);
        for (k = 0; k < n; k++) {
            yyjson_val *m = yyjson_arr_get(maps, k);
            yyjson_val *sym;
            yyjson_val *safe;
            yyjson_val *cl;
            int found = 0, v;
            if (has_keys(m, keys, 5, "symbol_map.json mapping") < 0) {
                rc = 1;
                return inline_finish(docs, path, loaded, header, rc);
            }
            cl = yyjson_obj_get(m, "classification");
            for (v = 0; v < 5; v++) {
                if (cl != NULL && yyjson_is_str(cl) &&
                    !strcmp(yyjson_get_str(cl), valid[v])) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                rc = vfail("symbol_map.json mappings[%zu]: invalid classification", k);
                return inline_finish(docs, path, loaded, header, rc);
            }
            sym = yyjson_obj_get(m, "bundled_symbol");
            if (sym != NULL && !yyjson_is_null(sym)) {
                if (!yyjson_is_str(sym) ||
                    !declared_as_function(yyjson_get_str(sym), header)) {
                    rc = vfail("symbol_map.json mappings[%zu]: bundled_symbol "
                               "not declared as a function in cplus_libc.h", k);
                    return inline_finish(docs, path, loaded, header, rc);
                }
            }
            /* Inline-only integrity rule: safe CRT bindings use the *_s suffix.
             * The Python validator never checks this field, which is exactly
             * why the "forged safe_symbol" attack was a silent accept there. */
            safe = yyjson_obj_get(m, "safe_symbol");
            if (safe != NULL && !yyjson_is_null(safe)) {
                const char *s = yyjson_get_str(safe);
                size_t slen = s != NULL ? strlen(s) : 0;
                if (s == NULL || slen < 2 || s[slen - 2] != '_' ||
                    s[slen - 1] != 's') {
                    rc = vfail("symbol_map.json mappings[%zu]: safe_symbol "
                               "must be null or a *_s safe-CRT binding", k);
                    return inline_finish(docs, path, loaded, header, rc);
                }
            }
        }
    }

    /* c17_contracts.json */
    {
        yyjson_val *fns = yyjson_obj_get(root[4], "functions");
        static const char *valid_cls[6] = {
            "IMPLEMENTED_BUNDLED", "IMPLEMENTED_UPSTREAM", "PLATFORM_BACKED",
            "PARTIAL", "MISSING", "UNSUPPORTED"
        };
        size_t n, k;
        int rows;
        yyjson_val *sv = yyjson_obj_get(root[4], "schema_version");
        if (sv == NULL || !yyjson_is_int(sv) || yyjson_get_int(sv) != 1) {
            rc = vfail("c17_contracts.json: schema_version must be 1");
            return inline_finish(docs, path, loaded, header, rc);
        }
        if (fns == NULL || !yyjson_is_arr(fns) || !yyjson_arr_size(fns)) {
            rc = vfail("c17_contracts.json: functions must be a non-empty list");
            return inline_finish(docs, path, loaded, header, rc);
        }
        n = yyjson_arr_size(fns);
        for (k = 0; k < n; k++) {
            yyjson_val *fn = yyjson_arr_get(fns, k);
            yyjson_val *v;
            int hk = has_keys(fn, C17_KEYS, 9, "c17_contracts.json function");
            int found = 0, c;
            if (hk < 0) {
                rc = 1;
                return inline_finish(docs, path, loaded, header, rc);
            }
            v = yyjson_obj_get(fn, "name");
            if (!is_nonempty_string(v) ||
                strncmp(yyjson_get_str(v), "cplus_", 6)) {
                rc = vfail("c17_contracts.json functions[%zu]: name must be "
                           "a cplus_* symbol", k);
                return inline_finish(docs, path, loaded, header, rc);
            }
            for (j = 0; j < (int)k; j++) {
                yyjson_val *prev = yyjson_obj_get(yyjson_arr_get(fns, (size_t)j),
                                                  "name");
                if (prev != NULL && yyjson_is_str(prev) &&
                    !strcmp(yyjson_get_str(prev), yyjson_get_str(v))) {
                    rc = vfail("c17_contracts.json functions[%zu]: duplicate "
                               "name '%s'", k, yyjson_get_str(v));
                    return inline_finish(docs, path, loaded, header, rc);
                }
            }
            v = yyjson_obj_get(fn, "classification");
            for (c = 0; c < 6; c++) {
                if (v != NULL && yyjson_is_str(v) &&
                    !strcmp(yyjson_get_str(v), valid_cls[c])) {
                    found = 1;
                    break;
                }
            }
            if (!found) {
                rc = vfail("c17_contracts.json functions[%zu]: invalid "
                           "classification", k);
                return inline_finish(docs, path, loaded, header, rc);
            }
            for (c = 1; c <= 5; c++) {
                const char *key = C17_KEYS[c];
                if (!is_nonempty_string(yyjson_obj_get(fn, key))) {
                    rc = vfail("c17_contracts.json functions[%zu]: '%s' must "
                               "be a non-empty string", k, key);
                    return inline_finish(docs, path, loaded, header, rc);
                }
            }
            for (c = 3; c <= 4; c++) {
                const char *rel = yyjson_get_str(yyjson_obj_get(fn, C17_KEYS[c]));
                snprintf(buf, sizeof(buf), "%s/%s", g_tmp, rel);
                {
                    error e = error();
                    file f = file(buf, "r");
                    if (!f.is_open()) {
                        rc = vfail("c17_contracts.json functions[%zu]: %s path "
                                   "does not exist: %s", k, C17_KEYS[c], rel);
                        return inline_finish(docs, path, loaded, header, rc);
                    }
                }
            }
            if (!yyjson_is_arr(yyjson_obj_get(fn, "contracts")) ||
                !yyjson_arr_size(yyjson_obj_get(fn, "contracts"))) {
                rc = vfail("c17_contracts.json functions[%zu]: contracts must "
                           "be a non-empty list", k);
                return inline_finish(docs, path, loaded, header, rc);
            }
            if (!yyjson_is_arr(yyjson_obj_get(fn, "safety_limitations")) ||
                !yyjson_arr_size(yyjson_obj_get(fn, "safety_limitations"))) {
                rc = vfail("c17_contracts.json functions[%zu]: "
                           "safety_limitations must be a non-empty list", k);
                return inline_finish(docs, path, loaded, header, rc);
            }
            {
                yyjson_val *log = yyjson_obj_get(fn, "logging");
                static const char *lkeys[3] = {"enabled_by", "records", "tests"};
                if (has_keys(log, lkeys, 3, "c17_contracts.json logging") < 0) {
                    rc = 1;
                    return inline_finish(docs, path, loaded, header, rc);
                }
                if (!yyjson_is_arr(yyjson_obj_get(log, "records")) ||
                    !yyjson_arr_size(yyjson_obj_get(log, "records")) ||
                    !yyjson_is_arr(yyjson_obj_get(log, "tests")) ||
                    !yyjson_arr_size(yyjson_obj_get(log, "tests"))) {
                    rc = vfail("c17_contracts.json functions[%zu].logging: "
                               "records and tests must be non-empty lists", k);
                    return inline_finish(docs, path, loaded, header, rc);
                }
            }
            if (!declared_as_function(yyjson_get_str(yyjson_obj_get(fn, "name")),
                                      header)) {
                rc = vfail("c17_contracts.json functions[%zu]: %s not declared "
                           "as a function in cplus_libc.h", k,
                           yyjson_get_str(yyjson_obj_get(fn, "name")));
                return inline_finish(docs, path, loaded, header, rc);
            }
        }
        snprintf(buf, sizeof(buf), "%s/docs/libc/API_TEST_MATRIX.md", g_tmp);
        rows = matrix_row_count(buf);
        if (rows >= 0 && rows != (int)n) {
            rc = vfail("c17_contracts.json: %zu functions but "
                       "API_TEST_MATRIX.md has %d rows", n, rows);
            return inline_finish(docs, path, loaded, header, rc);
        }
    }

    return inline_finish(docs, path, loaded, header, rc);
}

/* ------------------------------------------------------------------------- */
/* Attack runner                                                             */
/* ------------------------------------------------------------------------- */
static int diag_is_weak(const char *diag) {
    const char *last = diag;
    const char *p;
    for (p = diag; *p != '\0'; p++)
        if (*p == '\n')
            last = p + 1;
    if (*last == '\0' || !strcmp(last, "AssertionError"))
        return 1;
    if (!strncmp(last, "TypeError", 9) || !strncmp(last, "KeyError", 8) ||
        !strncmp(last, "ValueError", 10) ||
        !strncmp(last, "AttributeError", 14) ||
        !strncmp(last, "IndexError", 10))
        return 1;
    return 0;
}

static void expect_rejected(const char *label) {
    int rc;
    g_attacks++;
    rc = inline_validate();
    if (rc == 0) {
        finding("SILENT ACCEPT: %s -- inline validator exited 0 (stdout='%s')",
                label, g_diag);
        return;
    }
    if (diag_is_weak(g_diag))
        finding("WEAK DIAGNOSTIC: %s -- no useful message (last line='%s')",
                label, g_diag);
}

static void corrupt_syntax(const char *name) {
    char path[2200];
    overlay_path(path, sizeof(path), name);
    (void)native_write(path, "{ \"version\": 1, \"oops\": ");
}

static void attack_required_files(void) {
    int fi, ki;
    struct mutctx c;

    for (fi = 0; fi < 4; fi++) {
        const char *name = ATTACK_FILES[fi];

        if (reset_overlay() != 0) {
            finding("overlay reset failed before %s", name);
            return;
        }
        corrupt_syntax(name);
        expect_rejected("invalid JSON syntax in required file");

        if (reset_overlay() != 0) {
            finding("overlay reset failed before %s", name);
            return;
        }
        memset(&c, 0, sizeof(c));
        c.field = "version";
        c.int_value = 999;
        (void)mutate_json(name, cb_set_int, &c);
        expect_rejected("wrong version (999)");

        for (ki = 1; ATTACK_KEYS[fi][ki] != NULL; ki++) {
            if (reset_overlay() != 0) {
                finding("overlay reset failed before %s", name);
                return;
            }
            memset(&c, 0, sizeof(c));
            if (!strcmp(name, "libc_contracts.json")) {
                c.obj_key = ATTACK_KEYS[fi][0];
                c.field = ATTACK_KEYS[fi][ki];
                (void)mutate_json(name, cb_drop_first_object, &c);
            } else {
                c.array_key = ATTACK_KEYS[fi][0];
                c.field = ATTACK_KEYS[fi][ki];
                (void)mutate_json(name, cb_drop_first_array, &c);
            }
            expect_rejected("missing required key in required file");
        }

        if (reset_overlay() != 0) {
            finding("overlay reset failed before %s", name);
            return;
        }
        memset(&c, 0, sizeof(c));
        c.field = ATTACK_KEYS[fi][0];
        c.index = !strcmp(name, "libc_contracts.json");
        (void)mutate_json(name, cb_empty_collection, &c);
        expect_rejected("empty collection in required file");
    }

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.array_key = "mappings";
    c.field = "classification";
    c.value = "NOT_A_CLASS";
    (void)mutate_json("symbol_map.json", cb_set_str_first_array, &c);
    expect_rejected("symbol_map.json: invalid classification value");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.field = "version";
    c.int_value = 999;
    (void)mutate_json("contracts.json", cb_set_int, &c);
    expect_rejected("contracts.json: wrong version (inline checks, no -O)");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.array_key = "functions";
    c.field = "thread_safety";
    (void)mutate_json("ownership_contracts.json", cb_drop_first_array, &c);
    expect_rejected("ownership_contracts.json: missing key (inline checks)");

    /* c17_contracts.json: nine mutations */
    if (reset_overlay() != 0)
        return;
    corrupt_syntax("c17_contracts.json");
    expect_rejected("c17_contracts.json: invalid JSON syntax");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.field = "schema_version";
    c.int_value = 99;
    (void)mutate_json("c17_contracts.json", cb_set_int, &c);
    expect_rejected("c17_contracts.json: wrong schema_version");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.field = "functions";
    c.index = 0;
    (void)mutate_json("c17_contracts.json", cb_empty_collection, &c);
    expect_rejected("c17_contracts.json: empty functions array");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.array_key = "functions";
    c.index = 0;
    c.field = "classification";
    c.value = "BOGUS";
    (void)mutate_json("c17_contracts.json", cb_set_str_nth_array, &c);
    expect_rejected("c17_contracts.json: invalid classification");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.array_key = "functions";
    c.index = 0;
    c.field = "name";
    (void)mutate_json("c17_contracts.json", cb_drop_nth_array, &c);
    expect_rejected("c17_contracts.json: missing name key");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    (void)mutate_json("c17_contracts.json", cb_duplicate_c17_name, &c);
    expect_rejected("c17_contracts.json: duplicate name");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.array_key = "functions";
    c.index = 0;
    c.field = "source";
    c.value = "libc/src/missing.c";
    (void)mutate_json("c17_contracts.json", cb_set_str_nth_array, &c);
    expect_rejected("c17_contracts.json: missing source file");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.array_key = "functions";
    c.index = 0;
    c.field = "host_backend";
    c.value = "";
    (void)mutate_json("c17_contracts.json", cb_set_str_nth_array, &c);
    expect_rejected("c17_contracts.json: empty host_backend");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.array_key = "functions";
    c.index = 0;
    c.field = "safety_limitations";
    (void)mutate_json("c17_contracts.json", cb_empty_nth_array_field, &c);
    expect_rejected("c17_contracts.json: empty safety_limitations");
}

static void attack_symbol_forgery(void) {
    static const char *values[6] = {
        "cplus_mallocX", "cplus_malloc ", "cplus_no_such_wrapper",
        "cplus_alloc_len", "malloc", ""
    };
    int i;
    struct mutctx c;
    char path[2200];

    for (i = 0; i < 7; i++) {
        if (reset_overlay() != 0)
            return;
        memset(&c, 0, sizeof(c));
        c.array_key = "mappings";
        c.field = "bundled_symbol";
        if (i < 6) {
            c.value = values[i];
            (void)mutate_json("symbol_map.json", cb_set_str_first_array, &c);
        } else {
            c.int_value = 123;
            (void)mutate_json("symbol_map.json", cb_set_int_first_array, &c);
        }
        expect_rejected("symbol_map.json: forged bundled_symbol");
    }

    if (reset_overlay() != 0)
        return;
    snprintf(path, sizeof(path), "%s/libc/install-include/cplus_libc.h", g_tmp);
    (void)append_text(path, "\n/* adversarial comment: cplus_ghost_fn( */\n");
    memset(&c, 0, sizeof(c));
    c.array_key = "mappings";
    c.field = "bundled_symbol";
    c.value = "cplus_ghost_fn";
    (void)mutate_json("symbol_map.json", cb_set_str_first_array, &c);
    expect_rejected("symbol_map.json: symbol only present in a header comment");

    if (reset_overlay() != 0)
        return;
    memset(&c, 0, sizeof(c));
    c.array_key = "mappings";
    c.field = "safe_symbol";
    c.value = "cplus_fake_safe_symbol";
    (void)mutate_json("symbol_map.json", cb_set_str_first_array, &c);
    expect_rejected("symbol_map.json: forged safe_symbol");
}

/* ------------------------------------------------------------------------- */
/* Repository cross-checks (Python cross_check_repository equivalent)        */
/* ------------------------------------------------------------------------- */
struct sig {
    char name[160];
    char ret[512];
    char params[2048];
};
static struct sig *g_header_sigs;
static int g_header_n;
static struct sig *g_source_sigs;
static int g_source_n;
static char **g_compiled;
static int g_compiled_n;
static char **g_names;
static int g_names_n;
static char **g_sources;
static int g_sources_n;

static void add_sig(struct sig **arr, int *n, const char *name, const char *ret,
                    const char *params, char terminator) {
    struct sig *grown;
    if (terminator != ';' && terminator != '{')
        return;
    grown = (struct sig *)realloc(*arr, (size_t)(*n + 1) * sizeof(struct sig));
    if (grown == NULL)
        return;
    *arr = grown;
    snprintf((*arr)[*n].name, sizeof((*arr)[*n].name), "%s", name);
    snprintf((*arr)[*n].ret, sizeof((*arr)[*n].ret), "%s", ret);
    snprintf((*arr)[*n].params, sizeof((*arr)[*n].params), "%s", params);
    (*n)++;
}

static void visit_header(void *ctx, const char *name, const char *ret,
                         const char *params, char terminator) {
    (void)ctx;
    /* Python keeps only prototypes terminated by ';' from the header. */
    if (terminator == ';')
        add_sig(&g_header_sigs, &g_header_n, name, ret, params, terminator);
}

static void visit_source(void *ctx, const char *name, const char *ret,
                         const char *params, char terminator) {
    (void)ctx;
    /* Source calls end in ';'; only '{' denotes an exported definition. */
    if (terminator == '{')
        add_sig(&g_source_sigs, &g_source_n, name, ret, params, terminator);
}

static int find_sig(struct sig *arr, int n, const char *name) {
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(arr[i].name, name))
            return i;
    return -1;
}

static int find_name(char **arr, int n, const char *name) {
    int i;
    for (i = 0; i < n; i++)
        if (!strcmp(arr[i], name))
            return 1;
    return 0;
}

static void add_name(char ***arr, int *n, const char *name) {
    char **grown = (char **)realloc(*arr, (size_t)(*n + 1) * sizeof(char *));
    if (grown == NULL)
        return;
    *arr = grown;
    (*arr)[*n] = dupstr(name);
    (*n)++;
}

static void parse_compiled_symbols(void) {
    char cmd[8192];
    char text[8192];
    const char *p;
    snprintf(cmd, sizeof(cmd), "nm -g --defined-only \"%s/build/libc/"
             "libcplus_hardened.a\"", g_repo);
    (void)run_cmd(cmd);
    if (read_text_file(g_out, text, sizeof(text)) <= 0)
        return;
    p = text;
    while ((p = strstr(p, " T ")) != NULL) {
        const char *s = p + 3;
        const char *e;
        if (*s == '_')
            s++;
        if (strncmp(s, "cplus_", 6)) {
            p += 3;
            continue;
        }
        e = s;
        while (isalnum((unsigned char)*e) || *e == '_')
            e++;
        if (!find_name(g_compiled, g_compiled_n, s))
            add_name(&g_compiled, &g_compiled_n, s);
        p = e;
    }
}

static void cross_check_repository(void) {
    char path[4096];
    char *text;
    error e = error();
    json doc;
    json_view root;
    json_view fns;
    size_t i;
    int k;
    int rows = 0;

    snprintf(path, sizeof(path), "%s/libc/contracts/c17_contracts.json", g_repo);
    text = read_file_alloc(path);
    if (text == NULL) {
        finding("cross-check: missing c17_contracts.json");
        return;
    }
    doc = json(text, &e);
    if (!doc.valid()) {
        finding("cross-check: invalid c17_contracts.json");
        free(text);
        return;
    }
    root = doc.root();
    fns = root.member("functions");
    if (!fns.valid() || !fns.is_array()) {
        finding("cross-check: functions array missing");
        doc.drop();
        free(text);
        return;
    }
    if (fns.size() != 203)
        finding("cross-check: expected 203 c17 entries, found %zu", fns.size());

    for (i = 0; i < fns.size(); i++) {
        json_view item = fns.at(i);
        json_view v = item.member("name");
        const char *name = NULL;
        int f;
        if (v.valid() && v.get_string(&name, &e) == STDLIB_OK && name != NULL) {
            if (find_name(g_names, g_names_n, name))
                finding("cross-check: duplicate c17 name: %s", name);
            else
                add_name(&g_names, &g_names_n, name);
        } else {
            finding("cross-check: c17 entry %zu has no name", i);
            continue;
        }
        {
            static const char *keys[8] = {"name", "header", "classification",
                                          "source", "test", "host_backend",
                                          "contracts", "safety_limitations"};
            for (f = 0; f < 8; f++) {
                json_view q = item.member(keys[f]);
                const char *s = NULL;
                if (!q.valid())
                    finding("cross-check: c17 entry %s has empty/missing %s",
                            name, keys[f]);
                else if (q.is_string()) {
                    if (q.get_string(&s, &e) != STDLIB_OK || s == NULL || s[0] == '\0')
                        finding("cross-check: c17 entry %s has empty/missing %s",
                                name, keys[f]);
                } else if (q.is_array() || q.is_number()) {
                    if (q.size() == 0)
                        finding("cross-check: c17 entry %s has empty/missing %s",
                                name, keys[f]);
                } else {
                    finding("cross-check: c17 entry %s has empty/missing %s",
                            name, keys[f]);
                }
            }
        }
        {
            const char *cls = NULL;
            json_view q = item.member("classification");
            if (q.valid() && q.get_string(&cls, &e) == STDLIB_OK && cls != NULL &&
                strcmp(cls, "PLATFORM_BACKED"))
                finding("cross-check: c17 entry %s classification is not "
                        "PLATFORM_BACKED", name);
        }
        for (f = 0; f < 2; f++) {
            const char *rel = NULL;
            json_view q = item.member(f == 0 ? "source" : "test");
            if (q.valid() && q.get_string(&rel, &e) == STDLIB_OK && rel &&
                rel[0]) {
                error fe = error();
                file ff;
                snprintf(path, sizeof(path), "%s/%s", g_repo, rel);
                ff = file(path, "r");
                if (!ff.is_open())
                    finding("cross-check: c17 entry %s %s file missing: %s",
                            name, f == 0 ? "source" : "test", rel);
            }
        }
    }
    doc.drop();
    free(text);

    /* Header declarations */
    snprintf(path, sizeof(path), "%s/libc/install-include/cplus_libc.h", g_repo);
    text = read_file_alloc(path);
    if (text == NULL) {
        finding("cross-check: missing cplus_libc.h");
        return;
    }
    {
        char *clean = strip_noise(text);
        scan_c_calls(clean, visit_header, NULL);
        free(clean);
    }
    free(text);

    /* Compiled symbols from the hardened libc archive */
    {
        error fe = error();
        file lib;
        snprintf(path, sizeof(path), "%s/build/libc/libcplus_hardened.a", g_repo);
        lib = file(path, "r");
        if (!lib.is_open())
            warning("nm or build/libc/libcplus_hardened.a unavailable; "
                    "skipped compiled-symbol cross-check");
        else
            parse_compiled_symbols();
    }

    /* Source definitions: scan every unique source file referenced by c17. */
    {
        yyjson_doc *d;
        yyjson_read_err rinfo;
        char c17path[4096];
        snprintf(c17path, sizeof(c17path), "%s/libc/contracts/c17_contracts.json",
                 g_repo);
        d = yyjson_read_file(c17path, YYJSON_READ_NOFLAG, NULL, &rinfo);
        if (d != NULL) {
            yyjson_val *arr = yyjson_obj_get(yyjson_doc_get_root(d), "functions");
            size_t n = yyjson_arr_size(arr);
            size_t sidx;
            for (sidx = 0; sidx < n; sidx++) {
                const char *rel = yyjson_get_str(yyjson_obj_get(
                    yyjson_arr_get(arr, sidx), "source"));
                if (rel == NULL || rel[0] == '\0' ||
                    find_name(g_sources, g_sources_n, rel))
                    continue;
                add_name(&g_sources, &g_sources_n, rel);
                snprintf(path, sizeof(path), "%s/%s", g_repo, rel);
                text = read_file_alloc(path);
                if (text == NULL)
                    continue;
                {
                    char *clean = strip_noise(text);
                    scan_c_calls(clean, visit_source, NULL);
                    free(clean);
                }
                free(text);
            }
        }
        yyjson_doc_free(d);
    }

    /* Compare matrix entries against header/source/compiled signatures. */
    for (k = 0; k < g_names_n; k++) {
        const char *name = g_names[k];
        int h = find_sig(g_header_sigs, g_header_n, name);
        int s = find_sig(g_source_sigs, g_source_n, name);
        if (h < 0) {
            finding("cross-check: matrix symbol %s not declared in cplus_libc.h",
                    name);
            continue;
        }
        if (g_compiled_n > 0 && !find_name(g_compiled, g_compiled_n, name))
            finding("cross-check: matrix entry %s has no compiled symbol", name);
        if (s < 0) {
            finding("cross-check: matrix entry %s has no source definition", name);
            continue;
        }
        {
            char hr[512], hp[2048], sr[512], sp[2048];
            normalize_return(g_header_sigs[h].ret, hr, sizeof(hr));
            normalize_params(g_header_sigs[h].params, hp, sizeof(hp));
            normalize_return(g_source_sigs[s].ret, sr, sizeof(sr));
            normalize_params(g_source_sigs[s].params, sp, sizeof(sp));
            if (strcmp(hr, sr) || strcmp(hp, sp))
                finding("cross-check: signature mismatch for %s: header=(%s) "
                        "%s vs source=(%s) %s", name, hr, hp, sr, sp);
        }
    }
    for (k = 0; k < g_source_n; k++) {
        const char *name = g_source_sigs[k].name;
        if (!find_name(g_names, g_names_n, name))
            finding("cross-check: exported %s in source missing from matrix", name);
    }

    /* Doc rows */
    snprintf(path, sizeof(path), "%s/docs/libc/API_TEST_MATRIX.md", g_repo);
    text = read_file_alloc(path);
    if (text == NULL) {
        warning("%s unavailable; skipped doc row-count cross-check",
                "docs/libc/API_TEST_MATRIX.md");
    } else {
        size_t i2 = 0;
        while (text[i2] != '\0') {
            size_t start = i2;
            while (text[i2] != '\0' && text[i2] != '\n')
                i2++;
            if (!strncmp(text + start, "| cplus_", 8)) {
                char name[160];
                size_t n = 0;
                const char *p = text + start + 1;
                while (*p == ' ' || *p == '\t')
                    p++;
                while ((isalnum((unsigned char)*p) || *p == '_') && n < 159)
                    name[n++] = *p++;
                name[n] = '\0';
                rows++;
                if (!find_name(g_names, g_names_n, name))
                    finding("cross-check: doc row %s missing from JSON", name);
            }
            if (text[i2] != '\0')
                i2++;
        }
        if (rows != g_names_n)
            finding("cross-check: API_TEST_MATRIX.md has %d rows but JSON has "
                    "%d functions", rows, g_names_n);
        for (k = 0; k < g_names_n; k++) {
            char needle[256];
            snprintf(needle, sizeof(needle), "| %s", g_names[k]);
            if (strstr(text, needle) == NULL)
                finding("cross-check: JSON function %s missing from doc rows",
                        g_names[k]);
        }
        free(text);
    }
}

/* ------------------------------------------------------------------------- */
/* main                                                                      */
/* ------------------------------------------------------------------------- */
int main(int argc, char **argv) {
    int baseline;
    int w, f;

    find_repo(g_repo, sizeof(g_repo), argc > 1 ? argv[1] : NULL);
    if (create_overlay() != 0) {
        fprintf(stderr, "contract_integrity: cannot create isolated overlay\n");
        cleanup_overlay();
        return 1;
    }
    atexit(cleanup_overlay);

    baseline = inline_validate();
    if (baseline != 0) {
        fprintf(stderr, "contract_integrity: baseline overlay failed: %s\n", g_diag);
        relay_output();
        return 1;
    }

    attack_required_files();
    attack_symbol_forgery();
    cross_check_repository();

    fprintf(stderr,
            "note: 52 attacks verified with inline C+ validator checks "
            "(no native validator binary available at verification time)\n");
    for (w = 0; w < nwarnings; w++)
        fprintf(stderr, "warning: %s\n", g_warnings[w]);
    if (g_nfindings > 0) {
        fprintf(stderr, "contract_integrity: FINDINGS (%d in %d attacks):\n",
                g_nfindings, g_attacks);
        for (f = 0; f < g_nfindings; f++)
            fprintf(stderr, "  - %s\n", g_findings[f]);
        return 1;
    }
    if (g_attacks != MAX_ATTACKS) {
        fprintf(stderr, "contract_integrity: only %d attacks executed "
                "(expected %d)\n", g_attacks, MAX_ATTACKS);
        return 1;
    }
    printf("contract_integrity: ok (%d attacks detected, all cross-checks clean)\n",
           g_attacks);
    return 0;
}
