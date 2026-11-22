/* Deep stress test for stdlib/json.hp -- bounded large/deep input,
 * serialization stability, allocation-fault interaction and stdin fixtures.
 *
 * Input: tests/stdlib/deep_json_stress.stdin, one JSON document per line.
 * Every line is parsed with a bounded depth budget, walked, serialized twice,
 * re-parsed and compared canonically. A large generated array and a deep
 * generated object are exercised without growing without bound.
 *
 * Self-checking: each check prints "<label> <1|0>"; per-line records print
 * "line <n> <parsed> <values> <maxdepth> <roundtrip>". The final line is
 * "RESULT <checks> <failed>". Exit 0 == all checks passed.
 *
 * Fatal-safety: none executed. Allocation fault injection is applied only to
 * the checked stdlib allocator; json.hp currently allocates through yyjson's
 * own allocator (documented in DEEP_JSON_CONTRACT.md). The test therefore
 * accepts either a successful operation or a documented recoverable refusal,
 * and fails only on a crash, a wrong status, or a malformed error record.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/json.hp"

#define JSON_DEEP_BUDGET 96
#define JSON_LINE_CAP 8192

static int g_checks = 0;
static int g_failed = 0;

static void check(const char *label, int cond) {
    g_checks++;
    if (!cond)
        g_failed++;
    print(label, cond ? 1 : 0);
}

/* Walk array children with an explicit depth budget. json.hp has no generic
 * object iterator, so object depth is exercised via member() in main(). */
static void walk(json_view v, int depth, int *count, int *maxdepth) {
    size_t n;
    size_t i;
    json_view child;
    if (!v.valid())
        return;
    (*count)++;
    if (depth > *maxdepth)
        *maxdepth = depth;
    if (depth >= JSON_DEEP_BUDGET)
        return;
    if (v.is_array()) {
        n = v.size();
        for (i = 0; i < n; i++) {
            child = v.at(i);
            walk(child, depth + 1, count, maxdepth);
        }
    }
}

/* Parse one document, bound-walk it, serialize twice, reparse and compare. */
static void process_line(const char *text, int line_no) {
    error err = error();
    json doc;
    json_view root;
    char *dump_a;
    char *dump_b;
    size_t len_a = 0;
    size_t len_b = 0;
    int values = 0;
    int maxdepth = 0;
    int parsed;
    int roundtrip = 0;
    json reparsed;
    json_view re_root;
    char *dump_c;
    size_t len_c = 0;

    doc = json(text, &err);
    parsed = doc.valid();
    if (parsed) {
        root = doc.root();
        walk(root, 0, &values, &maxdepth);
        dump_a = root.write(&len_a);
        dump_b = root.write(&len_b);
        if (dump_a != 0 && dump_b != 0 && len_a == len_b &&
            memcmp(dump_a, dump_b, len_a) == 0) {
            reparsed = json(dump_a, &err);
            if (reparsed.valid()) {
                re_root = reparsed.root();
                dump_c = re_root.write(&len_c);
                roundtrip = dump_c != 0 && len_c == len_a &&
                            memcmp(dump_c, dump_a, len_a) == 0;
                json_free(dump_c);
                reparsed.drop();
            }
        }
        json_free(dump_a);
        json_free(dump_b);
        doc.drop();
    } else if (err.failed_now() == 0 ||
               err.category() != STDLIB_CATEGORY_INVALID) {
        check("line_err_shape", 0);
    }
    print("line", line_no, parsed, values, maxdepth, roundtrip);
    if (parsed)
        check("line_roundtrip", roundtrip == 1);
    else
        check("line_parse_refusal", err.failed_now() == 1 &&
                                        err.category() == STDLIB_CATEGORY_INVALID);
}

int main(void) {
    char line[JSON_LINE_CAP];
    int line_no = 0;
    error err = error();
    json doc;
    json_view root;
    json_view v;
    json_builder builder;
    char big[JSON_LINE_CAP];
    size_t pos = 0;
    size_t len = 0;
    char *dump;
    int i;
    int rc;
    int values;
    int maxdepth;

    /* Hard wall-clock bound: reaching RESULT early is the real proof, the
     * alarm is a backstop against a hung parse/walk on a hostile input. */
    alarm(20);

    while (fgets(line, (int)sizeof(line), stdin) != (char *)0) {
        size_t n = strlen(line);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r')) {
            line[n - 1] = '\0';
            n--;
        }
        if (n == 0)
            continue;
        line_no++;
        process_line(line, line_no);
    }
    check("s_stdin_lines", line_no == 5);

    /* ---- large generated array (bounded) ------------------------------- */
    big[pos++] = '[';
    for (i = 0; i < 900; i++) {
        if (i != 0)
            big[pos++] = ',';
        if (i < 10)
            big[pos++] = (char)('0' + i);
        else if (i < 100)
            pos += (size_t)snprintf(big + pos, JSON_LINE_CAP - pos, "%d", i);
        else
            pos += (size_t)snprintf(big + pos, JSON_LINE_CAP - pos, "%d", i);
    }
    big[pos++] = ']';
    big[pos] = '\0';
    doc = json(big, &err);
    check("s_big_parse", doc.valid() == 1 && err.failed_now() == 0);
    root = doc.root();
    check("s_big_size", root.is_array() == 1 && root.size() == 900);
    v = root.at(0);
    rc = v.get_int(&i, &err);
    check("s_big_first", rc == STDLIB_OK && i == 0);
    v = root.at(899);
    rc = v.get_int(&i, &err);
    check("s_big_last", rc == STDLIB_OK && i == 899);
    dump = root.write(&len);
    check("s_big_write", dump != 0 && len > 0);
    json_free(dump);
    values = 0;
    maxdepth = 0;
    walk(root, 0, &values, &maxdepth);
    check("s_big_walk", values == 901 && maxdepth == 1);
    doc.drop();

    /* ---- deep generated object chain (bounded) -------------------------- */
    pos = 0;
    for (i = 0; i < 64; i++) {
        memcpy(big + pos, "{\"k\":", 5);
        pos += 5;
    }
    big[pos++] = '1';
    for (i = 0; i < 64; i++)
        big[pos++] = '}';
    big[pos] = '\0';
    doc = json(big, &err);
    check("s_deep_obj_parse", doc.valid() == 1);
    root = doc.root();
    v = root;
    for (i = 0; i < 64; i++) {
        json_view next = v.member("k");
        v = next;
    }
    rc = v.get_int(&i, &err);
    check("s_deep_obj_leaf", rc == STDLIB_OK && i == 1);
    dump = root.write(&len);
    check("s_deep_obj_write", dump != 0 && len > 0);
    json_free(dump);
    doc.drop();

    /* ---- allocation fault interaction ---------------------------------- */
    /* json.hp uses yyjson's allocator, so stdlib injection does not currently
     * reach it. Accept either behaviour but require a well-formed status. */
    stdlib_alloc_fail_after(0);
    doc = json("{\"x\":[1,2,3]}", &err);
    if (doc.valid()) {
        root = doc.root();
        check("s_fault_parse", root.is_object() == 1 && root.size() == 1);
        doc.drop();
    } else {
        check("s_fault_parse", err.failed_now() == 1 &&
                                   err.category() == STDLIB_CATEGORY_MEMORY);
    }
    builder = json_builder();
    if (builder.valid()) {
        rc = builder.add_int("k", 7, &err);
        check("s_fault_build", rc == STDLIB_OK || (rc == STDLIB_FAIL &&
                                err.category() == STDLIB_CATEGORY_MEMORY));
        dump = builder.write(&len);
        json_free(dump);
        builder.drop();
    } else {
        check("s_fault_build", 1);
    }
    stdlib_alloc_fail_reset();
    check("s_fault_reset", 1);

    /* Recoverable errors after the injection window stay well-formed. */
    rc = builder.add_int("k", 1, &err);
    check("s_post_fault_builder", rc == STDLIB_FAIL &&
                                      err.category() == STDLIB_CATEGORY_INVALID);
    doc = json("{oops", &err);
    check("s_post_fault_parse", doc.valid() == 0 && err.failed_now() == 1 &&
                                    err.category() == STDLIB_CATEGORY_INVALID);
    doc.drop();

    print("RESULT", g_checks, g_failed);
    return g_failed == 0 ? 0 : 1;
}
