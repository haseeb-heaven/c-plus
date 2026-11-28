/* kv_store -- an in-memory, teaching-only key/value store written in C+.
 *
 * This is NOT a database engine: nothing is persisted, there is no SQL, no
 * transaction log and no concurrency.  It exists to show how the checked C+
 * stdlib APIs turn hostile inputs into *recoverable refusals* instead of
 * undefined behaviour.
 *
 * Safety properties demonstrated here:
 *   - keys and values are carried in stdlib/string (owned, bounds-checked
 *     at() / append()); length limits are enforced before any write
 *   - storage lives in stdlib/vector; every read uses at_checked(), every
 *     growth uses reserve() before committing fields, and delete uses erase()
 *   - every refusal is recorded in stdlib/error with a stable code/category
 *   - duplicate keys and missing keys are refused rather than silently
 *     overwriting or reading uninitialised data
 *
 * Build (from the repository root):
 *   ./build/c+ build apps/legacy_examples/expanded/database/kv_store.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/database/kv_store
 * Run with argv commands (preferred) or with one command per stdin line:
 *   ./apps/legacy_examples/expanded/database/kv_store insert alpha one get alpha list
 */
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define KV_KEY_MAX 16
#define KV_VAL_MAX 24

/* Fixed record layout inside the vector (all ints, no raw pointers):
 *   [0] key length, [1 .. 1+KEY_MAX) key bytes (zero padded),
 *   [1+KEY_MAX] value length, [1+KEY_MAX+1 ..) value bytes (zero padded). */
#define KV_R_LEN  0
#define KV_R_KEY  1
#define KV_R_VLEN (KV_R_KEY + KV_KEY_MAX)
#define KV_R_VAL  (KV_R_VLEN + 1)
#define KV_FIELDS (KV_R_VAL + KV_VAL_MAX)

static int g_ops = 0;
static int g_ok = 0;
static int g_refused = 0;
static int g_failed = 0;

static void kv_refuse(error *err, int category, cplus_exit_code code,
                      const char *message) {
    error_fail_cat(err, category, code, message);
}

/* Copy a NUL/newline terminated token into an owned, cleaned string without
 * ever indexing the raw pointer parameter (strict mode rejects that). */
static void kv_clean_string(const char *raw, string *out) {
    string tmp = string(raw);
    size_t i;
    size_t n = tmp.size();
    char one[2];
    for (i = 0; i < n; i++) {
        char c = tmp.at(i);
        if (c == '\n' || c == '\r') continue;
        one[0] = c;
        one[1] = '\0';
        out->append(one);
    }
    tmp.drop();
}

static int kv_check_key(string *key, error *err) {
    if (!key->valid() || key->size() == 0) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "empty or invalid key");
        return -1;
    }
    if (key->size() > KV_KEY_MAX) {
        kv_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                  "key exceeds KV_KEY_MAX");
        return -1;
    }
    if (!key->is_valid_utf8()) {
        kv_refuse(err, STDLIB_CATEGORY_UTF8, CPLUS_EXIT_UNSUPPORTED,
                  "key is not valid UTF-8");
        return -1;
    }
    return 0;
}

static int kv_check_value(string *value, error *err) {
    if (!value->valid()) {
        kv_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                  "value could not be stored in a string");
        return -1;
    }
    if (value->size() > KV_VAL_MAX) {
        kv_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                  "value exceeds KV_VAL_MAX");
        return -1;
    }
    if (!value->is_valid_utf8()) {
        kv_refuse(err, STDLIB_CATEGORY_UTF8, CPLUS_EXIT_UNSUPPORTED,
                  "value is not valid UTF-8");
        return -1;
    }
    return 0;
}

/* Index of the record whose key equals `key`, or -1 when absent.  All storage
 * reads go through vector::at_checked so a corrupt size cannot be read. */
static long kv_find(vector *store, string *key) {
    size_t total = store->size();
    size_t base;
    if (total < KV_FIELDS) return -1;
    for (base = 0; base + KV_FIELDS <= total; base += KV_FIELDS) {
        size_t j;
        int match = 1;
        int byte = 0;
        for (j = 0; j < KV_KEY_MAX; j++) {
            if (store->at_checked(base + KV_R_KEY + j, &byte) != 0)
                return -1;
            if ((char)byte != key->at(j)) match = 0;
        }
        if (match) return (long)base;
    }
    return -1;
}

/* 0 on success; a positive stable exit code on refusal. */
static int kv_put(vector *store, const char *key_text, const char *value_text,
                  error *err) {
    string key;
    string value;
    size_t j;
    int rc = 0;

    error_reset(err);
    if (key_text == NULL) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null key");
        return err->code();
    }
    if (value_text == NULL) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null value");
        return err->code();
    }
    key = string(key_text);
    value = string(value_text);
    if (kv_check_key(&key, err) != 0 || kv_check_value(&value, err) != 0) {
        rc = err->code();
        key.drop();
        value.drop();
        return rc;
    }
    if (kv_find(store, &key) >= 0) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "duplicate key");
        rc = err->code();
        key.drop();
        value.drop();
        return rc;
    }
    /* One reserve first, then no push_back can fail: the record is committed
     * as a whole and there is no half-written tail on allocation failure. */
    if (store->reserve(store->size() + KV_FIELDS) != 0) {
        kv_refuse(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                  "store reserve failed");
        rc = err->code();
        key.drop();
        value.drop();
        return rc;
    }
    store->push_back((int)key.size());
    for (j = 0; j < KV_KEY_MAX; j++)
        store->push_back(j < key.size() ? (int)(unsigned char)key.at(j) : 0);
    store->push_back((int)value.size());
    for (j = 0; j < KV_VAL_MAX; j++)
        store->push_back(j < value.size() ? (int)(unsigned char)value.at(j) : 0);
    key.drop();
    value.drop();
    return rc;
}

static int kv_get(vector *store, const char *key_text, string *out,
                  error *err) {
    string key;
    long at;
    int vlen = 0;
    int byte = 0;
    int i;
    char one[2];

    error_reset(err);
    if (key_text == NULL) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null key");
        return err->code();
    }
    key = string(key_text);
    if (kv_check_key(&key, err) != 0) {
        int code = err->code();
        key.drop();
        return code;
    }
    at = kv_find(store, &key);
    key.drop();
    if (at < 0) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "no such key");
        return err->code();
    }
    if (store->at_checked((size_t)at + KV_R_VLEN, &vlen) != 0) {
        kv_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                  "corrupt value length");
        return err->code();
    }
    if (vlen < 0 || vlen > KV_VAL_MAX) {
        kv_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                  "value length out of range");
        return err->code();
    }
    out->clear();
    for (i = 0; i < vlen; i++) {
        if (store->at_checked((size_t)at + KV_R_VAL + (size_t)i, &byte) != 0) {
            kv_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                      "corrupt value bytes");
            return err->code();
        }
        one[0] = (char)byte;
        one[1] = '\0';
        if (out->append(one) != 0) {
            kv_refuse(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                      "output append failed");
            return err->code();
        }
    }
    return 0;
}

static int kv_update(vector *store, const char *key_text,
                     const char *value_text, error *err) {
    string key;
    string value;
    vector fresh = vector();
    long at;
    size_t i;
    size_t j;
    size_t total;
    size_t vlen;
    int rc = 0;

    error_reset(err);
    if (key_text == NULL || value_text == NULL) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null key or value");
        return err->code();
    }
    key = string(key_text);
    value = string(value_text);
    if (kv_check_key(&key, err) != 0 || kv_check_value(&value, err) != 0) {
        rc = err->code();
        key.drop();
        value.drop();
        return rc;
    }
    at = kv_find(store, &key);
    if (at < 0) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "no such key");
        rc = err->code();
        key.drop();
        value.drop();
        return rc;
    }
    total = store->size();
    vlen = value.size();
    if (fresh.reserve(total) != 0) {
        kv_refuse(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                  "rebuild reserve failed");
        rc = err->code();
        key.drop();
        value.drop();
        fresh.drop();
        return rc;
    }
    for (i = 0; i < total; i += KV_FIELDS) {
        for (j = 0; j < KV_FIELDS; j++) {
            int byte = 0;
            if (store->at_checked(i + j, &byte) != 0) {
                kv_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                          "corrupt record");
                rc = err->code();
                key.drop();
                value.drop();
                fresh.drop();
                return rc;
            }
            if ((long)i == at) {
                if (j == KV_R_VLEN)
                    byte = (int)vlen;
                else if (j >= KV_R_VAL)
                    byte = (j - KV_R_VAL < vlen)
                               ? (int)(unsigned char)value.at(j - KV_R_VAL)
                               : 0;
            }
            fresh.push_back(byte);
        }
    }
    if (store->copy_from(&fresh) != 0) {
        kv_refuse(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                  "rebuild commit failed");
        rc = err->code();
    }
    key.drop();
    value.drop();
    fresh.drop();
    return rc;
}

static int kv_delete(vector *store, const char *key_text, error *err) {
    string key;
    long at;
    size_t j;
    int rc = 0;

    error_reset(err);
    if (key_text == NULL) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null key");
        return err->code();
    }
    key = string(key_text);
    if (kv_check_key(&key, err) != 0) {
        rc = err->code();
        key.drop();
        return rc;
    }
    at = kv_find(store, &key);
    key.drop();
    if (at < 0) {
        kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "no such key");
        return err->code();
    }
    for (j = 0; j < KV_FIELDS; j++) {
        if (store->erase((size_t)at, NULL) != 0) {
            kv_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                      "erase failed");
            return err->code();
        }
    }
    return rc;
}

/* Number of whole records (vector::size() counts int fields, not records). */
static int kv_count(vector *store) {
    size_t total = store->size();
    size_t base;
    int count = 0;
    for (base = 0; base + KV_FIELDS <= total; base += KV_FIELDS)
        count++;
    return count;
}

static void kv_list(vector *store) {
    string key = string("");
    string value = string("");
    size_t total = store->size();
    size_t base;
    int count = 0;
    int klen = 0;
    int vlen = 0;
    int byte = 0;
    int i;

    for (base = 0; base + KV_FIELDS <= total; base += KV_FIELDS) {
        key.clear();
        value.clear();
        if (store->at_checked(base + KV_R_LEN, &klen) != 0) klen = 0;
        if (store->at_checked(base + KV_R_VLEN, &vlen) != 0) vlen = 0;
        if (klen < 0 || klen > KV_KEY_MAX) klen = 0;
        if (vlen < 0 || vlen > KV_VAL_MAX) vlen = 0;
        for (i = 0; i < klen; i++) {
            char one[2];
            if (store->at_checked(base + KV_R_KEY + (size_t)i, &byte) != 0)
                break;
            one[0] = (char)byte;
            one[1] = '\0';
            key.append(one);
        }
        for (i = 0; i < vlen; i++) {
            char one[2];
            if (store->at_checked(base + KV_R_VAL + (size_t)i, &byte) != 0)
                break;
            one[0] = (char)byte;
            one[1] = '\0';
            value.append(one);
        }
        print("KV item", count, key.c_str(), value.c_str());
        count++;
    }
    print("KV list", count);
    key.drop();
    value.drop();
}

static int kv_parse_int(const char *text, int *out) {
    char *end = NULL;
    long value;
    if (text == NULL) return -1;
    errno = 0;
    value = strtol(text, &end, 10);
    if (end == text || *end != '\0' || errno == ERANGE) return -1;
    if (value < INT_MIN || value > INT_MAX) return -1;
    *out = (int)value;
    return 0;
}

/* Execute one command.  `a` and `b` may be NULL when the command needs fewer
 * operands; missing operands are refusals, never out-of-bounds reads. */
static int kv_command(vector *store, const char *cmd, const char *a,
                      const char *b, error *err) {
    string outcome = string("");
    int code;

    if (strcmp(cmd, "insert") == 0 || strcmp(cmd, "update") == 0) {
        if (a == NULL || b == NULL) {
            kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                      "missing key or value operand");
            g_ops++;
            g_refused++;
            print("KV", cmd, "REFUSED", err->code(), err->what());
            return err->code();
        }
        code = strcmp(cmd, "insert") == 0 ? kv_put(store, a, b, err)
                                          : kv_update(store, a, b, err);
        g_ops++;
        if (code == 0) {
            g_ok++;
            print("KV", cmd, "OK", a);
        } else {
            g_refused++;
            print("KV", cmd, "REFUSED", code,
                  stdlib_category_name(err->category()), err->what());
        }
        return code;
    }
    if (strcmp(cmd, "get") == 0 || strcmp(cmd, "delete") == 0) {
        if (a == NULL) {
            kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                      "missing key operand");
            g_ops++;
            g_refused++;
            print("KV", cmd, "REFUSED", err->code(), err->what());
            return err->code();
        }
        outcome = string("");
        if (strcmp(cmd, "get") == 0) {
            code = kv_get(store, a, &outcome, err);
        } else {
            code = kv_delete(store, a, err);
        }
        g_ops++;
        if (code == 0) {
            g_ok++;
            if (strcmp(cmd, "get") == 0)
                print("KV get", a, outcome.c_str());
            else
                print("KV delete", a, "OK");
        } else {
            g_refused++;
            print("KV", cmd, "REFUSED", code,
                  stdlib_category_name(err->category()), err->what());
        }
        outcome.drop();
        return code;
    }
    if (strcmp(cmd, "list") == 0) {
        g_ops++;
        g_ok++;
        kv_list(store);
        return 0;
    }
    if (strcmp(cmd, "expect_size") == 0 || strcmp(cmd, "size") == 0) {
        int want = 0;
        int actual = kv_count(store);
        g_ops++;
        if (strcmp(cmd, "size") == 0 || kv_parse_int(a, &want) != 0) {
            if (strcmp(cmd, "size") == 0) {
                g_ok++;
                print("KV size", actual);
                return 0;
            }
            g_failed++;
            print("KV expect_size BAD_INPUT", actual);
            return CPLUS_EXIT_UNSUPPORTED;
        }
        if (want == actual) {
            g_ok++;
            print("KV expect_size", want, "OK");
            return 0;
        }
        g_failed++;
        print("KV expect_size", want, "FAIL", actual);
        return CPLUS_EXIT_BOUNDS;
    }
    kv_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
              "unknown command");
    g_ops++;
    g_refused++;
    print("KV", cmd, "REFUSED", err->code(), err->what());
    return err->code();
}

static char *kv_argv_at(char **argv, int index) {
    char *value = NULL;
    __builtin_memcpy(&value, argv + index, sizeof(value));
    return value;
}

int main(int argc, char **argv) {
    vector store = vector();
    error err = error();
    int i;
    char line[128];

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    if (argc > 1) {
        i = 1;
        while (i < argc) {
            char *cmd = kv_argv_at(argv, i);
            char *a = i + 1 < argc ? kv_argv_at(argv, i + 1) : NULL;
            char *b = i + 2 < argc ? kv_argv_at(argv, i + 2) : NULL;
            int used = 1;
            if (strcmp(cmd, "insert") == 0 || strcmp(cmd, "update") == 0) {
                used = 3;
            } else if (strcmp(cmd, "get") == 0 || strcmp(cmd, "delete") == 0 ||
                       strcmp(cmd, "expect_size") == 0 || strcmp(cmd, "size") == 0) {
                used = 2;
            }
            if (i + used > argc) used = 1;
            kv_command(&store, cmd, a, b, &err);
            i += used;
        }
    } else {
        /* stdin fallback: one whitespace-separated command per line. */
        while (fgets(line, (int)sizeof(line), stdin) != NULL) {
            string ln = string(line);
            string tok0 = string("");
            string tok1 = string("");
            string tok2 = string("");
            char raw[64];
            if (ln.field(' ', 0, raw, sizeof(raw)) == 0) kv_clean_string(raw, &tok0);
            if (ln.field(' ', 1, raw, sizeof(raw)) == 0) kv_clean_string(raw, &tok1);
            if (ln.field(' ', 2, raw, sizeof(raw)) == 0) kv_clean_string(raw, &tok2);
            kv_command(&store, tok0.c_str(),
                       tok1.size() > 0 ? tok1.c_str() : NULL,
                       tok2.size() > 0 ? tok2.c_str() : NULL, &err);
            ln.drop();
            tok0.drop();
            tok1.drop();
            tok2.drop();
        }
    }

    print("KV_SUMMARY", g_ops, g_ok, g_refused, g_failed);
    store.drop();
    return g_failed == 0 ? 0 : 1;
}
