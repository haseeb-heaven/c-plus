/* table_query -- a tiny in-memory table with checked queries and aggregates.
 *
 * Teaching example only: there is no persistence, no index maintenance beyond
 * a small intmap, no SQL parser and no concurrency.  The point is to show
 * runtime safety through the checked C+ stdlib:
 *
 *   - rows live in stdlib/vector; every field read uses at_checked()
 *   - stdlib/collections intmap detects duplicate ids before insertion
 *   - stdlib/string parses field names without indexing raw argv pointers
 *   - stdlib/error records stable refusal codes for bad predicates and for
 *     integer overflow in SUM (the sum is checked before it is committed)
 *
 * Build (from the repository root):
 *   ./build/c+ build examples/expanded/database/table_query.cp --strict --sanitize=none -I. -o examples/expanded/database/table_query
 * Run: ./examples/expanded/database/table_query row 1 10 100 eq grp 10
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
#include "stdlib/collections.hp"

#define TQ_FIELDS 3
#define TQ_COL_ID 0
#define TQ_COL_GRP 1
#define TQ_COL_AMT 2

static int g_ops = 0;
static int g_ok = 0;
static int g_refused = 0;
static int g_failed = 0;

static void tq_refuse(error *err, int category, cplus_exit_code code,
                      const char *message) {
    error_fail_cat(err, category, code, message);
}

static int tq_parse_int(const char *text, int *out) {
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

/* Column name -> fixed field index.  -1 for an unknown field; the caller
 * refuses it instead of reading an arbitrary vector slot. */
static int tq_field(const char *name) {
    string f = string(name);
    int index = -1;
    if (f.equals("id")) index = TQ_COL_ID;
    else if (f.equals("grp")) index = TQ_COL_GRP;
    else if (f.equals("amt")) index = TQ_COL_AMT;
    f.drop();
    return index;
}

static int tq_count(vector *rows) {
    size_t total = rows->size();
    size_t base;
    int count = 0;
    for (base = 0; base + TQ_FIELDS <= total; base += TQ_FIELDS)
        count++;
    return count;
}

static int tq_read_row(vector *rows, size_t base, int *id, int *grp,
                       int *amt, error *err) {
    if (rows->at_checked(base + TQ_COL_ID, id) != 0 ||
        rows->at_checked(base + TQ_COL_GRP, grp) != 0 ||
        rows->at_checked(base + TQ_COL_AMT, amt) != 0) {
        tq_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                  "corrupt row");
        return err->code();
    }
    return 0;
}

static int tq_insert(vector *rows, intmap *ids, int id, int grp, int amt,
                     error *err) {
    size_t base;
    int i = 0;
    int r_id = 0;
    int r_grp = 0;
    int r_amt = 0;

    error_reset(err);
    if (ids->contains(id)) {
        tq_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "duplicate id");
        return err->code();
    }
    /* Validate the whole table before committing either structure. */
    base = 0;
    while (base + TQ_FIELDS <= rows->size()) {
        int code = tq_read_row(rows, base, &r_id, &r_grp, &r_amt, err);
        if (code != 0) return code;
        base += TQ_FIELDS;
        i++;
    }
    if (rows->reserve(rows->size() + TQ_FIELDS) != 0) {
        tq_refuse(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                  "row reserve failed");
        return err->code();
    }
    if (ids->put(id, i) != 0) {
        tq_refuse(err, STDLIB_CATEGORY_MEMORY, CPLUS_EXIT_RESOURCE,
                  "index insert failed");
        return err->code();
    }
    rows->push_back(id);
    rows->push_back(grp);
    rows->push_back(amt);
    return 0;
}

/* Addition that never executes signed overflow; on refusal *out is unchanged. */
static int tq_add_checked(int a, int b, int *out) {
    if (b > 0 && a > INT_MAX - b) return -1;
    if (b < 0 && a < INT_MIN - b) return -1;
    *out = a + b;
    return 0;
}

/* 1 when the row's selected field is within inclusive [lo, hi]. */
static int tq_in_range(vector *rows, size_t base, int field, int lo, int hi) {
    int value = 0;
    if (rows->at_checked(base + (size_t)field, &value) != 0) return 0;
    return value >= lo && value <= hi;
}

static int tq_count_matching(vector *rows, int field, int lo, int hi,
                             int *out, error *err) {
    size_t base;
    int count = 0;
    error_reset(err);
    if (field < 0 || field >= TQ_FIELDS) {
        tq_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "unknown field");
        return err->code();
    }
    if (lo > hi) {
        tq_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                  "empty range lo>hi");
        return err->code();
    }
    for (base = 0; base + TQ_FIELDS <= rows->size(); base += TQ_FIELDS)
        if (tq_in_range(rows, base, field, lo, hi)) count++;
    *out = count;
    return 0;
}

/* SUM over the amt column of matching rows; refuses before wrapping. */
static int tq_sum_matching(vector *rows, int field, int lo, int hi,
                           int *out, error *err) {
    size_t base;
    int total = 0;

    error_reset(err);
    if (field < 0 || field >= TQ_FIELDS) {
        tq_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                  "unknown field");
        return err->code();
    }
    if (lo > hi) {
        tq_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                  "empty range lo>hi");
        return err->code();
    }
    for (base = 0; base + TQ_FIELDS <= rows->size(); base += TQ_FIELDS) {
        int amt = 0;
        int next = 0;
        if (!tq_in_range(rows, base, field, lo, hi)) continue;
        if (rows->at_checked(base + TQ_COL_AMT, &amt) != 0) {
            tq_refuse(err, STDLIB_CATEGORY_BOUNDS, CPLUS_EXIT_BOUNDS,
                      "corrupt amount");
            return err->code();
        }
        if (tq_add_checked(total, amt, &next) != 0) {
            tq_refuse(err, STDLIB_CATEGORY_RANGE, CPLUS_EXIT_BOUNDS,
                      "sum overflow");
            return err->code();
        }
        total = next;
    }
    *out = total;
    return 0;
}

static void tq_list_matching(vector *rows, int field, int lo, int hi) {
    size_t base;
    int count = 0;
    for (base = 0; base + TQ_FIELDS <= rows->size(); base += TQ_FIELDS) {
        int id = 0;
        int grp = 0;
        int amt = 0;
        error err = error();
        if (!tq_in_range(rows, base, field, lo, hi)) continue;
        if (tq_read_row(rows, base, &id, &grp, &amt, &err) != 0) continue;
        print("TQ row", id, grp, amt);
        count++;
    }
}

static char *tq_argv_at(char **argv, int index) {
    char *value = NULL;
    __builtin_memcpy(&value, argv + index, sizeof(value));
    return value;
}

/* Dispatch one command; operands are already bound-checked by main(). */
static int tq_command(vector *rows, intmap *ids, const char *cmd,
                      const char *a, const char *b, const char *c,
                      error *err) {
    int field = -1;
    int x = 0;
    int y = 0;
    int want = 0;
    int value = 0;
    int code = 0;
    int matched = 0;

    if (strcmp(cmd, "row") == 0) {
        g_ops++;
        if (a == NULL || b == NULL || c == NULL ||
            tq_parse_int(a, &x) != 0 || tq_parse_int(b, &y) != 0 ||
            tq_parse_int(c, &want) != 0) {
            tq_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                      "row needs id grp amt integers");
            g_refused++;
            print("TQ row REFUSED", err->code(), err->what());
            return err->code();
        }
        code = tq_insert(rows, ids, x, y, want, err);
        if (code == 0) {
            g_ok++;
            print("TQ row", x, "OK");
        } else {
            g_refused++;
            print("TQ row", x, "REFUSED", code,
                  stdlib_category_name(err->category()), err->what());
        }
        return code;
    }

    if (strcmp(cmd, "eq") == 0 || strcmp(cmd, "range") == 0 ||
        strcmp(cmd, "count") == 0 || strcmp(cmd, "sum") == 0 ||
        strcmp(cmd, "expect_count") == 0 || strcmp(cmd, "expect_sum") == 0 ||
        strcmp(cmd, "expect_sum_refusal") == 0) {
        g_ops++;
        field = tq_field(a);
        if (field < 0 || b == NULL ||
            tq_parse_int(b, &x) != 0 ||
            (strcmp(cmd, "expect_sum_refusal") == 0
                 ? (c == NULL || tq_parse_int(c, &want) != 0)
                 : (strcmp(cmd, "range") == 0
                        ? (c == NULL || tq_parse_int(c, &want) != 0)
                        : 0)) != 0) {
            tq_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
                      "bad field or predicate operand");
            g_refused++;
            print("TQ", cmd, "REFUSED", err->code(), err->what());
            return err->code();
        }
        if (strcmp(cmd, "range") == 0) {
            y = want; /* inclusive [x, y] */
        } else {
            y = x;    /* equality */
        }

        if (strcmp(cmd, "eq") == 0 || strcmp(cmd, "range") == 0) {
            code = tq_count_matching(rows, field, x, y, &matched, err);
            if (code != 0) {
                g_refused++;
                print("TQ", cmd, "REFUSED", code,
                      stdlib_category_name(err->category()), err->what());
                return code;
            }
            g_ok++;
            if (strcmp(cmd, "eq") == 0)
                print("TQ eq", a, x, matched);
            else
                print("TQ range", a, x, y, matched);
            tq_list_matching(rows, field, x, y);
            return 0;
        }

        if (strcmp(cmd, "count") == 0) {
            code = tq_count_matching(rows, field, x, y, &value, err);
            if (code != 0) {
                g_refused++;
                print("TQ count REFUSED", code,
                      stdlib_category_name(err->category()), err->what());
                return code;
            }
            g_ok++;
            print("TQ count", a, x, value);
            return 0;
        }

        if (strcmp(cmd, "sum") == 0) {
            code = tq_sum_matching(rows, field, x, y, &value, err);
            if (code != 0) {
                g_refused++;
                print("TQ sum REFUSED", code,
                      stdlib_category_name(err->category()), err->what());
                return code;
            }
            g_ok++;
            print("TQ sum", a, x, value);
            return 0;
        }

        if (strcmp(cmd, "expect_count") == 0) {
            if (c == NULL || tq_parse_int(c, &want) != 0) {
                g_failed++;
                print("TQ expect_count BAD_INPUT");
                return CPLUS_EXIT_UNSUPPORTED;
            }
            code = tq_count_matching(rows, field, x, y, &value, err);
            if (code != 0) {
                g_failed++;
                print("TQ expect_count ERROR", code);
                return code;
            }
            if (value == want) {
                g_ok++;
                print("TQ expect_count", a, x, "OK");
                return 0;
            }
            g_failed++;
            print("TQ expect_count", a, x, "FAIL", value, want);
            return CPLUS_EXIT_BOUNDS;
        }

        if (strcmp(cmd, "expect_sum") == 0) {
            if (c == NULL || tq_parse_int(c, &want) != 0) {
                g_failed++;
                print("TQ expect_sum BAD_INPUT");
                return CPLUS_EXIT_UNSUPPORTED;
            }
            code = tq_sum_matching(rows, field, x, y, &value, err);
            if (code != 0) {
                g_failed++;
                print("TQ expect_sum ERROR", code);
                return code;
            }
            if (value == want) {
                g_ok++;
                print("TQ expect_sum", a, x, "OK");
                return 0;
            }
            g_failed++;
            print("TQ expect_sum", a, x, "FAIL", value, want);
            return CPLUS_EXIT_BOUNDS;
        }

        /* expect_sum_refusal: the aggregate must be refused with `want`. */
        code = tq_sum_matching(rows, field, x, y, &value, err);
        if (code == want) {
            g_ok++;
            print("TQ expect_sum_refusal", a, x, "OK");
            return 0;
        }
        g_failed++;
        print("TQ expect_sum_refusal", a, x, "FAIL", code, want);
        return CPLUS_EXIT_BOUNDS;
    }

    if (strcmp(cmd, "check") == 0) {
        g_ops++;
        if (a == NULL || tq_parse_int(a, &want) != 0) {
            g_failed++;
            print("TQ check BAD_INPUT");
            return CPLUS_EXIT_UNSUPPORTED;
        }
        value = tq_count(rows);
        if (value == want) {
            g_ok++;
            print("TQ check", want, "OK");
            return 0;
        }
        g_failed++;
        print("TQ check", want, "FAIL", value);
        return CPLUS_EXIT_BOUNDS;
    }

    tq_refuse(err, STDLIB_CATEGORY_INVALID, CPLUS_EXIT_UNSUPPORTED,
              "unknown command");
    g_ops++;
    g_refused++;
    print("TQ", cmd, "REFUSED", err->code(), err->what());
    return err->code();
}

int main(int argc, char **argv) {
    vector rows = vector();
    intmap ids = intmap();
    error err = error();
    int i;

    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();

    i = 1;
    while (i < argc) {
        char *cmd = tq_argv_at(argv, i);
        char *a = i + 1 < argc ? tq_argv_at(argv, i + 1) : NULL;
        char *b = i + 2 < argc ? tq_argv_at(argv, i + 2) : NULL;
        char *c = i + 3 < argc ? tq_argv_at(argv, i + 3) : NULL;
        int used = 1;

        if (strcmp(cmd, "row") == 0) {
            used = 4;
        } else if (strcmp(cmd, "range") == 0 ||
                   strcmp(cmd, "expect_count") == 0 ||
                   strcmp(cmd, "expect_sum") == 0 ||
                   strcmp(cmd, "expect_sum_refusal") == 0) {
            used = 4;
        } else if (strcmp(cmd, "eq") == 0 || strcmp(cmd, "count") == 0 ||
                   strcmp(cmd, "sum") == 0) {
            used = 3;
        } else if (strcmp(cmd, "check") == 0) {
            used = 2;
        }
        if (i + used > argc) used = 1;
        tq_command(&rows, &ids, cmd, a, b, c, &err);
        i += used;
    }

    print("TQ_SUMMARY", g_ops, g_ok, g_refused, g_failed);
    rows.drop();
    ids.drop();
    return g_failed == 0 ? 0 : 1;
}
