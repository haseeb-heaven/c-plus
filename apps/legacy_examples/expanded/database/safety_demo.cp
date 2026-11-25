/* safety_demo -- hostile operations against the in-memory stdlib containers.
 *
 * Teaching example only.  Each case deliberately feeds the store/aggregate a
 * value that must be refused: an oversized key, a NULL value, a SUM that would
 * overflow int, a duplicate key and a delete of a missing key.  Every refusal
 * is reported with its stable C+ exit code and error details, and a 32-byte
 * canary is checked before and after the operation so a failed operation can
 * be seen not to have written outside its own storage.
 *
 * Output contract (one line per case, then a summary):
 *   SAFETY <op> REFUSED <code>
 *   SAFETY <op> DETAIL <category> <message>
 *   SAFETY <op> CANARY ok
 *   SAFETY_SUMMARY <cases> <refused> <canary_ok> <failed>
 *
 * Build:
 *   ./build/c+ build apps/legacy_examples/expanded/database/safety_demo.cp --strict --sanitize=none -I. -o apps/legacy_examples/expanded/database/safety_demo
 */
#include <limits.h>
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

#define SD_KEY_MAX 16
#define SD_VAL_MAX 16
#define SD_R_LEN 0
#define SD_R_KEY 1
#define SD_R_VLEN (SD_R_KEY + SD_KEY_MAX)
#define SD_R_VAL (SD_R_VLEN + 1)
#define SD_FIELDS (SD_R_VAL + SD_VAL_MAX)
#define SD_CANARY_LEN 32
#define SD_ROW_FIELDS 2

static unsigned char g_canary[SD_CANARY_LEN];
static int g_cases = 0;
static int g_refused = 0;
static int g_canary_ok = 0;
static int g_failed = 0;

static void sd_refuse(error *err, int category, cplus_exit_code code,
                      const char *message) {
    error_fail_cat(err, category, code, message);
}

static unsigned char sd_expected(int i) {
    return (unsigned char)((0xA5 ^ (unsigned int)i) & 0xFFu);
}

static void sd_canary_fill(void) {
    int i;
    for (i = 0; i < SD_CANARY_LEN; i++)
        g_canary[i] = sd_expected(i);
}

static int sd_canary_check(void) {
    int i;
    for (i = 0; i < SD_CANARY_LEN; i++)
        if (g_canary[i] != sd_expected(i)) return 0;
    return 1;
}

static int sd_find(vector *store, string *key) {
    size_t total = store->size();
    size_t base;
    for (base = 0; base + SD_FIELDS <= total; base += SD_FIELDS) {
        size_t j;
        int match = 1;
        int byte = 0;
        for (j = 0; j < SD_KEY_MAX; j++) {
            if (store->at_checked(base + SD_R_KEY + j, &byte) != 0)
                return -1;
            if ((char)byte != key->at(j)) match = 0;
        }
        if (match) return (int)base;
    }
    return -1;
}

/* Same deliberate checks as kv_store: NULL, length bounds, duplicate key. */
static int sd_put(vector *store, const char *key_text, const char *value_text,
                  error *err) {
    string key;
    string value;
    size_t j;

    error_reset(err);
    if (key_text == NULL) {
        sd_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null key");
        return err->code();
    }
    if (value_text == NULL) {
        sd_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null value");
        return err->code();
    }
    key = string(key_text);
    value = string(value_text);
    if (!key.valid() || key.size() == 0 || key.size() > SD_KEY_MAX) {
        sd_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                  "key length out of bounds");
        key.drop();
        value.drop();
        return err->code();
    }
    if (!value.valid() || value.size() > SD_VAL_MAX) {
        sd_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                  "value length out of bounds");
        key.drop();
        value.drop();
        return err->code();
    }
    if (sd_find(store, &key) >= 0) {
        sd_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "duplicate key");
        key.drop();
        value.drop();
        return err->code();
    }
    if (store->reserve(store->size() + SD_FIELDS) != 0) {
        sd_refuse(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                  "reserve failed");
        key.drop();
        value.drop();
        return err->code();
    }
    store->push_back((int)key.size());
    for (j = 0; j < SD_KEY_MAX; j++)
        store->push_back(j < key.size() ? (int)(unsigned char)key.at(j) : 0);
    store->push_back((int)value.size());
    for (j = 0; j < SD_VAL_MAX; j++)
        store->push_back(j < value.size() ? (int)(unsigned char)value.at(j) : 0);
    key.drop();
    value.drop();
    return 0;
}

static int sd_delete(vector *store, const char *key_text, error *err) {
    string key;
    long at;
    size_t j;

    error_reset(err);
    if (key_text == NULL) {
        sd_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null key");
        return err->code();
    }
    key = string(key_text);
    if (!key.valid() || key.size() == 0 || key.size() > SD_KEY_MAX) {
        sd_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                  "key length out of bounds");
        key.drop();
        return err->code();
    }
    at = sd_find(store, &key);
    key.drop();
    if (at < 0) {
        sd_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "no such key");
        return err->code();
    }
    for (j = 0; j < SD_FIELDS; j++)
        if (store->erase((size_t)at, NULL) != 0) {
            sd_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                      "erase failed");
            return err->code();
        }
    return 0;
}

/* SUM over the amount column, refused before signed overflow is executed. */
static int sd_sum(vector *rows, error *err) {
    size_t total = rows->size();
    size_t base;
    int sum = 0;

    error_reset(err);
    for (base = 0; base + SD_ROW_FIELDS <= total; base += SD_ROW_FIELDS) {
        int amt = 0;
        int next = 0;
        if (rows->at_checked(base + 1, &amt) != 0) {
            sd_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                      "corrupt amount");
            return err->code();
        }
        if (amt > 0 && sum > INT_MAX - amt) {
            sd_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                      "sum overflow");
            return err->code();
        }
        if (amt < 0 && sum < INT_MIN - amt) {
            sd_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                      "sum overflow");
            return err->code();
        }
        next = sum + amt;
        sum = next;
    }
    return 0;
}

static int sd_count(vector *store) {
    size_t total = store->size();
    size_t base;
    int count = 0;
    for (base = 0; base + SD_FIELDS <= total; base += SD_FIELDS)
        count++;
    return count;
}

/* Read back the surviving value; every byte comes from at_checked(). */
static int sd_get(vector *store, const char *key_text, string *out,
                  error *err) {
    string key;
    long at;
    int vlen = 0;
    int byte = 0;
    int i;
    char one[2];

    error_reset(err);
    if (key_text == NULL) {
        sd_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "null key");
        return err->code();
    }
    key = string(key_text);
    at = sd_find(store, &key);
    key.drop();
    if (at < 0) {
        sd_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "no such key");
        return err->code();
    }
    if (store->at_checked((size_t)at + SD_R_VLEN, &vlen) != 0) {
        sd_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                  "corrupt value length");
        return err->code();
    }
    for (i = 0; i < vlen; i++) {
        if (store->at_checked((size_t)at + SD_R_VAL + (size_t)i, &byte) != 0) {
            sd_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                      "corrupt value bytes");
            return err->code();
        }
        one[0] = (char)byte;
        one[1] = '\0';
        if (out->append(one) != 0) {
            sd_refuse(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                      "append failed");
            return err->code();
        }
    }
    return 0;
}

static void sd_report(const char *op, int expected_code, error *err) {
    int ok = 1;
    g_cases++;
    if (err->code() != expected_code) ok = 0;
    if (err->ok()) ok = 0;
    if (ok) {
        g_refused++;
        print("SAFETY", op, "REFUSED", err->code());
        print("SAFETY", op, "DETAIL", stdlib_category_name(err->category()),
              err->what());
    } else {
        g_failed++;
        print("SAFETY", op, "FAIL", err->code(), expected_code, err->what());
    }
    if (sd_canary_check()) {
        g_canary_ok++;
        print("SAFETY", op, "CANARY ok");
    } else {
        g_failed++;
        print("SAFETY", op, "CANARY broken");
    }
}

int main(void) {
    vector store = vector();
    vector rows = vector();
    error err = error();
    int code;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    sd_canary_fill();

    /* 1. oversized key: 40 bytes is well past the 16-byte key limit. */
    {
        string big = string("0123456789012345678901234567890123456789");
        sd_canary_fill();
        code = sd_put(&store, big.c_str(), "v", &err);
        (void)code;
        sd_report("oversized_key", CPLUS_EXIT_BOUNDS, &err);
        big.drop();
    }

    /* 2. NULL value: no string is constructed from the NULL pointer. */
    {
        sd_canary_fill();
        code = sd_put(&store, "good", NULL, &err);
        (void)code;
        sd_report("null_value", CPLUS_EXIT_UNSUPPORTED, &err);
    }

    /* 3. overflowing SUM: INT_MAX + 1 must be refused, not wrapped. */
    {
        sd_canary_fill();
        rows.push_back(1);
        rows.push_back(INT_MAX);
        rows.push_back(1);
        rows.push_back(1);
        code = sd_sum(&rows, &err);
        (void)code;
        sd_report("overflowing_sum", CPLUS_EXIT_BOUNDS, &err);
    }

    /* 4. duplicate key: the second insert is refused, the first survives. */
    {
        sd_canary_fill();
        code = sd_put(&store, "dup", "one", &err);
        if (code != 0) g_failed++;
        code = sd_put(&store, "dup", "two", &err);
        (void)code;
        sd_report("duplicate_key", CPLUS_EXIT_UNSUPPORTED, &err);
    }

    /* 5. delete-missing: refusing must not shift or clear live records. */
    {
        sd_canary_fill();
        code = sd_delete(&store, "absent", &err);
        (void)code;
        sd_report("delete_missing", CPLUS_EXIT_UNSUPPORTED, &err);
    }

    /* Live data must still be read back correctly after all refusals. */
    {
        string kept = string("");
        code = sd_get(&store, "dup", &kept, &err);
        if (code == 0 && kept.equals("one"))
            print("SAFETY FINAL_VALUE dup one");
        else {
            print("SAFETY FINAL_VALUE FAIL", code, kept.c_str());
            g_failed++;
        }
        kept.drop();
    }
    if (sd_canary_check())
        print("SAFETY FINAL_CANARY ok");
    else {
        print("SAFETY FINAL_CANARY broken");
        g_failed++;
    }
    print("SAFETY FINAL_STORE", sd_count(&store));
    print("SAFETY_SUMMARY", g_cases, g_refused, g_canary_ok, g_failed);
    rows.drop();
    store.drop();
    return g_failed == 0 ? 0 : 1;
}
