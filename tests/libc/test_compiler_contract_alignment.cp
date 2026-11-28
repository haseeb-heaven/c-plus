/* Native C+ port of tests/libc/test_compiler_contract_alignment.py.
 *
 * Loads libc/contracts/compiler_libc_contracts.json and verifies that it still
 * matches the key set, API names and CPLUS-SAFE-* diagnostic categories
 * requested by Agent 1 in issue #22, plus the additive P0 integration fields
 * (contract_semantics, implementation_symbols, extended P0 semantics).
 *
 * Porting notes
 * -------------
 * - Every assertion is explicit (never a bare assert) so a release/optimised
 *   build cannot silently disable the checks, mirroring the Python original.
 * - File access uses stdlib/file.hp (RAII handle); JSON parsing and typed value
 *   access use stdlib/json.hp (json / json_view / json_view_of, backed by
 *   yyjson).  yyjson is used directly only for object-key enumeration, which
 *   json_view deliberately does not expose.
 * - Success prints exactly:  compiler contract alignment: ok
 * - Any failure prints "compiler contract alignment FAILED: ..." to stderr and
 *   exits non-zero (1).  No fabricated passes: a check that cannot run fails.
 *
 * Build (yyjson flags MUST be three separate argv operands):
 *   ./build/c+ build tests/libc/test_compiler_contract_alignment.cp \
 *       -Ithird_party/yyjson -Lbuild/ext -lyyjson \
 *       -o build/test_compiler_contract_alignment
 * Run from the repository root (optional argv[1] overrides the contract path):
 *   ./build/test_compiler_contract_alignment
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/file.hp"
#include "stdlib/json.hp"

#define CONTRACT_PATH "libc/contracts/compiler_libc_contracts.json"
#define CONTRACT_CAP (1 << 20) /* JSON is ~86 KiB; 1 MiB leaves headroom. */

static char g_contract_buf[CONTRACT_CAP + 1];
static char g_msg[2048];
static error *g_err;            /* points at the error owned by run_checks() */
static size_t g_contract_len;
static yyjson_doc *g_raw;

static const char *API_KEYS[] = {
    "name", "params", "ownership", "transfer", "extent",
    "effects", "failure", "diagnostics", "dynamic"
};
/* Counts are constants, not macros: a cast inside a macro expansion cannot be
 * instrumented in STRICT. */
static const int N_API_KEYS = (int)(sizeof(API_KEYS) / sizeof(API_KEYS[0]));

static const char *PARAM_KEYS[] = {"name", "type", "nullable", "extent"};
static const int N_PARAM_KEYS =
    (int)(sizeof(PARAM_KEYS) / sizeof(PARAM_KEYS[0]));

static const char *FAILURE_KEYS[] = {"result_code", "conditions"};
static const int N_FAILURE_KEYS =
    (int)(sizeof(FAILURE_KEYS) / sizeof(FAILURE_KEYS[0]));

static const char *DYNAMIC_KEYS[] = {"checks", "limitations"};
static const int N_DYNAMIC_KEYS =
    (int)(sizeof(DYNAMIC_KEYS) / sizeof(DYNAMIC_KEYS[0]));

/* Original 29-name baseline. */
static const char *REQUIRED_NAMES[] = {
    "malloc", "calloc", "realloc", "free",
    "memcpy", "memmove", "memset", "memcmp",
    "strcpy", "strncpy", "strcat", "strncat", "strlen", "strnlen",
    "strcmp", "strncmp", "strchr", "strstr",
    "printf", "fprintf", "snprintf", "sprintf", "scanf", "fscanf", "sscanf",
    "fopen", "fclose", "fread", "fwrite"
};
static const int N_REQUIRED =
    (int)(sizeof(REQUIRED_NAMES) / sizeof(REQUIRED_NAMES[0]));

/* Expanded P0 coverage (must be a subset of the names present in the JSON). */
static const char *P0_REQUIRED_NAMES[] = {
    "memcpy", "memmove", "memset", "memcmp",
    "strlen", "strnlen", "strcpy", "strncpy", "strcat", "strncat",
    "strcmp", "strncmp", "snprintf", "vsnprintf",
    "malloc_s", "calloc_s", "realloc_s", "free_s", "aligned_alloc_s"
};
static const int N_P0 =
    (int)(sizeof(P0_REQUIRED_NAMES) / sizeof(P0_REQUIRED_NAMES[0]));

/* The six additive entries that must carry explicit semantic evidence. */
static const char *EXTENDED_P0_NAMES[] = {
    "vsnprintf", "malloc_s", "calloc_s", "realloc_s", "free_s",
    "aligned_alloc_s"
};
static const int N_EXTENDED =
    (int)(sizeof(EXTENDED_P0_NAMES) / sizeof(EXTENDED_P0_NAMES[0]));

/* Checked allocators keep their own names in implementation_symbols. */
static const char *CHECKED_ALLOCATORS[] = {
    "malloc_s", "calloc_s", "realloc_s", "free_s", "aligned_alloc_s"
};
static const int N_CHECKED =
    (int)(sizeof(CHECKED_ALLOCATORS) / sizeof(CHECKED_ALLOCATORS[0]));

/* Exactly the five categories from Agent 1's issue #22 comment. */
static const char *ALLOWED_DIAGNOSTICS[] = {
    "[CPLUS-SAFE-FREE] double free of <var>",
    "[CPLUS-SAFE-LIFETIME] use-after-free",
    "[CPLUS-SAFE-BOUNDS] copy of N bytes exceeds destination <var>",
    "[CPLUS-SAFE-CRT] checked string copy needs N bytes but capacity is M",
    "[CPLUS-SAFE-REALLOC] realloc warnings"
};
static const int N_ALLOWED_DIAG =
    (int)(sizeof(ALLOWED_DIAGNOSTICS) / sizeof(ALLOWED_DIAGNOSTICS[0]));

static const char *SEMANTIC_ASPECTS[] = {
    "argument_types", "read_write_behavior", "buffer_sizes",
    "source_dest_relationships", "nullability",
    "ownership_acquisition_release", "borrowed_args",
    "returned_pointer_lifetime", "allocation_behavior", "error_behavior",
    "reallocation_semantics"
};
static const int N_ASPECTS =
    (int)(sizeof(SEMANTIC_ASPECTS) / sizeof(SEMANTIC_ASPECTS[0]));

/* Marker groups (all lowercase) for EXTENDED_TEXT_MARKERS in the Python test.
 * Each group is NULL-terminated; ASPECT_MARKER_NAMES[i] names its aspect. */
static const char *MARKER_READ_WRITE[] = {"writes", "reads", "allocates", "frees", NULL};
static const char *MARKER_SRC_DST[] = {"dest", "source", "src", "ptr", "source/destination", NULL};
static const char *MARKER_BORROWED[] = {"borrow", "no source/destination", "ptr", NULL};
static const char *MARKER_RET_LIFETIME[] = {
    "owned by the caller", "caller-owned", "returned-pointer lifetime",
    "invalidated", "no allocation, reallocation", NULL
};
static const char *MARKER_ALLOCATION[] = {"allocat", "no allocation", NULL};
static const char *MARKER_REALLOC[] = {"realloc", "invalidat", NULL};

static const char *const *MARKER_GROUPS[] = {
    MARKER_READ_WRITE, MARKER_SRC_DST, MARKER_BORROWED,
    MARKER_RET_LIFETIME, MARKER_ALLOCATION, MARKER_REALLOC
};
static const char *ASPECT_MARKER_NAMES[] = {
    "read_write_behavior", "source_dest_relationships", "borrowed_args",
    "returned_pointer_lifetime", "allocation_behavior", "reallocation_semantics"
};
static const int N_MARKER_GROUPS =
    (int)(sizeof(MARKER_GROUPS) / sizeof(MARKER_GROUPS[0]));

/* Seen contract names and the value backing each (collected while scanning apis). */
static const char *g_seen[256];
static int g_seen_n = 0;
static const char *g_api_names[256];
static yyjson_val *g_api_vals[256];
static int g_api_n = 0;

/* ---- failure / set helpers --------------------------------------------- */

/* Checks record the failure message and return a non-zero status instead of
 * terminating: no owner is live at a terminating path.  main() is the only
 * caller of fail(), which it reaches after every owner has left scope. */
static void fail(const char *msg) {
    fprintf(stderr, "compiler contract alignment FAILED: %s\n", msg);
    exit(1);
}

#define FAIL(...) do { snprintf(g_msg, sizeof(g_msg), __VA_ARGS__); return 1; } while (0)

static int in_set(const char *s, const char *const *set, int n) {
    /* `items` is a local alias of the caller-owned, NUL-terminated pointer
     * table; the caller passes both the table and its element count. */
    const char *const *items = set;
    int i;
    if (s == NULL || items == NULL || n <= 0)
        return 0;
    for (i = 0; i < n; i++)
        if (strcmp(s, items[i]) == 0)
            return 1;
    return 0;
}

static int has_nonspace(const char *s) {
    if (s == NULL)
        return 0;
    while (*s != '\0') {
        if (!isspace((unsigned char)*s))
            return 1;
        s++;
    }
    return 0;
}

static void list_add(char *buf, size_t cap, const char *s) {
    /* snprintf() is a fortified macro on this platform: a dynamic
     * `buf + len` or a caller-supplied capacity in its argument list cannot
     * be instrumented in STRICT.  Render the fragment in a fixed-size
     * temporary with a literal format, then append it with an explicitly
     * computed room.  Both callers use a 1024-byte destination. */
    char piece[1024];
    size_t len, room;
    if (buf == NULL || cap == 0)
        return;
    len = strlen(buf);
    if (len >= cap)
        return;
    room = cap - len - 1;
    if (len == 0)
        (void)snprintf(piece, sizeof(piece), "'%s'", s);
    else
        (void)snprintf(piece, sizeof(piece), ", '%s'", s);
    strncat(buf, piece, room);
}

static void sort_names(const char **arr, int n) {
    const char **items = arr;
    int i, j;
    if (items == NULL || n <= 1)
        return;
    for (i = 1; i < n; i++) {
        const char *key = items[i];
        j = i;
        while (j > 0 && strcmp(items[j - 1], key) > 0) {
            items[j] = items[j - 1];
            j--;
        }
        items[j] = key;
    }
}

/* ---- typed getters via stdlib/json.hp ---------------------------------- */

/* Read a JSON string through json_view::get_string (borrowed pointer). */
static const char *jstr(yyjson_val *v) {
    const char *s = NULL;
    json_view view;
    if (v == NULL || !yyjson_is_str(v))
        return NULL;
    view = json_view_of(v);
    if (view.get_string(&s, g_err) != STDLIB_OK)
        return NULL;
    return s;
}

static int jbool(yyjson_val *v, int *out) {
    json_view view;
    if (v == NULL || !yyjson_is_bool(v))
        return 0;
    view = json_view_of(v);
    return view.get_bool(out, g_err) == STDLIB_OK;
}

static int jint(yyjson_val *v, int *out) {
    json_view view;
    if (v == NULL || !yyjson_is_int(v))
        return 0;
    view = json_view_of(v);
    return view.get_int(out, g_err) == STDLIB_OK;
}

static int is_list(yyjson_val *v) {
    return v != NULL && yyjson_is_arr(v);
}

/* ---- structural helpers ------------------------------------------------ */

/* Python exact_keys(): compare the object's key set with the expected set.
 * Enumeration uses yyjson (json_view does not expose keys); membership uses
 * yyjson_obj_get so the comparison is set-based exactly like Python's set(obj). */
static int exact_keys(yyjson_val *obj, const char *const *expected, int n,
                      const char *where) {
    const char *const *keys = expected;
    char missing[1024], extra[1024];
    yyjson_obj_iter it;
    yyjson_val *k;
    int i;
    if (obj == NULL || !yyjson_is_obj(obj))
        FAIL("%s: expected object", where);
    if (keys == NULL || n <= 0)
        FAIL("%s: expected a non-empty key set", where);
    missing[0] = extra[0] = '\0';
    for (i = 0; i < n; i++)
        if (yyjson_obj_get(obj, keys[i]) == NULL)
            list_add(missing, sizeof(missing), keys[i]);
    it = yyjson_obj_iter_with(obj);
    while ((k = yyjson_obj_iter_next(&it)) != NULL) {
        const char *key = yyjson_get_str(k);
        if (!in_set(key, expected, n))
            list_add(extra, sizeof(extra), key != NULL ? key : "(non-string key)");
    }
    if (missing[0] != '\0' || extra[0] != '\0')
        FAIL("%s: key mismatch, missing=[%s], extra=[%s]", where, missing, extra);
    return 0;
}

/* Python non_empty_str_list(): a non-empty list of non-empty strings. */
static int non_empty_str_list(yyjson_val *arr, const char *where) {
    size_t i, n;
    if (!is_list(arr) || yyjson_arr_size(arr) == 0)
        FAIL("%s: expected a non-empty list", where);
    n = yyjson_arr_size(arr);
    for (i = 0; i < n; i++) {
        const char *s = jstr(yyjson_arr_get(arr, i));
        if (s == NULL || !has_nonspace(s))
            FAIL("%s: expected non-empty strings", where);
    }
    return 0;
}

static void lower_in_place(char *s) {
    while (*s != '\0') {
        *s = (char)tolower((unsigned char)*s);
        s++;
    }
}

/* Python check_extended_semantics(): typed-field checks plus serialized-text
 * markers so the semantic evidence is machine-verifiable. */
static int check_extended_semantics(yyjson_val *api, const char *where) {
    yyjson_val *params = yyjson_obj_get(api, "params");
    yyjson_val *failure = yyjson_obj_get(api, "failure");
    size_t i, nparams;
    char *text;
    size_t text_len = 0;
    int ret = 0;

    /* argument_types: every param carries a non-empty C type. */
    if (!is_list(params) || yyjson_arr_size(params) == 0)
        FAIL("%s: extended entry needs typed params (argument_types)", where);
    nparams = yyjson_arr_size(params);
    for (i = 0; i < nparams; i++) {
        const char *ty = jstr(yyjson_obj_get(yyjson_arr_get(params, i), "type"));
        if (ty == NULL || !has_nonspace(ty))
            FAIL("%s: extended entry needs typed params (argument_types)", where);
    }

    /* buffer_sizes: entry extent and every param extent are non-empty. */
    {
        const char *ext = jstr(yyjson_obj_get(api, "extent"));
        if (ext == NULL || !has_nonspace(ext))
            FAIL("%s: extended entry needs extents (buffer_sizes)", where);
    }
    for (i = 0; i < nparams; i++) {
        const char *pe = jstr(yyjson_obj_get(yyjson_arr_get(params, i), "extent"));
        if (pe == NULL || !has_nonspace(pe))
            FAIL("%s: extended entry needs extents (buffer_sizes)", where);
    }

    /* nullability: every param declares a bool. */
    for (i = 0; i < nparams; i++) {
        int flag = 0;
        if (!jbool(yyjson_obj_get(yyjson_arr_get(params, i), "nullable"), &flag))
            FAIL("%s: extended entry needs param nullability", where);
    }

    /* ownership_acquisition_release: ownership value + explicit transfer key. */
    {
        const char *own = jstr(yyjson_obj_get(api, "ownership"));
        if (own == NULL || !has_nonspace(own) || yyjson_obj_get(api, "transfer") == NULL)
            FAIL("%s: extended entry needs ownership/transfer", where);
    }

    /* error_behavior: result code plus at least one failure condition. */
    if (failure == NULL || !yyjson_is_obj(failure))
        FAIL("%s: extended entry needs failure result/conditions", where);
    {
        const char *rc = jstr(yyjson_obj_get(failure, "result_code"));
        yyjson_val *conditions = yyjson_obj_get(failure, "conditions");
        if (rc == NULL || !has_nonspace(rc) || !is_list(conditions) ||
            yyjson_arr_size(conditions) == 0)
            FAIL("%s: extended entry needs failure result/conditions", where);
    }

    /* Non-structural aspects: serialized text (lowercased) must contain a
     * marker from every group. */
    {
        json_view view = json_view_of(api);
        text = view.write(&text_len);
    }
    if (text == NULL)
        FAIL("%s: cannot serialise api for semantic evidence", where);
    (void)text_len;
    lower_in_place(text);
    for (i = 0; i < (size_t)N_MARKER_GROUPS; i++) {
        const char *const *markers = MARKER_GROUPS[i];
        int found = 0;
        size_t j;
        for (j = 0; markers[j] != NULL; j++) {
            if (strstr(text, markers[j]) != NULL) {
                found = 1;
                break;
            }
        }
        if (!found) {
            snprintf(g_msg, sizeof(g_msg),
                     "%s: extended entry missing %s evidence", where,
                     ASPECT_MARKER_NAMES[i]);
            ret = 1;
            break;
        }
    }
    json_free(text);
    return ret;
}

/* ---- file + parse ------------------------------------------------------ */

static int load_contract(const char *path) {
    error err = error();
    file f = file(path, "rb");
    long size = 0;
    size_t got;
    (void)err;
    if (!f.is_open())
        FAIL("missing contract file: %s", path);
    if (fseek(f.native(), 0, SEEK_END) != 0 ||
        (size = ftell(f.native())) < 0 ||
        fseek(f.native(), 0, SEEK_SET) != 0)
        FAIL("cannot size contract file: %s", path);
    if ((size_t)size >= CONTRACT_CAP)
        FAIL("contract file too large: %ld", size);
    got = fread(g_contract_buf, 1, (size_t)size, f.native());
    g_contract_buf[got] = '\0';
    g_contract_len = got;
    return 0;
}

static int parse_raw(const char *text, size_t len) {
    yyjson_read_err rerr;
    yyjson_doc *doc = yyjson_read_opts((char *)text, len, YYJSON_READ_NOFLAG, NULL, &rerr);
    if (doc == NULL) {
        /* Keep the fallback selection out of the FAIL macro: arithmetic and
         * conversions in a macro argument cannot be instrumented in STRICT. */
        const char *detail = (rerr.msg != NULL) ? rerr.msg : "parse failed";
        FAIL("invalid JSON in compiler_libc_contracts.json: %s", detail);
    }
    g_raw = doc;
    return 0;
}

/* ---- main -------------------------------------------------------------- */

static int run_checks(const char *path) {
    error err = error();
    json doc;
    json_view root_view;
    yyjson_doc *raw;
    /* Keep pointer declarations separate: the C+ rewriter wraps a
     * null-checked pointer in a statement-expression macro, which cannot
     * appear in a multi-declarator declaration in the lowered C. */
    yyjson_val *root;
    yyjson_val *apis;
    yyjson_val *semantics;
    yyjson_val *aspects;
    yyjson_val *symbols;
    yyjson_val *per_api_keys;
    char where[128], sub[160];
    size_t len, idx;
    int i, api_index;

    g_err = &err;
    if (load_contract(path) != 0)
        return 1;
    len = g_contract_len;

    /* Authoritative parse through stdlib/json.hp; raw parse only for key
     * enumeration (json_view intentionally has no key iterator). */
    doc = json(g_contract_buf, g_err);
    if (!doc.valid())
        FAIL("invalid JSON in compiler_libc_contracts.json: %s", g_err->what());
    root_view = doc.root();
    (void)root_view; /* root is read through raw values below; kept for parity */
    if (parse_raw(g_contract_buf, len) != 0)
        return 1;
    raw = g_raw;
    root = yyjson_doc_get_root(raw);
    if (root == NULL || !yyjson_is_obj(root))
        FAIL("top level: expected object");

    /* Top-level scalar fields. */
    {
        int sv = -1;
        const char *src, *note;
        if (!jint(yyjson_obj_get(root, "schema_version"), &sv) || sv != 1)
            FAIL("schema_version must be 1, got '%d'", sv);
        src = jstr(yyjson_obj_get(root, "source"));
        if (src == NULL || strcmp(src, "Agent 2 hardened libc") != 0)
            FAIL("source must be 'Agent 2 hardened libc'");
        note = jstr(yyjson_obj_get(root, "note"));
        if (note == NULL || !has_nonspace(note))
            FAIL("note: expected a non-empty string");
    }

    /* contract_semantics (additive top-level map). */
    semantics = yyjson_obj_get(root, "contract_semantics");
    if (semantics == NULL || !yyjson_is_obj(semantics))
        FAIL("contract_semantics: expected object");
    {
        int additive = 0;
        if (!jbool(yyjson_obj_get(semantics, "additive"), &additive) || additive != 1)
            FAIL("contract_semantics.additive must be true");
    }
    per_api_keys = yyjson_obj_get(semantics, "per_api_keys");
    {
        const char *s = jstr(per_api_keys);
        if (s == NULL)
            FAIL("contract_semantics.per_api_keys must list all nine API keys");
        for (i = 0; i < N_API_KEYS; i++)
            if (strstr(s, API_KEYS[i]) == NULL)
                FAIL("contract_semantics.per_api_keys must list all nine API keys");
    }
    aspects = yyjson_obj_get(semantics, "aspects");
    if (aspects == NULL || !yyjson_is_obj(aspects))
        FAIL("contract_semantics.aspects: expected object");
    {
        char missing[1024];
        yyjson_obj_iter it;
        yyjson_val *k;
        missing[0] = '\0';
        for (i = 0; i < N_ASPECTS; i++)
            if (yyjson_obj_get(aspects, SEMANTIC_ASPECTS[i]) == NULL)
                list_add(missing, sizeof(missing), SEMANTIC_ASPECTS[i]);
        if (missing[0] != '\0')
            FAIL("contract_semantics.aspects missing: [%s]", missing);
        it = yyjson_obj_iter_with(aspects);
        while ((k = yyjson_obj_iter_next(&it)) != NULL) {
            const char *desc = jstr(yyjson_obj_iter_get_val(k));
            const char *key = yyjson_get_str(k);
            if (desc == NULL || !has_nonspace(desc)) {
                const char *shown = (key != NULL) ? key : "(non-string key)";
                FAIL("contract_semantics.aspects[%s]: expected non-empty string",
                     shown);
            }
        }
    }

    /* implementation_symbols and apis containers. */
    symbols = yyjson_obj_get(root, "implementation_symbols");
    if (symbols == NULL || !yyjson_is_obj(symbols) || yyjson_obj_size(symbols) == 0)
        FAIL("implementation_symbols: expected a non-empty object");
    apis = yyjson_obj_get(root, "apis");
    if (!is_list(apis) || yyjson_arr_size(apis) == 0)
        FAIL("apis: expected a non-empty list");

    /* Full scan of every API entry. */
    for (idx = 0; idx < yyjson_arr_size(apis); idx++) {
        yyjson_val *api = yyjson_arr_get(apis, idx);
        yyjson_val *params;
        yyjson_val *failure;
        yyjson_val *dyn;
        yyjson_val *tr;
        const char *name;
        size_t pi, ni;
        int param_index;

        /* Narrow outside the fortified snprintf macro: a conversion in a
         * macro argument cannot be instrumented in STRICT. */
        api_index = (int)idx;
        snprintf(where, sizeof(where), "apis[%d]", api_index);
        if (exact_keys(api, API_KEYS, N_API_KEYS, where) != 0) return 1;

        name = jstr(yyjson_obj_get(api, "name"));
        if (name == NULL || !has_nonspace(name))
            FAIL("%s.name: expected a non-empty string", where);
        if (in_set(name, g_seen, g_seen_n))
            FAIL("%s: duplicate API name '%s'", where, name);
        g_seen[g_seen_n++] = name;
        g_api_names[g_api_n] = name;
        g_api_vals[g_api_n] = api;
        g_api_n++;

        params = yyjson_obj_get(api, "params");
        if (!is_list(params))
            FAIL("%s.params: expected a list", where);
        for (pi = 0; pi < yyjson_arr_size(params); pi++) {
            yyjson_val *p = yyjson_arr_get(params, pi);
            const char *pn, *pt, *pe;
            int flag;
            param_index = (int)pi;
            snprintf(sub, sizeof(sub), "%s.params[%d]", where, param_index);
            if (exact_keys(p, PARAM_KEYS, N_PARAM_KEYS, sub) != 0) return 1;
            pn = jstr(yyjson_obj_get(p, "name"));
            if (pn == NULL || !has_nonspace(pn))
                FAIL("%s.name: expected a non-empty string", sub);
            pt = jstr(yyjson_obj_get(p, "type"));
            if (pt == NULL || !has_nonspace(pt))
                FAIL("%s.type: expected a non-empty string", sub);
            if (!jbool(yyjson_obj_get(p, "nullable"), &flag))
                FAIL("%s.nullable: expected a bool", sub);
            pe = jstr(yyjson_obj_get(p, "extent"));
            if (pe == NULL || !has_nonspace(pe))
                FAIL("%s.extent: expected a non-empty string", sub);
        }

        {
            const char *own = jstr(yyjson_obj_get(api, "ownership"));
            const char *ext = jstr(yyjson_obj_get(api, "extent"));
            if (own == NULL || !has_nonspace(own))
                FAIL("%s.ownership: expected a non-empty string", where);
            tr = yyjson_obj_get(api, "transfer");
            if (tr != NULL && !yyjson_is_null(tr) && !yyjson_is_str(tr))
                FAIL("%s.transfer: expected a string or null", where);
            if (ext == NULL || !has_nonspace(ext))
                FAIL("%s.extent: expected a non-empty string", where);
        }

        snprintf(sub, sizeof(sub), "%s.effects", where);
        if (non_empty_str_list(yyjson_obj_get(api, "effects"), sub) != 0) return 1;

        failure = yyjson_obj_get(api, "failure");
        snprintf(sub, sizeof(sub), "%s.failure", where);
        if (exact_keys(failure, FAILURE_KEYS, N_FAILURE_KEYS, sub) != 0) return 1;
        {
            const char *rc = jstr(yyjson_obj_get(failure, "result_code"));
            if (rc == NULL || !has_nonspace(rc))
                FAIL("%s.failure.result_code: expected a non-empty string", where);
        }
        snprintf(sub, sizeof(sub), "%s.failure.conditions", where);
        if (non_empty_str_list(yyjson_obj_get(failure, "conditions"), sub) != 0) return 1;

        {
            yyjson_val *diags = yyjson_obj_get(api, "diagnostics");
            if (!is_list(diags))
                FAIL("%s.diagnostics: expected a list", where);
            for (ni = 0; ni < yyjson_arr_size(diags); ni++) {
                const char *d = jstr(yyjson_arr_get(diags, ni));
                if (d == NULL || !in_set(d, ALLOWED_DIAGNOSTICS, N_ALLOWED_DIAG)) {
                    const char *shown = (d != NULL) ? d : "(non-string)";
                    FAIL("%s.diagnostics: unknown category '%s'", where, shown);
                }
            }
        }

        dyn = yyjson_obj_get(api, "dynamic");
        snprintf(sub, sizeof(sub), "%s.dynamic", where);
        if (exact_keys(dyn, DYNAMIC_KEYS, N_DYNAMIC_KEYS, sub) != 0) return 1;
        snprintf(sub, sizeof(sub), "%s.dynamic.checks", where);
        if (non_empty_str_list(yyjson_obj_get(dyn, "checks"), sub) != 0) return 1;
        snprintf(sub, sizeof(sub), "%s.dynamic.limitations", where);
        if (!is_list(yyjson_obj_get(dyn, "limitations")))
            FAIL("%s: expected a list", sub);
    }

    /* Baseline + P0 name coverage. */
    {
        char missing[1024];
        missing[0] = '\0';
        for (i = 0; i < N_REQUIRED; i++)
            if (!in_set(REQUIRED_NAMES[i], g_seen, g_seen_n))
                list_add(missing, sizeof(missing), REQUIRED_NAMES[i]);
        if (missing[0] != '\0')
            FAIL("missing required API names: [%s]", missing);
        missing[0] = '\0';
        for (i = 0; i < N_P0; i++)
            if (!in_set(P0_REQUIRED_NAMES[i], g_seen, g_seen_n))
                list_add(missing, sizeof(missing), P0_REQUIRED_NAMES[i]);
        if (missing[0] != '\0')
            FAIL("missing P0 contract names: [%s]", missing);
    }

    /* implementation_symbols must resolve every contract to a bundled symbol. */
    sort_names(g_seen, g_seen_n);
    for (i = 0; i < g_seen_n; i++) {
        const char *name = g_seen[i];
        yyjson_val *sym = yyjson_obj_get(symbols, name);
        const char *sv = jstr(sym);
        if (sv == NULL || !has_nonspace(sv))
            FAIL("implementation_symbols missing valid entry for '%s'", name);
        if (in_set(name, CHECKED_ALLOCATORS, N_CHECKED)) {
            if (strcmp(sv, name) != 0)
                FAIL("implementation_symbols[%s] must be '%s', got '%s'", name, name, sv);
        } else {
            if (strncmp(sv, "cplus_", 6) != 0)
                FAIL("implementation_symbols[%s] must be a cplus_* symbol, got '%s'",
                     name, sv);
        }
    }
    {
        char extra[1024];
        yyjson_obj_iter it;
        yyjson_val *k;
        extra[0] = '\0';
        it = yyjson_obj_iter_with(symbols);
        while ((k = yyjson_obj_iter_next(&it)) != NULL) {
            const char *key = yyjson_get_str(k);
            if (!in_set(key, g_seen, g_seen_n))
                list_add(extra, sizeof(extra), key != NULL ? key : "(non-string key)");
        }
        if (extra[0] != '\0')
            FAIL("implementation_symbols has unknown contract names: [%s]", extra);
    }

    /* Six extended P0 entries must carry explicit semantic evidence. */
    for (i = 0; i < N_EXTENDED; i++) {
        const char *name = EXTENDED_P0_NAMES[i];
        int found = 0, j;
        for (j = 0; j < g_api_n; j++) {
            if (strcmp(g_api_names[j], name) == 0) {
                snprintf(where, sizeof(where), "api '%s'", name);
                if (check_extended_semantics(g_api_vals[j], where) != 0) return 1;
                found = 1;
                break;
            }
        }
        if (!found)
            FAIL("missing extended P0 contract: %s", name);
    }

    yyjson_doc_free(raw);
    doc.drop();
    return 0;
}

int main(int argc, char **argv) {
    const char *path = (argc > 1) ? argv[1] : CONTRACT_PATH;
    if (run_checks(path) != 0)
        fail(g_msg);
    printf("compiler contract alignment: ok\n");
    return 0;
}
