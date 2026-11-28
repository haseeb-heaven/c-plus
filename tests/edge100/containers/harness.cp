/* EDGE100 containers -- exhaustive edge-case campaign for every public API in
 * stdlib/string.hp, stdlib/vector.hp and stdlib/collections.hp.
 *
 * Design
 * ------
 *  - Every public API is registered once in g_apis[] with a handler and a
 *    bitmask of the edge categories that apply to it (see API_MANIFEST.md).
 *  - Nine categories are probed where applicable: valid, NULL/empty,
 *    exact-fit, one-over, SIZE_MAX, self-alias, double-drop, invalid-index and
 *    allocation-failure atomicity.  A category that cannot apply to an API
 *    (e.g. invalid-index for a zero-argument accessor) is marked N/A, not
 *    silently skipped.
 *  - One fork per case.  The child writes a small verdict struct over a pipe;
 *    the parent classifies signals, fatal exits, short reads, wrong case IDs,
 *    canary corruption and failed assertions as FAIL.  Nothing counts as a
 *    pass unless the child exited cleanly, reported PASS and left the canary
 *    intact.
 *  - STDLIB_ALLOC_LIMIT is pinned to 4096 so exact-fit / one-over / overflow
 *    boundaries are reachable without large allocations.  Allocation failure
 *    is injected with stdlib_alloc_fail_at() at every fallible step of a
 *    multi-buffer rebuild, then the operation's atomicity is asserted.
 *  - No source or header is modified; this file lives only under
 *    tests/edge100/containers/.
 *
 * Build (from repo root):
 *   ./build/c+ build tests/edge100/containers/harness.cp \
 *     --default --sanitize=none -I. \
 *     -o tests/edge100/containers/out/harness
 */
#define STDLIB_ALLOC_LIMIT 4096
#define LIMIT 4096

#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/collections.hp"

/* ---- categories --------------------------------------------------------- */
enum {
    CAT_VALID = 0,
    CAT_NULL_EMPTY = 1,
    CAT_EXACT_FIT = 2,
    CAT_ONE_OVER = 3,
    CAT_SIZE_MAX = 4,
    CAT_SELF_ALIAS = 5,
    CAT_DOUBLE_DROP = 6,
    CAT_INVALID_INDEX = 7,
    CAT_ALLOC_FAIL = 8,
    N_CATS
};

#define CV (1u << CAT_VALID)
#define CN (1u << CAT_NULL_EMPTY)
#define CX (1u << CAT_EXACT_FIT)
#define CO (1u << CAT_ONE_OVER)
#define CS (1u << CAT_SIZE_MAX)
#define CA (1u << CAT_SELF_ALIAS)
#define CD (1u << CAT_DOUBLE_DROP)
#define CI (1u << CAT_INVALID_INDEX)
#define CF (1u << CAT_ALLOC_FAIL)

static const char *g_cat_names[N_CATS] = {
    "valid", "null_empty", "exact_fit", "one_over", "size_max",
    "self_alias", "double_drop", "invalid_index", "alloc_fail"
};

#define CK(p, c) do { if (!(c)) { (p) = 0; } } while (0)

/* ---- canary and verdict ------------------------------------------------- */
static unsigned char g_canary[256];

static void canary_init(void) {
    size_t i;
    for (i = 0; i < sizeof(g_canary); ++i)
        g_canary[i] = (unsigned char)0xA5;
}

static int canary_ok(void) {
    size_t i;
    for (i = 0; i < sizeof(g_canary); ++i)
        if (g_canary[i] != (unsigned char)0xA5)
            return 0;
    return 1;
}

struct verdict {
    int api;
    int cat;
    int pass;
    int canary;
};

/* ======================================================================== */
/* string.hp -- free functions                                               */
/* ======================================================================== */

static void api_STR_utf8_validate(int cat, int *pass) {
    char v[4];
    v[0] = 'a'; v[1] = (char)0x80; v[2] = (char)0x80; v[3] = '\0';
    switch (cat) {
    case CAT_VALID:
        CK(*pass, stdlib_utf8_validate(v, 1) == 1);
        CK(*pass, stdlib_utf8_validate("\xC3\xA9", 2) == 1);
        CK(*pass, stdlib_utf8_validate(v, 2) == 0);
        CK(*pass, stdlib_utf8_validate("\xC0\xAF", 2) == 0);
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, stdlib_utf8_validate(NULL, 0) == 1);
        CK(*pass, stdlib_utf8_validate(NULL, 1) == 0);
        CK(*pass, stdlib_utf8_validate("", 0) == 1);
        break;
    case CAT_EXACT_FIT:
        CK(*pass, stdlib_utf8_validate(v, 1) == 1);
        CK(*pass, stdlib_utf8_validate("\xF0\x90\x80\x80", 4) == 1);
        break;
    case CAT_ONE_OVER:
        CK(*pass, stdlib_utf8_validate(v, 2) == 0);
        CK(*pass, stdlib_utf8_validate("\xF0\x90\x80", 3) == 0);
        break;
    case CAT_SIZE_MAX:
        CK(*pass, stdlib_utf8_validate(v, SIZE_MAX) == 0);
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_calculate_growth(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID:
        CK(*pass, stdlib_string_calculate_growth(10u, 4u) == 10u);
        CK(*pass, stdlib_string_calculate_growth(6u, 4u) == 8u);
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, stdlib_string_calculate_growth(0u, 0u) == 0u);
        CK(*pass, stdlib_string_calculate_growth(1u, 0u) == 1u);
        break;
    case CAT_EXACT_FIT:
        CK(*pass, stdlib_string_calculate_growth((size_t)LIMIT, 0u) == (size_t)LIMIT);
        break;
    case CAT_ONE_OVER:
        CK(*pass, stdlib_string_calculate_growth((size_t)LIMIT + 1u, 0u) == 0u);
        break;
    case CAT_SIZE_MAX:
        CK(*pass, stdlib_string_calculate_growth(SIZE_MAX, 0u) == 0u);
        CK(*pass, stdlib_string_calculate_growth(4u, SIZE_MAX) == (size_t)LIMIT);
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_in(int cat, int *pass) {
    char big[4096];
    size_t i;
    for (i = 0; i < sizeof(big) - 1u; ++i)
        big[i] = 'z';
    big[sizeof(big) - 1u] = '\0';
    switch (cat) {
    case CAT_VALID: {
        string s = string_in((const stdlib_allocator *)0, "hello");
        CK(*pass, s.valid() == 1 && s.size() == 5u && s.capacity() == 6u);
        CK(*pass, s.equals("hello") == 1);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string_in((const stdlib_allocator *)0, NULL);
        string t = string_in((const stdlib_allocator *)0, "");
        CK(*pass, s.valid() == 1 && s.size() == 0u && s.c_str()[0] == '\0');
        CK(*pass, t.valid() == 1 && t.size() == 0u && t.c_str()[0] == '\0');
        s.drop(); t.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string_in((const stdlib_allocator *)0, big);
        CK(*pass, s.valid() == 1 && s.size() == (size_t)LIMIT - 1u);
        CK(*pass, s.capacity() == (size_t)LIMIT);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        char over[4098];
        for (i = 0; i < (size_t)LIMIT; ++i)
            over[i] = 'z';
        over[(size_t)LIMIT] = '\0';
        {
            string s = string_in((const stdlib_allocator *)0, over);
            CK(*pass, s.valid() == 1 && s.size() == 0u && s.capacity() == 1u);
            s.drop();
        }
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s;
        stdlib_alloc_fail_at(2);
        s = string_in((const stdlib_allocator *)0, "x");
        stdlib_alloc_fail_reset();
        CK(*pass, s.valid() == 0 && s.size() == 0u && s.capacity() == 1u && s.c_str()[0] == '\0');
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- string_view (direct C-style calls: C+ resolves method names globally,
 *      and string_view::size/data/is_valid_utf8 collide with string methods) */

static void api_STR_view_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string_view v = string_view("abcd", 4u);
        CK(*pass, string_view_size(&v) == 4u && string_view_data(&v) != NULL);
        CK(*pass, strcmp(string_view_data(&v), "abcd") == 0);
        break;
    }
    case CAT_NULL_EMPTY: {
        string_view e = string_view(NULL, 0u);
        string_view n = string_view(NULL, 3u);
        CK(*pass, string_view_size(&e) == 0u && string_view_data(&e) == NULL);
        CK(*pass, string_view_size(&n) == 3u && string_view_data(&n) == NULL);
        CK(*pass, string_view_equals(&n, "abc") == 0);
        CK(*pass, string_view_is_valid_utf8(&n) == 0);
        break;
    }
    case CAT_SIZE_MAX: {
        string_view v = string_view("x", SIZE_MAX);
        CK(*pass, string_view_size(&v) == SIZE_MAX && string_view_data(&v) != NULL);
        CK(*pass, string_view_data(&v)[0] == 'x');
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_view_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        string_view v = s.to_view();
        CK(*pass, string_view_size(&v) == 5u);
        s.clear();
        {
            string_view z = s.to_view();
            CK(*pass, string_view_size(&z) == 0u);
        }
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string_view e = string_view(NULL, 0u);
        CK(*pass, string_view_size(&e) == 0u);
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("x");
        s.drop(); s.drop();
        {
            string_view v = s.to_view();
            CK(*pass, string_view_size(&v) == 0u);
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_view_data(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("abc");
        string_view v = s.to_view();
        CK(*pass, string_view_data(&v) == s.c_str());
        CK(*pass, string_view_data(&v)[0] == 'a' && string_view_data(&v)[2] == 'c');
        CK(*pass, string_view_data(&v)[3] == '\0');
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string("x");
        s.drop();
        {
            string_view v = s.to_view();
            CK(*pass, string_view_data(&v) == NULL);
        }
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("y");
        s.drop(); s.drop();
        {
            string_view v = s.to_view();
            CK(*pass, string_view_data(&v) == NULL);
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_view_equals(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string_view v = string_view("hello", 5u);
        CK(*pass, string_view_equals(&v, "hello") == 1);
        CK(*pass, string_view_equals(&v, "hell") == 0);
        CK(*pass, string_view_equals(&v, "hellox") == 0);
        break;
    }
    case CAT_NULL_EMPTY: {
        string_view v = string_view("abc", 3u);
        string_view e = string_view("", 0u);
        CK(*pass, string_view_equals(&v, NULL) == 0);
        CK(*pass, string_view_equals(&e, "") == 1);
        CK(*pass, string_view_equals(&e, "x") == 0);
        break;
    }
    case CAT_EXACT_FIT: {
        string_view v = string_view("abc", 3u);
        CK(*pass, string_view_equals(&v, "abc") == 1);
        break;
    }
    case CAT_ONE_OVER: {
        string_view v = string_view("abc", 3u);
        CK(*pass, string_view_equals(&v, "abcd") == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_view_is_valid_utf8(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string_view a = string_view("abc", 3u);
        string_view b = string_view("\xC3\xA9", 2u);
        string_view c = string_view("\xC0\xAF", 2u);
        CK(*pass, string_view_is_valid_utf8(&a) == 1);
        CK(*pass, string_view_is_valid_utf8(&b) == 1);
        CK(*pass, string_view_is_valid_utf8(&c) == 0);
        break;
    }
    case CAT_NULL_EMPTY: {
        string_view e = string_view(NULL, 0u);
        string_view n = string_view(NULL, 2u);
        CK(*pass, string_view_is_valid_utf8(&e) == 1);
        CK(*pass, string_view_is_valid_utf8(&n) == 0);
        break;
    }
    case CAT_EXACT_FIT: {
        string_view v = string_view("\xF0\x90\x80\x80", 4u);
        CK(*pass, string_view_is_valid_utf8(&v) == 1);
        break;
    }
    case CAT_ONE_OVER: {
        string_view v = string_view("a\x80", 2u);
        CK(*pass, string_view_is_valid_utf8(&v) == 0);
        break;
    }
    case CAT_SIZE_MAX: {
        string_view v = string_view("\x80", SIZE_MAX);
        CK(*pass, string_view_is_valid_utf8(&v) == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- string class ------------------------------------------------------- */

static void api_STR_string_ctor(int cat, int *pass) {
    char big[4096];
    size_t i;
    for (i = 0; i < sizeof(big) - 1u; ++i)
        big[i] = 'q';
    big[sizeof(big) - 1u] = '\0';
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        CK(*pass, s.valid() == 1 && s.size() == 5u && s.capacity() == 6u);
        CK(*pass, strcmp(s.c_str(), "hello") == 0);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string a = string(NULL);
        string b = string("");
        CK(*pass, a.valid() == 1 && a.size() == 0u && a.capacity() == 1u && a.c_str()[0] == '\0');
        CK(*pass, b.valid() == 1 && b.size() == 0u && b.capacity() == 1u);
        a.drop(); b.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string(big);
        CK(*pass, s.valid() == 1 && s.size() == (size_t)LIMIT - 1u && s.capacity() == (size_t)LIMIT);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        char over[4098];
        for (i = 0; i < (size_t)LIMIT; ++i)
            over[i] = 'q';
        over[(size_t)LIMIT] = '\0';
        {
            string s = string(over);
            CK(*pass, s.valid() == 0 && s.size() == 0u && s.capacity() == 0u);
            s.drop();
        }
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s;
        stdlib_alloc_fail_at(1);
        s = string("abc");
        stdlib_alloc_fail_reset();
        CK(*pass, s.valid() == 0 && s.size() == 0u && s.c_str()[0] == '\0');
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("abcd");
        CK(*pass, s.size() == 4u);
        s.append("ef");
        CK(*pass, s.size() == 6u);
        s.clear();
        CK(*pass, s.size() == 0u);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        CK(*pass, s.size() == 0u);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("ab");
        s.drop(); s.drop();
        CK(*pass, s.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s;
        stdlib_alloc_fail_at(1);
        s = string("abc");
        stdlib_alloc_fail_reset();
        CK(*pass, s.size() == 0u);
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_capacity(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("abc");
        CK(*pass, s.capacity() == 4u);
        CK(*pass, s.reserve(20u) == 0 && s.capacity() >= 20u);
        s.drop(); CK(*pass, s.capacity() == 0u);
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        CK(*pass, s.capacity() == 1u);
        s.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc");
        CK(*pass, s.reserve(4u) == 0 && s.capacity() == 4u);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("abc");
        s.drop(); s.drop();
        CK(*pass, s.capacity() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s = string("abc");
        size_t c0 = s.capacity();
        stdlib_alloc_fail_at(1);
        CK(*pass, s.reserve(50u) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, s.capacity() == c0);
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_valid(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("a");
        CK(*pass, s.valid() == 1);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        CK(*pass, s.valid() == 1);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("a");
        s.drop();
        CK(*pass, s.valid() == 0);
        s.drop();
        CK(*pass, s.valid() == 0);
        s.append("b");
        CK(*pass, s.valid() == 1);
        s.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s;
        stdlib_alloc_fail_at(1);
        s = string("x");
        stdlib_alloc_fail_reset();
        CK(*pass, s.valid() == 0);
        s.append("y");
        CK(*pass, s.valid() == 1);
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_at(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        CK(*pass, s.at(0) == 'h' && s.at(4) == 'o');
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        CK(*pass, s.at(0) == '\0');
        s.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc");
        CK(*pass, s.at(s.size() - 1u) == 'c');
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("abc");
        CK(*pass, s.at(s.size()) == '\0');
        s.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        string s = string("abc");
        CK(*pass, s.at(SIZE_MAX) == '\0');
        s.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        string s = string("abc");
        CK(*pass, s.at(4u) == '\0' && s.at(99u) == '\0');
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("abc");
        s.drop(); s.drop();
        CK(*pass, s.at(0) == '\0' && s.at(99u) == '\0');
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s;
        stdlib_alloc_fail_at(1);
        s = string("abc");
        stdlib_alloc_fail_reset();
        CK(*pass, s.at(0) == '\0' && s.at(2u) == '\0');
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_c_str(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hi");
        CK(*pass, s.c_str() != NULL && strcmp(s.c_str(), "hi") == 0);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        CK(*pass, s.c_str() != NULL && s.c_str()[0] == '\0');
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("hi");
        s.drop(); s.drop();
        CK(*pass, s.c_str() != NULL && s.c_str()[0] == '\0');
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s;
        stdlib_alloc_fail_at(1);
        s = string("x");
        stdlib_alloc_fail_reset();
        CK(*pass, s.c_str() != NULL && s.c_str()[0] == '\0');
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_equals(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        CK(*pass, s.equals("hello") == 1);
        CK(*pass, s.equals("hell") == 0 && s.equals("hellox") == 0);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string("hello");
        string e = string(NULL);
        CK(*pass, s.equals(NULL) == 0);
        CK(*pass, e.equals("") == 1 && e.equals("x") == 0);
        s.drop(); e.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc");
        CK(*pass, s.equals("abc") == 1);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("abc");
        CK(*pass, s.equals("abcd") == 0);
        s.drop();
        break;
    }
    case CAT_SELF_ALIAS: {
        string s = string("self");
        CK(*pass, s.equals(s.c_str()) == 1);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("x");
        s.drop(); s.drop();
        CK(*pass, s.equals("") == 1 && s.equals("x") == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_compare(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        CK(*pass, s.compare("hello") == 0);
        CK(*pass, s.compare("hell") == 1 && s.compare("hellox") == -1);
        CK(*pass, s.compare("a") == 1 && s.compare("z") == -1);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string("hello");
        string e = string(NULL);
        CK(*pass, s.compare(NULL) == 1);
        CK(*pass, e.compare("") == 0 && e.compare("x") == -1);
        s.drop(); e.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc");
        CK(*pass, s.compare("abc") == 0);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("abc");
        CK(*pass, s.compare("abcd") == -1);
        s.drop();
        break;
    }
    case CAT_SELF_ALIAS: {
        string s = string("abc");
        CK(*pass, s.compare(s.c_str()) == 0);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("x");
        s.drop(); s.drop();
        CK(*pass, s.compare("") == 0 && s.compare("x") == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_find(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello world");
        CK(*pass, s.find("ll") == 2L && s.find("world") == 6L);
        CK(*pass, s.find("z") == -1L && s.find("") == 0L);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string("hello");
        string e = string(NULL);
        CK(*pass, s.find(NULL) == -1L);
        CK(*pass, e.find("") == 0L && e.find("x") == -1L);
        s.drop(); e.drop();
        break;
    }
    case CAT_SELF_ALIAS: {
        string s = string("abcdef");
        CK(*pass, s.find(s.c_str() + 2) == 2L);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("x");
        s.drop(); s.drop();
        CK(*pass, s.find("x") == -1L);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_reserve(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("abc");
        CK(*pass, s.reserve(64u) == 0 && s.capacity() >= 64u);
        CK(*pass, strcmp(s.c_str(), "abc") == 0 && s.size() == 3u);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string("abc");
        CK(*pass, s.reserve(0u) == 0 && s.capacity() == 4u);
        s.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc");
        CK(*pass, s.reserve(4u) == 0 && s.capacity() == 4u);
        CK(*pass, s.reserve((size_t)LIMIT) == 0 && s.capacity() == (size_t)LIMIT);
        CK(*pass, strcmp(s.c_str(), "abc") == 0);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("abc");
        size_t c0 = s.capacity();
        CK(*pass, s.reserve((size_t)LIMIT + 1u) == -1);
        CK(*pass, s.capacity() == c0 && s.size() == 3u && strcmp(s.c_str(), "abc") == 0);
        s.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        string s = string("abc");
        size_t c0 = s.capacity();
        CK(*pass, s.reserve(SIZE_MAX) == -1);
        CK(*pass, s.capacity() == c0 && strcmp(s.c_str(), "abc") == 0);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("abc");
        s.drop(); s.drop();
        CK(*pass, s.reserve(8u) == 0 && s.valid() == 1 && s.capacity() == 8u);
        CK(*pass, s.c_str()[0] == '\0');
        s.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s = string("abc");
        const char *p0 = s.c_str();
        size_t c0 = s.capacity();
        stdlib_alloc_fail_at(1);
        CK(*pass, s.reserve(100u) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, s.c_str() == p0 && s.capacity() == c0 && s.size() == 3u);
        CK(*pass, strcmp(s.c_str(), "abc") == 0);
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_append(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        CK(*pass, s.append(" world") == 0);
        CK(*pass, strcmp(s.c_str(), "hello world") == 0 && s.size() == 11u);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string("abc");
        size_t n = s.size();
        CK(*pass, s.append(NULL) == -1);
        CK(*pass, s.size() == n && strcmp(s.c_str(), "abc") == 0);
        CK(*pass, s.append("") == 0 && s.size() == n);
        s.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc");
        CK(*pass, s.reserve(7u) == 0);
        CK(*pass, s.append("def") == 0);
        CK(*pass, strcmp(s.c_str(), "abcdef") == 0 && s.capacity() == 7u);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("abc");
        char over[4097];
        size_t i;
        for (i = 0; i < (size_t)LIMIT; ++i)
            over[i] = 'z';
        over[(size_t)LIMIT] = '\0';
        CK(*pass, s.append(over) == -1);
        CK(*pass, s.size() == 3u && strcmp(s.c_str(), "abc") == 0);
        s.drop();
        break;
    }
    case CAT_SELF_ALIAS: {
        string a = string("abcdef");
        string b = string("abcdef");
        CK(*pass, b.reserve(40u) == 0);
        CK(*pass, a.append(a.c_str() + 2u) == 0);
        CK(*pass, b.append(b.c_str() + 2u) == 0);
        CK(*pass, strcmp(a.c_str(), "abcdefcdef") == 0);
        CK(*pass, strcmp(b.c_str(), "abcdefcdef") == 0);
        CK(*pass, a.size() == 10u && b.size() == 10u);
        a.drop(); b.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("abc");
        s.drop(); s.drop();
        CK(*pass, s.append("again") == 0 && s.valid() == 1 && s.size() == 5u);
        CK(*pass, strcmp(s.c_str(), "again") == 0);
        s.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        string s = string("abc");
        const char *p0 = s.c_str();
        size_t c0 = s.capacity();
        stdlib_alloc_fail_at(1);
        CK(*pass, s.append("defgh") == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, s.c_str() == p0 && s.capacity() == c0 && s.size() == 3u);
        CK(*pass, strcmp(s.c_str(), "abc") == 0);
        s.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_clear(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        size_t c = s.capacity();
        s.clear();
        CK(*pass, s.size() == 0u && s.capacity() == c && s.valid() == 1 && s.c_str()[0] == '\0');
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        s.clear();
        CK(*pass, s.size() == 0u && s.c_str()[0] == '\0');
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("x");
        s.drop(); s.drop();
        s.clear();
        CK(*pass, s.size() == 0u && s.valid() == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_copy_from(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string a = string("dest");
        string b = string("source");
        CK(*pass, a.copy_from(&b) == 0);
        CK(*pass, strcmp(a.c_str(), "source") == 0 && a.size() == 6u && a.capacity() == 7u);
        a.drop(); b.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string a = string("dest");
        string e = string(NULL);
        CK(*pass, a.copy_from(NULL) == -1 && strcmp(a.c_str(), "dest") == 0);
        CK(*pass, a.copy_from(&e) == 0 && a.size() == 0u && a.valid() == 1 && a.c_str()[0] == '\0');
        a.drop(); e.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        char big[4096];
        size_t i;
        for (i = 0; i < (size_t)LIMIT - 1u; ++i)
            big[i] = 'k';
        big[(size_t)LIMIT - 1u] = '\0';
        {
            string src = string(big);
            string dst = string("x");
            CK(*pass, dst.copy_from(&src) == 0 && dst.size() == (size_t)LIMIT - 1u);
            CK(*pass, dst.capacity() == (size_t)LIMIT);
            dst.drop(); src.drop();
        }
        break;
    }
    case CAT_SELF_ALIAS: {
        string s = string("self");
        CK(*pass, s.copy_from(&s) == 0 && strcmp(s.c_str(), "self") == 0 && s.size() == 4u);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string src = string("abc");
        string dst = string("z");
        src.drop(); src.drop();
        CK(*pass, dst.copy_from(&src) == 0 && dst.size() == 0u && dst.c_str()[0] == '\0');
        dst.drop(); src.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        string dst = string("dest");
        string src = string("source");
        const char *p0 = dst.c_str();
        size_t n0 = dst.size();
        size_t c0 = dst.capacity();
        stdlib_alloc_fail_at(1);
        CK(*pass, dst.copy_from(&src) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, dst.c_str() == p0 && dst.size() == n0 && dst.capacity() == c0);
        CK(*pass, strcmp(dst.c_str(), "dest") == 0);
        dst.drop(); src.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_is_valid_utf8(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string a = string("hello");
        string b = string("\xC3\xA9");
        string c = string("\xC0\xAF");
        CK(*pass, a.is_valid_utf8() == 1 && b.is_valid_utf8() == 1 && c.is_valid_utf8() == 0);
        a.drop(); b.drop(); c.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        CK(*pass, s.is_valid_utf8() == 1);
        s.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("\xF0\x90\x80\x80");
        CK(*pass, s.is_valid_utf8() == 1 && s.size() == 4u);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("a\x80");
        CK(*pass, s.is_valid_utf8() == 0);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("x");
        s.drop(); s.drop();
        CK(*pass, s.is_valid_utf8() == 1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_substr(int cat, int *pass) {
    char out[16];
    size_t i;
    for (i = 0; i < sizeof(out); ++i)
        out[i] = 'Z';
    switch (cat) {
    case CAT_VALID: {
        string s = string("abcdef");
        CK(*pass, s.substr(1u, 3u, out, sizeof(out)) == 0);
        CK(*pass, strcmp(out, "bcd") == 0);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string("abc");
        string e = string(NULL);
        CK(*pass, s.substr(0u, 1u, NULL, sizeof(out)) == -1);
        CK(*pass, s.substr(0u, 1u, out, 0u) == -1);
        CK(*pass, e.substr(0u, 4u, out, sizeof(out)) == 0 && out[0] == '\0');
        s.drop(); e.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc");
        CK(*pass, s.substr(0u, 3u, out, 4u) == 0 && strcmp(out, "abc") == 0);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("abcdef");
        CK(*pass, s.substr(1u, 4u, out, 3u) == 0 && strcmp(out, "bc") == 0);
        s.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        string s = string("abc");
        CK(*pass, s.substr(SIZE_MAX, 1u, out, sizeof(out)) == -1);
        CK(*pass, s.substr(0u, SIZE_MAX, out, 4u) == 0 && strcmp(out, "abc") == 0);
        s.drop();
        break;
    }
    case CAT_SELF_ALIAS: {
        string s = string("abcdef");
        CK(*pass, s.substr(0u, 3u, (char *)s.c_str(), 4u) == 0);
        CK(*pass, strncmp(s.c_str(), "abc", 3) == 0 && s.c_str()[3] == '\0');
        s.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        string s = string("abc");
        CK(*pass, s.substr(4u, 1u, out, sizeof(out)) == -1);
        CK(*pass, s.substr(3u, 1u, out, sizeof(out)) == 0 && out[0] == '\0');
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("abc");
        s.drop(); s.drop();
        CK(*pass, s.substr(0u, 1u, out, sizeof(out)) == 0 && out[0] == '\0');
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_count_fields(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("aa,bb,cc");
        CK(*pass, s.count_fields(',') == 3u);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        CK(*pass, s.count_fields(',') == 1u);
        s.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc");
        CK(*pass, s.count_fields(',') == 1u);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("a,");
        CK(*pass, s.count_fields(',') == 2u);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("a,b");
        s.drop(); s.drop();
        CK(*pass, s.count_fields(',') == 1u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_field(int cat, int *pass) {
    char out[16];
    size_t i;
    for (i = 0; i < sizeof(out); ++i)
        out[i] = 'Z';
    switch (cat) {
    case CAT_VALID: {
        string s = string("aa,bb,cc");
        CK(*pass, s.field(',', 0u, out, sizeof(out)) == 0 && strcmp(out, "aa") == 0);
        CK(*pass, s.field(',', 1u, out, sizeof(out)) == 0 && strcmp(out, "bb") == 0);
        CK(*pass, s.field(',', 2u, out, sizeof(out)) == 0 && strcmp(out, "cc") == 0);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string("aa,bb");
        CK(*pass, s.field(',', 0u, NULL, sizeof(out)) == -1);
        CK(*pass, s.field(',', 0u, out, 0u) == -1);
        s.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        string s = string("abc,def");
        CK(*pass, s.field(',', 1u, out, 4u) == 0 && strcmp(out, "def") == 0);
        s.drop();
        break;
    }
    case CAT_ONE_OVER: {
        string s = string("abcdef,ghi");
        CK(*pass, s.field(',', 1u, out, 3u) == 0 && strcmp(out, "gh") == 0);
        s.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        string s = string("a,b");
        CK(*pass, s.field(',', SIZE_MAX, out, sizeof(out)) == -1);
        s.drop();
        break;
    }
    case CAT_SELF_ALIAS: {
        string s = string("aaa,bbb");
        CK(*pass, s.field(',', 0u, (char *)s.c_str(), 4u) == 0);
        CK(*pass, strncmp(s.c_str(), "aaa", 3) == 0 && s.c_str()[3] == '\0');
        s.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        string s = string("a,b");
        CK(*pass, s.field(',', 99u, out, sizeof(out)) == -1);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("a,b");
        s.drop(); s.drop();
        CK(*pass, s.field(',', 0u, out, sizeof(out)) == 0 && out[0] == '\0');
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_to_view(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        string_view v = s.to_view();
        CK(*pass, v.size() == 5u && v.data() == s.c_str());
        CK(*pass, v.equals("hello") == 1 && v.is_valid_utf8() == 1);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        string_view v = s.to_view();
        CK(*pass, v.size() == 0u && v.equals("") == 1);
        s.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("x");
        s.drop(); s.drop();
        string_view v = s.to_view();
        CK(*pass, v.size() == 0u && v.data() == NULL && v.equals("") == 1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_STR_string_drop(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        string s = string("hello");
        s.drop();
        CK(*pass, s.valid() == 0 && s.size() == 0u && s.capacity() == 0u);
        s.append("new");
        CK(*pass, s.valid() == 1 && strcmp(s.c_str(), "new") == 0);
        s.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        string s = string(NULL);
        s.drop(); s.drop();
        CK(*pass, s.valid() == 0 && s.size() == 0u);
        break;
    }
    case CAT_DOUBLE_DROP: {
        string s = string("abc");
        s.drop();
        CK(*pass, s.valid() == 0 && s.c_str()[0] == '\0');
        s.drop();
        CK(*pass, s.valid() == 0 && s.at(0) == '\0' && s.size() == 0u && s.capacity() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ======================================================================== */
/* vector.hp -- free functions                                               */
/* ======================================================================== */

static void api_VEC_allocation_bytes(int cat, int *pass) {
    size_t bytes = 99u;
    switch (cat) {
    case CAT_VALID:
        CK(*pass, vector_allocation_bytes(4u, &bytes) == 0 && bytes == 16u);
        CK(*pass, vector_allocation_bytes(1u, &bytes) == 0 && bytes == 4u);
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, vector_allocation_bytes(0u, &bytes) == 0 && bytes == 0u);
        break;
    case CAT_EXACT_FIT:
        CK(*pass, vector_allocation_bytes((size_t)LIMIT / sizeof(int), &bytes) == 0);
        CK(*pass, bytes == (size_t)LIMIT);
        break;
    case CAT_ONE_OVER:
        bytes = 99u;
        CK(*pass, vector_allocation_bytes((size_t)LIMIT / sizeof(int) + 1u, &bytes) == -1);
        CK(*pass, bytes == 99u);
        break;
    case CAT_SIZE_MAX:
        CK(*pass, vector_allocation_bytes(SIZE_MAX, &bytes) == -1);
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_calculate_growth(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID:
        CK(*pass, vector_calculate_growth(4u, 7u) == 8u);
        CK(*pass, vector_calculate_growth(4u, 20u) == 20u);
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, vector_calculate_growth(0u, 0u) == 0u);
        CK(*pass, vector_calculate_growth(0u, 4u) == 4u);
        break;
    case CAT_EXACT_FIT:
        CK(*pass, vector_calculate_growth(0u, (size_t)LIMIT / sizeof(int)) ==
                  (size_t)LIMIT / sizeof(int));
        break;
    case CAT_ONE_OVER:
        CK(*pass, vector_calculate_growth(0u, (size_t)LIMIT / sizeof(int) + 1u) ==
                  (size_t)LIMIT / sizeof(int) + 1u);
        break;
    case CAT_SIZE_MAX:
        CK(*pass, vector_calculate_growth(SIZE_MAX, 4u) == (size_t)LIMIT / sizeof(int));
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_in(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        stdlib_allocator allocIface = stdlib_heap_allocator();
        vector vec = vector_in(&allocIface);
        CK(*pass, vec.size() == 0u && vec.capacity() == 0u);
        CK(*pass, vec.push_back(11) == 0 && vec.size() == 1u && vec.at(0) == 11);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector_in((const stdlib_allocator *)0);
        CK(*pass, vec.size() == 0u && vec.capacity() == 0u);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector_in((const stdlib_allocator *)0);
        vec.drop(); vec.drop();
        CK(*pass, vec.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        stdlib_allocator allocIface = stdlib_heap_allocator();
        vector vec = vector_in(&allocIface);
        stdlib_alloc_fail_at(1);
        CK(*pass, vec.push_back(3) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, vec.size() == 0u && vec.capacity() == 0u);
        vec.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- slice -------------------------------------------------------------- */

static void api_VEC_slice_ctor(int cat, int *pass) {
    int data[4];
    data[0] = 1; data[1] = 2; data[2] = 3; data[3] = 4;
    switch (cat) {
    case CAT_VALID: {
        slice sl = slice(data, 4u);
        CK(*pass, sl.size() == 4u && sl.valid() == 1 && sl.stale() == 0);
        break;
    }
    case CAT_NULL_EMPTY: {
        slice slE = slice(NULL, 0u);
        slice slN = slice(NULL, 3u);
        CK(*pass, slE.size() == 0u && slE.valid() == 0);
        CK(*pass, slN.size() == 3u && slN.valid() == 0);
        break;
    }
    case CAT_SIZE_MAX: {
        slice sl = slice(data, SIZE_MAX);
        CK(*pass, sl.size() == SIZE_MAX && sl.valid() == 1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_slice_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2); vec.push_back(3);
        slice sl = vec.view();
        CK(*pass, sl.size() == 3u);
        {
            slice slt = sl.sub(1u, 1u);
            CK(*pass, slt.size() == 1u);
        }
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        slice slE = slice(NULL, 0u);
        CK(*pass, slE.size() == 0u);
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        {
            slice sl = vec.view();
            CK(*pass, sl.size() == 0u);
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_slice_stale(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.reserve(16u);
        vec.push_back(1);
        slice sl = vec.view();
        CK(*pass, sl.stale() == 0);
        vec.push_back(2); /* no reallocation: generation unchanged */
        CK(*pass, sl.stale() == 0);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(1);
        slice sl = vec.view();
        vec.reserve(64u); /* reallocation bumps generation */
        CK(*pass, sl.stale() == 1 && sl.valid() == 0);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        slice sl = vec.view();
        vec.drop(); vec.drop();
        CK(*pass, sl.stale() == 1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_slice_valid(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(7);
        slice sl = vec.view();
        CK(*pass, sl.valid() == 1);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        slice slE = slice(NULL, 0u);
        slice slN = slice(NULL, 2u);
        CK(*pass, slE.valid() == 0 && slN.valid() == 0);
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(7);
        slice sl = vec.view();
        vec.reserve(64u);
        CK(*pass, sl.valid() == 0);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(7);
        vec.drop(); vec.drop();
        {
            slice sl = vec.view();
            CK(*pass, sl.valid() == 0);
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_slice_at_checked(int cat, int *pass) {
    int out = -99;
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(10); vec.push_back(20); vec.push_back(30);
        slice sl = vec.view();
        CK(*pass, sl.at_checked(0u, &out) == 0 && out == 10);
        CK(*pass, sl.at_checked(2u, &out) == 0 && out == 30);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        slice slE = slice(NULL, 0u);
        CK(*pass, slE.at_checked(0u, &out) == -1);
        CK(*pass, slE.at_checked(0u, NULL) == -1);
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        vec.push_back(5); vec.push_back(6);
        slice sl = vec.view();
        CK(*pass, sl.at_checked(1u, &out) == 0 && out == 6);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(5);
        slice sl = vec.view();
        CK(*pass, sl.at_checked(1u, &out) == -1);
        vec.reserve(32u);
        CK(*pass, sl.at_checked(0u, &out) == -1);
        vec.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        vector vec = vector();
        vec.push_back(5);
        slice sl = vec.view();
        CK(*pass, sl.at_checked(SIZE_MAX, &out) == -1);
        vec.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        vector vec = vector();
        vec.push_back(5);
        slice sl = vec.view();
        CK(*pass, sl.at_checked(99u, &out) == -1);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(5);
        vec.drop(); vec.drop();
        {
            slice sl = vec.view();
            CK(*pass, sl.at_checked(0u, &out) == -1);
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_slice_sub(int cat, int *pass) {
    int out = 0;
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2); vec.push_back(3); vec.push_back(4);
        slice sl = vec.view();
        slice slt = sl.sub(1u, 2u);
        CK(*pass, slt.size() == 2u && slt.valid() == 1);
        CK(*pass, slt.at_checked(0u, &out) == 0 && out == 2);
        CK(*pass, slt.at_checked(1u, &out) == 0 && out == 3);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        slice slE = slice(NULL, 0u);
        slice slt = slE.sub(0u, 1u);
        CK(*pass, slt.size() == 0u && slt.valid() == 0);
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2); vec.push_back(3);
        slice sl = vec.view();
        slice slt = sl.sub(1u, SIZE_MAX);
        CK(*pass, slt.size() == 2u && slt.at_checked(0u, &out) == 0 && out == 2);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        slice sl = vec.view();
        {
            slice slA = sl.sub(2u, 1u);
            CK(*pass, slA.size() == 0u && slA.valid() == 0);
        }
        vec.reserve(32u);
        {
            slice slB = sl.sub(0u, 1u);
            CK(*pass, slB.size() == 0u && slB.valid() == 0);
        }
        vec.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        vector vec = vector();
        vec.push_back(1);
        slice sl = vec.view();
        {
            slice slt = sl.sub(SIZE_MAX, 1u);
            CK(*pass, slt.size() == 0u && slt.valid() == 0);
        }
        vec.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        vector vec = vector();
        vec.push_back(1);
        slice sl = vec.view();
        {
            slice slt = sl.sub(3u, 1u);
            CK(*pass, slt.size() == 0u);
        }
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        {
            slice sl = vec.view();
            slice slt = sl.sub(0u, 1u);
            CK(*pass, slt.size() == 0u && slt.valid() == 0);
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- vector class ------------------------------------------------------- */

static void api_VEC_vector_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        CK(*pass, vec.size() == 0u && vec.capacity() == 0u);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.drop(); vec.drop();
        CK(*pass, vec.size() == 0u && vec.capacity() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        CK(*pass, vec.size() == 0u);
        vec.push_back(1); vec.push_back(2);
        CK(*pass, vec.size() == 2u);
        {
            int popped = -1;
            CK(*pass, vec.pop_back(&popped) == 0 && popped == 2);
        }
        CK(*pass, vec.size() == 1u);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        CK(*pass, vec.size() == 0u && vec.pop_back(NULL) == -1);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        CK(*pass, vec.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        vector vec = vector();
        stdlib_alloc_fail_at(1);
        CK(*pass, vec.push_back(1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, vec.size() == 0u);
        vec.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_capacity(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        CK(*pass, vec.capacity() == 0u);
        CK(*pass, vec.push_back(1) == 0 && vec.capacity() == 4u);
        CK(*pass, vec.reserve(32u) == 0 && vec.capacity() == 32u);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        CK(*pass, vec.reserve((size_t)LIMIT / sizeof(int)) == 0);
        CK(*pass, vec.capacity() == (size_t)LIMIT / sizeof(int));
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        CK(*pass, vec.capacity() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_reserve(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        CK(*pass, vec.reserve(32u) == 0 && vec.capacity() == 32u);
        CK(*pass, vec.size() == 2u && vec.at(0) == 1 && vec.at(1) == 2);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        CK(*pass, vec.reserve(0u) == 0 && vec.capacity() == 0u);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        CK(*pass, vec.reserve(4u) == 0 && vec.capacity() == 4u);
        CK(*pass, vec.reserve(4u) == 0 && vec.capacity() == 4u);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(9);
        CK(*pass, vec.reserve((size_t)LIMIT / sizeof(int) + 1u) == -1);
        CK(*pass, vec.size() == 1u && vec.at(0) == 9 && vec.capacity() == 4u);
        vec.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        vector vec = vector();
        vec.push_back(9);
        CK(*pass, vec.reserve(SIZE_MAX) == -1);
        CK(*pass, vec.size() == 1u && vec.at(0) == 9);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(9);
        vec.drop(); vec.drop();
        CK(*pass, vec.reserve(8u) == 0 && vec.capacity() == 8u && vec.size() == 0u);
        CK(*pass, vec.push_back(4) == 0 && vec.at(0) == 4);
        vec.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        size_t c0 = vec.capacity();
        stdlib_alloc_fail_at(1);
        CK(*pass, vec.reserve(64u) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, vec.capacity() == c0 && vec.size() == 2u);
        CK(*pass, vec.at(0) == 1 && vec.at(1) == 2);
        vec.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_push_back(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        CK(*pass, vec.push_back(10) == 0 && vec.push_back(20) == 0 && vec.push_back(30) == 0);
        CK(*pass, vec.size() == 3u && vec.at(0) == 10 && vec.at(2) == 30);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        int i;
        CK(*pass, vec.reserve(4u) == 0);
        for (i = 0; i < 4; ++i)
            CK(*pass, vec.push_back(i) == 0);
        CK(*pass, vec.size() == 4u && vec.capacity() == 4u);
        CK(*pass, vec.push_back(4) == 0 && vec.capacity() == 8u && vec.size() == 5u);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        size_t i;
        CK(*pass, vec.reserve(600u) == 0);
        for (i = 0; i < 600u; ++i)
            CK(*pass, vec.push_back((int)i) == 0);
        CK(*pass, vec.size() == 600u && vec.capacity() == 600u);
        CK(*pass, vec.push_back(999) == -1);
        CK(*pass, vec.size() == 600u && vec.capacity() == 600u);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        CK(*pass, vec.push_back(5) == 0 && vec.size() == 1u && vec.at(0) == 5);
        vec.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        vector vec = vector();
        CK(*pass, vec.push_back(1) == 0 && vec.push_back(2) == 0);
        CK(*pass, vec.push_back(3) == 0 && vec.push_back(4) == 0);
        {
            size_t n0 = vec.size();
            size_t c0 = vec.capacity();
            stdlib_alloc_fail_at(1);
            CK(*pass, vec.push_back(2) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, vec.size() == n0 && vec.capacity() == c0 && vec.at(0) == 1);
        }
        vec.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_at(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(11); vec.push_back(22);
        CK(*pass, vec.at(0) == 11 && vec.at(1) == 22);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        CK(*pass, vec.at(0) == 0);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        vec.push_back(3); vec.push_back(4);
        CK(*pass, vec.at(vec.size() - 1u) == 4);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(3);
        CK(*pass, vec.at(vec.size()) == 0);
        vec.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        vector vec = vector();
        vec.push_back(3);
        CK(*pass, vec.at(SIZE_MAX) == 0);
        vec.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        vector vec = vector();
        vec.push_back(3);
        CK(*pass, vec.at(2u) == 0 && vec.at(99u) == 0);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(3);
        vec.drop(); vec.drop();
        CK(*pass, vec.at(0) == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_at_checked(int cat, int *pass) {
    int out = -77;
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(11); vec.push_back(22);
        CK(*pass, vec.at_checked(0u, &out) == 0 && out == 11);
        CK(*pass, vec.at_checked(1u, &out) == 0 && out == 22);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        CK(*pass, vec.at_checked(0u, &out) == -1);
        CK(*pass, vec.at_checked(0u, NULL) == -1);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        vec.push_back(8);
        CK(*pass, vec.at_checked(0u, &out) == 0 && out == 8);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(8);
        CK(*pass, vec.at_checked(1u, &out) == -1);
        vec.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        vector vec = vector();
        vec.push_back(8);
        CK(*pass, vec.at_checked(SIZE_MAX, &out) == -1);
        vec.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        vector vec = vector();
        vec.push_back(8);
        CK(*pass, vec.at_checked(50u, &out) == -1);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(8);
        vec.drop(); vec.drop();
        CK(*pass, vec.at_checked(0u, &out) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_pop_back(int cat, int *pass) {
    int out = -1;
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        CK(*pass, vec.pop_back(&out) == 0 && out == 2 && vec.size() == 1u);
        CK(*pass, vec.pop_back(&out) == 0 && out == 1 && vec.size() == 0u);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        CK(*pass, vec.pop_back(&out) == -1);
        CK(*pass, vec.pop_back(NULL) == -1);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        vec.push_back(42);
        CK(*pass, vec.pop_back(&out) == 0 && out == 42 && vec.size() == 0u);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(1);
        CK(*pass, vec.pop_back(&out) == 0);
        CK(*pass, vec.pop_back(&out) == -1 && vec.size() == 0u);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        CK(*pass, vec.pop_back(&out) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_insert(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(3);
        CK(*pass, vec.insert(1u, 2) == 0 && vec.size() == 3u);
        CK(*pass, vec.at(0) == 1 && vec.at(1) == 2 && vec.at(2) == 3);
        CK(*pass, vec.insert(0u, 0) == 0 && vec.at(0) == 0);
        CK(*pass, vec.insert(vec.size(), 4) == 0 && vec.at(vec.size() - 1u) == 4);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        CK(*pass, vec.insert(0u, 7) == 0 && vec.size() == 1u && vec.at(0) == 7);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        CK(*pass, vec.reserve(4u) == 0);
        vec.push_back(1); vec.push_back(2); vec.push_back(3);
        CK(*pass, vec.insert(1u, 9) == 0 && vec.capacity() == 4u && vec.size() == 4u);
        CK(*pass, vec.at(1) == 9 && vec.at(3) == 3);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(1);
        CK(*pass, vec.insert(2u, 9) == -1 && vec.size() == 1u && vec.at(0) == 1);
        vec.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        vector vec = vector();
        vec.push_back(1);
        CK(*pass, vec.insert(SIZE_MAX, 9) == -1 && vec.size() == 1u);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        CK(*pass, vec.insert(0u, 5) == 0 && vec.size() == 1u && vec.at(0) == 5);
        vec.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2); vec.push_back(3); vec.push_back(4);
        {
            size_t n0 = vec.size();
            size_t c0 = vec.capacity();
            stdlib_alloc_fail_at(1);
            CK(*pass, vec.insert(1u, 99) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, vec.size() == n0 && vec.capacity() == c0);
            CK(*pass, vec.at(0) == 1 && vec.at(1) == 2 && vec.at(2) == 3 && vec.at(3) == 4);
        }
        vec.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_erase(int cat, int *pass) {
    int out = -1;
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2); vec.push_back(3);
        CK(*pass, vec.erase(1u, &out) == 0 && out == 2 && vec.size() == 2u);
        CK(*pass, vec.at(0) == 1 && vec.at(1) == 3);
        CK(*pass, vec.erase(0u, NULL) == 0 && vec.size() == 1u && vec.at(0) == 3);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        CK(*pass, vec.erase(0u, &out) == -1);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        vec.push_back(7);
        CK(*pass, vec.erase(0u, &out) == 0 && out == 7 && vec.size() == 0u);
        vec.drop();
        break;
    }
    case CAT_ONE_OVER: {
        vector vec = vector();
        vec.push_back(7);
        CK(*pass, vec.erase(1u, &out) == -1 && vec.size() == 1u);
        vec.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        vector vec = vector();
        vec.push_back(7);
        CK(*pass, vec.erase(SIZE_MAX, &out) == -1 && vec.size() == 1u);
        vec.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        vector vec = vector();
        vec.push_back(7);
        CK(*pass, vec.erase(20u, &out) == -1 && vec.at(0) == 7);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(7);
        vec.drop(); vec.drop();
        CK(*pass, vec.erase(0u, &out) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_clear(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        slice sl = vec.view();
        size_t c = vec.capacity();
        vec.clear();
        CK(*pass, vec.size() == 0u && vec.capacity() == c);
        CK(*pass, sl.stale() == 1 && sl.valid() == 0);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        vec.clear();
        CK(*pass, vec.size() == 0u);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        vec.clear();
        CK(*pass, vec.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_copy_from(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vecA = vector();
        vector vecVB = vector();
        vecVB.push_back(1); vecVB.push_back(2); vecVB.push_back(3);
        CK(*pass, vecA.copy_from(&vecVB) == 0 && vecA.size() == 3u);
        CK(*pass, vecA.at(0) == 1 && vecA.at(2) == 3);
        vecA.push_back(4);
        CK(*pass, vecVB.size() == 3u);
        vecA.drop(); vecVB.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vecA = vector();
        vector vecE = vector();
        vecA.push_back(9);
        CK(*pass, vecA.copy_from(NULL) == -1 && vecA.size() == 1u && vecA.at(0) == 9);
        CK(*pass, vecA.copy_from(&vecE) == 0 && vecA.size() == 0u);
        vecA.drop(); vecE.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vecSrc = vector();
        vector vecDst = vector();
        size_t i;
        CK(*pass, vecSrc.reserve((size_t)LIMIT / sizeof(int)) == 0);
        for (i = 0; i < (size_t)LIMIT / sizeof(int); ++i)
            vecSrc.push_back((int)i);
        CK(*pass, vecDst.copy_from(&vecSrc) == 0 && vecDst.size() == (size_t)LIMIT / sizeof(int));
        CK(*pass, vecDst.at(0) == 0 && vecDst.at(vecDst.size() - 1u) == (int)((size_t)LIMIT / sizeof(int)) - 1);
        vecDst.drop(); vecSrc.drop();
        break;
    }
    case CAT_SELF_ALIAS: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        CK(*pass, vec.copy_from(&vec) == 0 && vec.size() == 2u && vec.at(0) == 1 && vec.at(1) == 2);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vecSrc = vector();
        vector vecDst = vector();
        vecSrc.push_back(7);
        vecSrc.drop(); vecSrc.drop();
        vecDst.push_back(9);
        CK(*pass, vecDst.copy_from(&vecSrc) == 0 && vecDst.size() == 0u);
        vecDst.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        vector vecDst = vector();
        vector vecSrc = vector();
        vecDst.push_back(1); vecDst.push_back(2);
        vecSrc.push_back(5); vecSrc.push_back(6); vecSrc.push_back(7);
        {
            size_t n0 = vecDst.size();
            size_t c0 = vecDst.capacity();
            stdlib_alloc_fail_at(1);
            CK(*pass, vecDst.copy_from(&vecSrc) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, vecDst.size() == n0 && vecDst.capacity() == c0);
            CK(*pass, vecDst.at(0) == 1 && vecDst.at(1) == 2);
        }
        vecDst.drop(); vecSrc.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_shrink_to_fit(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        vec.reserve(64u);
        CK(*pass, vec.shrink_to_fit() == 0 && vec.capacity() == 2u);
        CK(*pass, vec.size() == 2u && vec.at(0) == 1 && vec.at(1) == 2);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        vec.reserve(32u);
        CK(*pass, vec.shrink_to_fit() == 0 && vec.capacity() == 0u);
        CK(*pass, vec.size() == 0u && vec.push_back(3) == 0 && vec.at(0) == 3);
        vec.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        vector vec = vector();
        vec.reserve(4u);
        vec.push_back(1); vec.push_back(2); vec.push_back(3); vec.push_back(4);
        CK(*pass, vec.capacity() == 4u && vec.shrink_to_fit() == 0 && vec.capacity() == 4u);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        CK(*pass, vec.shrink_to_fit() == 0 && vec.capacity() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        vec.reserve(64u);
        {
            size_t c0 = vec.capacity();
            int *p0 = NULL;
            slice sl = vec.view();
            (void)p0;
            stdlib_alloc_fail_at(1);
            CK(*pass, vec.shrink_to_fit() == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, vec.capacity() == c0 && vec.size() == 2u);
            CK(*pass, vec.at(0) == 1 && vec.at(1) == 2 && sl.stale() == 0);
        }
        vec.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_view(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        slice sl = vec.view();
        int out = 0;
        CK(*pass, sl.size() == 2u && sl.valid() == 1 && sl.stale() == 0);
        CK(*pass, sl.at_checked(1u, &out) == 0 && out == 2);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        slice sl = vec.view();
        CK(*pass, sl.size() == 0u && sl.valid() == 0);
        vec.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop(); vec.drop();
        {
            slice sl = vec.view();
            CK(*pass, sl.size() == 0u && sl.valid() == 0);
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_VEC_vector_drop(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        vector vec = vector();
        vec.push_back(1); vec.push_back(2);
        vec.drop();
        CK(*pass, vec.size() == 0u && vec.capacity() == 0u);
        CK(*pass, vec.push_back(9) == 0 && vec.at(0) == 9);
        vec.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        vector vec = vector();
        vec.drop(); vec.drop();
        CK(*pass, vec.size() == 0u && vec.capacity() == 0u);
        break;
    }
    case CAT_DOUBLE_DROP: {
        vector vec = vector();
        vec.push_back(1);
        vec.drop();
        CK(*pass, vec.size() == 0u);
        vec.drop();
        CK(*pass, vec.capacity() == 0u && vec.at(0) == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ======================================================================== */
/* collections.hp -- free functions                                          */
/* ======================================================================== */

static void api_COL_map_hash_int(int cat, int *pass) {
    unsigned long long h0, h1;
    switch (cat) {
    case CAT_VALID:
        h0 = stdlib_map_hash_int(0LL);
        h1 = stdlib_map_hash_int(1LL);
        CK(*pass, h0 == 0ULL && h0 != h1);
        CK(*pass, stdlib_map_hash_int(42LL) == stdlib_map_hash_int(42LL));
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, stdlib_map_hash_int(0LL) == 0ULL);
        break;
    case CAT_SIZE_MAX:
        CK(*pass, stdlib_map_hash_int((long long)SIZE_MAX) != 0ULL);
        CK(*pass, stdlib_map_hash_int((-1LL)) == stdlib_map_hash_int(-1LL));
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_map_hash_text(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID:
        CK(*pass, stdlib_map_hash_text("a") != stdlib_map_hash_text("b"));
        CK(*pass, stdlib_map_hash_text("abc") == stdlib_map_hash_text("abc"));
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, stdlib_map_hash_text(NULL) == 1469598103934665603ULL);
        CK(*pass, stdlib_map_hash_text("") == 1469598103934665603ULL);
        break;
    case CAT_EXACT_FIT:
        CK(*pass, stdlib_map_hash_text("exact") == stdlib_map_hash_text("exact"));
        break;
    case CAT_ONE_OVER:
        CK(*pass, stdlib_map_hash_text("abc") != stdlib_map_hash_text("abcd"));
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_map_next_pow2(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID:
        CK(*pass, stdlib_map_next_pow2(8u) == 8u);
        CK(*pass, stdlib_map_next_pow2(9u) == 16u);
        CK(*pass, stdlib_map_next_pow2(100u) == 128u);
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, stdlib_map_next_pow2(0u) == 8u);
        CK(*pass, stdlib_map_next_pow2(1u) == 8u);
        break;
    case CAT_EXACT_FIT:
        CK(*pass, stdlib_map_next_pow2(16u) == 16u);
        break;
    case CAT_ONE_OVER:
        CK(*pass, stdlib_map_next_pow2(17u) == 32u);
        break;
    case CAT_SIZE_MAX:
        CK(*pass, stdlib_map_next_pow2(SIZE_MAX) == 0u);
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_map_needs_room(int cat, int *pass) {
    int r;
    switch (cat) {
    case CAT_VALID:
        CK(*pass, stdlib_map_needs_room(0u, 8u) == 0);
        CK(*pass, stdlib_map_needs_room(5u, 8u) == 1);
        CK(*pass, stdlib_map_needs_room(4u, 8u) == 0);
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, stdlib_map_needs_room(0u, 0u) == 1);
        break;
    case CAT_EXACT_FIT:
        /* (4+1)*10 == 50 < 56: still room at capacity 8. */
        CK(*pass, stdlib_map_needs_room(4u, 8u) == 0);
        break;
    case CAT_ONE_OVER:
        CK(*pass, stdlib_map_needs_room(5u, 8u) == 1);
        CK(*pass, stdlib_map_needs_room(6u, 16u) == 0);
        break;
    case CAT_SIZE_MAX:
        CK(*pass, stdlib_map_needs_room(SIZE_MAX, 0u) == 1);
        r = stdlib_map_needs_room(SIZE_MAX, 8u);
        CK(*pass, r == 0 || r == 1);
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_map_rebuild_capacity(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID:
        CK(*pass, stdlib_map_rebuild_capacity(2u, 8u) == 8u);
        CK(*pass, stdlib_map_rebuild_capacity(5u, 8u) == 16u);
        break;
    case CAT_NULL_EMPTY:
        CK(*pass, stdlib_map_rebuild_capacity(0u, 0u) == 8u);
        break;
    case CAT_EXACT_FIT:
        CK(*pass, stdlib_map_rebuild_capacity(2u, 8u) == 8u);
        break;
    case CAT_ONE_OVER:
        CK(*pass, stdlib_map_rebuild_capacity(3u, 8u) == 16u);
        break;
    case CAT_SIZE_MAX:
        CK(*pass, stdlib_map_rebuild_capacity(SIZE_MAX, 8u) == 8u);
        break;
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_ring_copy(int cat, int *pass) {
    int data[8];
    int i;
    for (i = 0; i < 8; ++i)
        data[i] = i + 1;
    switch (cat) {
    case CAT_VALID: {
        int *fresh = stdlib_ring_copy(data, 8u, 3u, 4u, 8u);
        CK(*pass, fresh != NULL);
        if (fresh != NULL) {
            CK(*pass, fresh[0] == 4 && fresh[1] == 5 && fresh[2] == 6 && fresh[3] == 7);
            stdlib_release(fresh);
        }
        break;
    }
    case CAT_NULL_EMPTY: {
        int *fresh = stdlib_ring_copy(NULL, 0u, 0u, 0u, 8u);
        CK(*pass, fresh != NULL);
        if (fresh != NULL)
            stdlib_release(fresh);
        break;
    }
    case CAT_EXACT_FIT: {
        int *fresh = stdlib_ring_copy(data, 8u, 0u, 8u, (size_t)LIMIT / sizeof(int));
        CK(*pass, fresh != NULL && fresh[0] == 1 && fresh[7] == 8);
        if (fresh != NULL)
            stdlib_release(fresh);
        break;
    }
    case CAT_ONE_OVER: {
        int *fresh = stdlib_ring_copy(data, 8u, 0u, 8u, (size_t)LIMIT / sizeof(int) + 1u);
        CK(*pass, fresh == NULL);
        break;
    }
    case CAT_SIZE_MAX: {
        int *fresh = stdlib_ring_copy(data, 8u, 0u, 8u, SIZE_MAX);
        CK(*pass, fresh == NULL);
        break;
    }
    case CAT_ALLOC_FAIL: {
        int *fresh;
        stdlib_alloc_fail_at(1);
        fresh = stdlib_ring_copy(data, 8u, 0u, 8u, 8u);
        stdlib_alloc_fail_reset();
        CK(*pass, fresh == NULL);
        CK(*pass, data[0] == 1 && data[7] == 8);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- intmap ------------------------------------------------------------- */

static void api_COL_intmap_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        CK(*pass, im.size() == 0u && im.valid() == 0);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.drop(); im.drop();
        CK(*pass, im.size() == 0u && im.valid() == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        CK(*pass, im.size() == 0u);
        im.put(1, 10); im.put(2, 20);
        CK(*pass, im.size() == 2u);
        im.remove(1);
        CK(*pass, im.size() == 1u);
        im.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intmap im = intmap();
        CK(*pass, im.size() == 0u);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.put(1, 1);
        im.drop(); im.drop();
        CK(*pass, im.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        intmap im = intmap();
        stdlib_alloc_fail_at(1);
        CK(*pass, im.put(1, 1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, im.size() == 0u);
        im.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_valid(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        CK(*pass, im.valid() == 0);
        CK(*pass, im.put(1, 1) == 0 && im.valid() == 1);
        im.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intmap im = intmap();
        CK(*pass, im.valid() == 0);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.put(1, 1);
        im.drop();
        CK(*pass, im.valid() == 0);
        im.drop();
        CK(*pass, im.valid() == 0);
        im.put(2, 2);
        CK(*pass, im.valid() == 1);
        im.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_grow(int cat, int *pass) {
    int i;
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        CK(*pass, im.grow(8u) == 0 && im.valid() == 1);
        CK(*pass, im.put(7, 70) == 0 && im.get(7, &v) == 0 && v == 70);
        im.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intmap im = intmap();
        CK(*pass, im.grow(0u) == 0 && im.valid() == 1 && im.size() == 0u);
        im.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        intmap im = intmap();
        CK(*pass, im.grow(8u) == 0);
        CK(*pass, im.grow(4u) == 0 && im.grow(8u) == 0);
        im.drop();
        break;
    }
    case CAT_ONE_OVER: {
        intmap im = intmap();
        CK(*pass, im.grow(9u) == 0);
        for (i = 0; i < 9; ++i)
            CK(*pass, im.put(i, i + 100) == 0);
        CK(*pass, im.size() == 9u);
        for (i = 0; i < 9; ++i)
            CK(*pass, im.get(i, &v) == 0 && v == i + 100);
        im.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intmap im = intmap();
        CK(*pass, im.grow(SIZE_MAX) == -1 && im.valid() == 0);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.grow(8u);
        im.drop(); im.drop();
        CK(*pass, im.grow(8u) == 0 && im.valid() == 1);
        im.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        intmap im = intmap();
        int fp;
        for (fp = 1; fp <= 3; ++fp) {
            intmap imFresh = intmap();
            int j;
            for (j = 0; j < 5; ++j)
                imFresh.put(j, j + 1);
            stdlib_alloc_fail_at(fp);
            CK(*pass, imFresh.grow(64u) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, imFresh.size() == 5u && imFresh.valid() == 1);
            CK(*pass, imFresh.get(0, &v) == 0 && v == 1);
            CK(*pass, imFresh.get(4, &v) == 0 && v == 5);
            imFresh.drop();
        }
        im.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_put(int cat, int *pass) {
    int i;
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        CK(*pass, im.put(1, 10) == 0 && im.put(2, 20) == 0);
        CK(*pass, im.put(1, 99) == 0 && im.size() == 2u);
        CK(*pass, im.get(1, &v) == 0 && v == 99);
        CK(*pass, im.get(2, &v) == 0 && v == 20);
        im.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        intmap im = intmap();
        for (i = 0; i < 5; ++i)
            CK(*pass, im.put(i, i) == 0);
        CK(*pass, im.size() == 5u && im.valid() == 1);
        for (i = 0; i < 5; ++i)
            CK(*pass, im.get(i, &v) == 0 && v == i);
        im.drop();
        break;
    }
    case CAT_ONE_OVER: {
        intmap im = intmap();
        for (i = 0; i < 6; ++i)
            CK(*pass, im.put(i, i + 1) == 0);
        CK(*pass, im.size() == 6u);
        for (i = 0; i < 6; ++i)
            CK(*pass, im.get(i, &v) == 0 && v == i + 1);
        im.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intmap im = intmap();
        CK(*pass, im.put((int)SIZE_MAX, 7) == 0);
        CK(*pass, im.get((int)SIZE_MAX, &v) == 0 && v == 7);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.put(1, 1);
        im.drop(); im.drop();
        CK(*pass, im.put(5, 5) == 0 && im.size() == 1u && im.valid() == 1);
        CK(*pass, im.get(5, &v) == 0 && v == 5);
        im.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        int fp;
        for (fp = 1; fp <= 3; ++fp) {
            intmap imFresh = intmap();
            int j;
            for (j = 0; j < 5; ++j)
                imFresh.put(j, j + 1);
            stdlib_alloc_fail_at(fp);
            CK(*pass, imFresh.put(99, 99) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, imFresh.size() == 5u);
            CK(*pass, imFresh.get(0, &v) == 0 && v == 1);
            CK(*pass, imFresh.get(99, &v) == -1);
            imFresh.drop();
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_get(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        im.put(3, 33);
        CK(*pass, im.get(3, &v) == 0 && v == 33);
        im.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intmap im = intmap();
        CK(*pass, im.get(0, &v) == -1);
        CK(*pass, im.get(0, NULL) == -1);
        im.put(1, 1);
        CK(*pass, im.get(2, &v) == -1);
        im.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intmap im = intmap();
        CK(*pass, im.get((int)SIZE_MAX, &v) == -1);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.put(1, 1);
        im.drop(); im.drop();
        CK(*pass, im.get(1, &v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_contains(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        im.put(5, 1);
        CK(*pass, im.contains(5) == 1 && im.contains(6) == 0);
        im.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intmap im = intmap();
        CK(*pass, im.contains(0) == 0);
        im.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intmap im = intmap();
        CK(*pass, im.contains((int)SIZE_MAX) == 0);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.put(1, 1);
        im.drop(); im.drop();
        CK(*pass, im.contains(1) == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_remove(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        im.put(1, 10); im.put(2, 20);
        CK(*pass, im.remove(1) == 0 && im.size() == 1u);
        CK(*pass, im.contains(1) == 0 && im.get(2, &v) == 0 && v == 20);
        CK(*pass, im.put(3, 30) == 0 && im.get(3, &v) == 0 && v == 30);
        im.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intmap im = intmap();
        CK(*pass, im.remove(0) == -1);
        im.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intmap im = intmap();
        CK(*pass, im.remove((int)SIZE_MAX) == -1);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.put(1, 1);
        CK(*pass, im.remove(1) == 0);
        CK(*pass, im.remove(1) == -1);
        im.drop();
        CK(*pass, im.remove(1) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_clear(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        im.put(1, 1); im.put(2, 2);
        im.clear();
        CK(*pass, im.size() == 0u && im.valid() == 1 && im.contains(1) == 0);
        CK(*pass, im.put(3, 3) == 0 && im.size() == 1u);
        im.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intmap im = intmap();
        im.clear();
        CK(*pass, im.size() == 0u && im.valid() == 0);
        im.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.put(1, 1);
        im.drop(); im.drop();
        im.clear();
        CK(*pass, im.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intmap_drop(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intmap im = intmap();
        im.put(1, 1);
        im.drop();
        CK(*pass, im.size() == 0u && im.valid() == 0);
        im.drop();
        CK(*pass, im.size() == 0u);
        break;
    }
    case CAT_NULL_EMPTY: {
        intmap im = intmap();
        im.drop(); im.drop();
        CK(*pass, im.size() == 0u && im.valid() == 0);
        break;
    }
    case CAT_DOUBLE_DROP: {
        intmap im = intmap();
        im.put(1, 1); im.put(2, 2);
        im.drop();
        im.drop();
        CK(*pass, im.size() == 0u && im.valid() == 0);
        CK(*pass, im.put(3, 3) == 0 && im.size() == 1u);
        im.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- strmap ------------------------------------------------------------- */

static void api_COL_strmap_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        CK(*pass, sm.size() == 0u && sm.valid() == 0);
        sm.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.drop(); sm.drop();
        CK(*pass, sm.size() == 0u && sm.valid() == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        CK(*pass, sm.size() == 0u);
        sm.put("a", 1); sm.put("b", 2);
        CK(*pass, sm.size() == 2u);
        sm.remove("a");
        CK(*pass, sm.size() == 1u);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        CK(*pass, sm.size() == 0u);
        sm.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.put("a", 1);
        sm.drop(); sm.drop();
        CK(*pass, sm.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        strmap sm = strmap();
        stdlib_alloc_fail_at(1);
        CK(*pass, sm.put("a", 1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, sm.size() == 0u);
        sm.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_valid(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        CK(*pass, sm.valid() == 0);
        CK(*pass, sm.put("a", 1) == 0 && sm.valid() == 1);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        CK(*pass, sm.valid() == 0);
        sm.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.put("a", 1);
        sm.drop();
        CK(*pass, sm.valid() == 0);
        sm.drop();
        CK(*pass, sm.valid() == 0);
        sm.put("b", 2);
        CK(*pass, sm.valid() == 1);
        sm.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_grow(int cat, int *pass) {
    int i;
    int v = 0;
    static const char *keys[9] = {"a", "b", "c", "d", "e", "f", "g", "h", "i"};
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        CK(*pass, sm.grow(8u) == 0 && sm.valid() == 1);
        CK(*pass, sm.put("k", 7) == 0 && sm.get("k", &v) == 0 && v == 7);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        CK(*pass, sm.grow(0u) == 0 && sm.valid() == 1 && sm.size() == 0u);
        sm.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        strmap sm = strmap();
        CK(*pass, sm.grow(8u) == 0);
        CK(*pass, sm.grow(4u) == 0 && sm.grow(8u) == 0);
        sm.drop();
        break;
    }
    case CAT_ONE_OVER: {
        strmap sm = strmap();
        CK(*pass, sm.grow(9u) == 0);
        for (i = 0; i < 9; ++i)
            CK(*pass, sm.put(keys[i], i + 1) == 0);
        CK(*pass, sm.size() == 9u);
        for (i = 0; i < 9; ++i)
            CK(*pass, sm.get(keys[i], &v) == 0 && v == i + 1);
        sm.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        strmap sm = strmap();
        CK(*pass, sm.grow(SIZE_MAX) == -1 && sm.valid() == 0);
        sm.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.grow(8u);
        sm.drop(); sm.drop();
        CK(*pass, sm.grow(8u) == 0 && sm.valid() == 1);
        sm.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        int fp;
        for (fp = 1; fp <= 3; ++fp) {
            strmap smFresh = strmap();
            int j;
            for (j = 0; j < 5; ++j)
                smFresh.put(keys[j], j + 1);
            stdlib_alloc_fail_at(fp);
            CK(*pass, smFresh.grow(64u) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, smFresh.size() == 5u && smFresh.valid() == 1);
            CK(*pass, smFresh.get("a", &v) == 0 && v == 1);
            CK(*pass, smFresh.get("e", &v) == 0 && v == 5);
            smFresh.drop();
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_put(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        CK(*pass, sm.put("alpha", 1) == 0 && sm.put("beta", 2) == 0);
        CK(*pass, sm.put("alpha", 9) == 0 && sm.size() == 2u);
        CK(*pass, sm.get("alpha", &v) == 0 && v == 9);
        CK(*pass, sm.get("beta", &v) == 0 && v == 2);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        CK(*pass, sm.put(NULL, 1) == -1 && sm.size() == 0u);
        CK(*pass, sm.put("", 0) == 0 && sm.get("", &v) == 0 && v == 0);
        sm.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        char key[4096];
        size_t i;
        for (i = 0; i < (size_t)LIMIT - 1u; ++i)
            key[i] = 'k';
        key[(size_t)LIMIT - 1u] = '\0';
        {
            strmap sm = strmap();
            CK(*pass, sm.put(key, 17) == 0 && sm.size() == 1u);
            CK(*pass, sm.get(key, &v) == 0 && v == 17);
            sm.drop();
        }
        break;
    }
    case CAT_ONE_OVER: {
        char key[4098];
        size_t i;
        for (i = 0; i < (size_t)LIMIT; ++i)
            key[i] = 'k';
        key[(size_t)LIMIT] = '\0';
        {
            strmap sm = strmap();
            CK(*pass, sm.put("base", 4) == 0);
            CK(*pass, sm.put(key, 18) == -1);
            CK(*pass, sm.size() == 1u && sm.get("base", &v) == 0 && v == 4);
            CK(*pass, sm.get(key, &v) == -1);
            sm.drop();
        }
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.put("a", 1);
        sm.drop(); sm.drop();
        CK(*pass, sm.put("b", 2) == 0 && sm.size() == 1u && sm.valid() == 1);
        CK(*pass, sm.get("b", &v) == 0 && v == 2);
        sm.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        int fp;
        for (fp = 4; fp >= 1; --fp) {
            strmap smFresh = strmap();
            stdlib_alloc_fail_at(fp);
            CK(*pass, smFresh.put("newkey", 5) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, smFresh.size() == 0u);
            CK(*pass, smFresh.get("newkey", &v) == -1);
            smFresh.drop();
        }
        /* rebuild paths with existing entries */
        for (fp = 1; fp <= 3; ++fp) {
            strmap smFresh = strmap();
            int j;
            for (j = 0; j < 5; ++j)
                smFresh.put("a", j + 1); /* same key: no growth, just overwrite */
            CK(*pass, smFresh.size() == 1u);
            smFresh.drop();
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_get(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        sm.put("k", 42);
        CK(*pass, sm.get("k", &v) == 0 && v == 42);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        CK(*pass, sm.get("k", &v) == -1);
        CK(*pass, sm.get(NULL, &v) == -1);
        CK(*pass, sm.get("k", NULL) == -1);
        sm.put("a", 1);
        CK(*pass, sm.get("b", &v) == -1);
        sm.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.put("k", 1);
        sm.drop(); sm.drop();
        CK(*pass, sm.get("k", &v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_contains(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        sm.put("yes", 1);
        CK(*pass, sm.contains("yes") == 1 && sm.contains("no") == 0);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        CK(*pass, sm.contains(NULL) == 0 && sm.contains("") == 0);
        sm.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.put("k", 1);
        sm.drop(); sm.drop();
        CK(*pass, sm.contains("k") == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_remove(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        sm.put("a", 1); sm.put("b", 2);
        CK(*pass, sm.remove("a") == 0 && sm.size() == 1u);
        CK(*pass, sm.contains("a") == 0 && sm.get("b", &v) == 0 && v == 2);
        CK(*pass, sm.put("c", 3) == 0 && sm.get("c", &v) == 0 && v == 3);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        CK(*pass, sm.remove("none") == -1);
        CK(*pass, sm.remove(NULL) == -1);
        sm.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.put("a", 1);
        CK(*pass, sm.remove("a") == 0);
        CK(*pass, sm.remove("a") == -1);
        sm.drop();
        CK(*pass, sm.remove("a") == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_clear(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        sm.put("a", 1); sm.put("b", 2);
        sm.clear();
        CK(*pass, sm.size() == 0u && sm.valid() == 1);
        CK(*pass, sm.get("a", &v) == -1 && sm.get("b", &v) == -1);
        CK(*pass, sm.put("c", 3) == 0 && sm.get("c", &v) == 0 && v == 3);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        sm.clear();
        CK(*pass, sm.size() == 0u && sm.valid() == 0);
        sm.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.put("a", 1);
        sm.drop(); sm.drop();
        sm.clear();
        CK(*pass, sm.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_strmap_drop(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        strmap sm = strmap();
        sm.put("a", 1);
        sm.drop();
        CK(*pass, sm.size() == 0u && sm.valid() == 0 && sm.get("a", &v) == -1);
        sm.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        strmap sm = strmap();
        sm.drop(); sm.drop();
        CK(*pass, sm.size() == 0u && sm.valid() == 0);
        break;
    }
    case CAT_DOUBLE_DROP: {
        strmap sm = strmap();
        sm.put("a", 1); sm.put("b", 2);
        sm.drop(); sm.drop();
        CK(*pass, sm.size() == 0u && sm.valid() == 0);
        CK(*pass, sm.put("c", 3) == 0 && sm.get("c", &v) == 0 && v == 3);
        sm.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- intset ------------------------------------------------------------- */

static void api_COL_intset_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        CK(*pass, isv.size() == 0u && isv.valid() == 0);
        isv.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.drop(); isv.drop();
        CK(*pass, isv.size() == 0u && isv.valid() == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intset_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        CK(*pass, isv.size() == 0u);
        isv.insert(1); isv.insert(2);
        CK(*pass, isv.size() == 2u);
        isv.remove(1);
        CK(*pass, isv.size() == 1u);
        isv.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intset isv = intset();
        CK(*pass, isv.size() == 0u);
        isv.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.insert(1);
        isv.drop(); isv.drop();
        CK(*pass, isv.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        intset isv = intset();
        stdlib_alloc_fail_at(1);
        CK(*pass, isv.insert(1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, isv.size() == 0u);
        isv.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intset_valid(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        CK(*pass, isv.valid() == 0);
        CK(*pass, isv.insert(1) == 0 && isv.valid() == 1);
        isv.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intset isv = intset();
        CK(*pass, isv.valid() == 0);
        isv.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.insert(1);
        isv.drop();
        CK(*pass, isv.valid() == 0);
        isv.drop();
        CK(*pass, isv.valid() == 0);
        isv.insert(2);
        CK(*pass, isv.valid() == 1);
        isv.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intset_grow(int cat, int *pass) {
    int i;
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        CK(*pass, isv.grow(8u) == 0 && isv.valid() == 1);
        CK(*pass, isv.insert(7) == 0 && isv.contains(7) == 1);
        isv.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intset isv = intset();
        CK(*pass, isv.grow(0u) == 0 && isv.valid() == 1 && isv.size() == 0u);
        isv.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        intset isv = intset();
        CK(*pass, isv.grow(8u) == 0 && isv.grow(4u) == 0 && isv.grow(8u) == 0);
        isv.drop();
        break;
    }
    case CAT_ONE_OVER: {
        intset isv = intset();
        CK(*pass, isv.grow(9u) == 0);
        for (i = 0; i < 9; ++i)
            CK(*pass, isv.insert(i) == 0);
        CK(*pass, isv.size() == 9u);
        for (i = 0; i < 9; ++i)
            CK(*pass, isv.contains(i) == 1);
        isv.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intset isv = intset();
        CK(*pass, isv.grow(SIZE_MAX) == -1 && isv.valid() == 0);
        isv.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.grow(8u);
        isv.drop(); isv.drop();
        CK(*pass, isv.grow(8u) == 0 && isv.valid() == 1);
        isv.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        int fp;
        for (fp = 1; fp <= 2; ++fp) {
            intset isFresh = intset();
            int j;
            for (j = 0; j < 5; ++j)
                isFresh.insert(j);
            stdlib_alloc_fail_at(fp);
            CK(*pass, isFresh.grow(64u) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, isFresh.size() == 5u && isFresh.valid() == 1);
            CK(*pass, isFresh.contains(0) == 1 && isFresh.contains(4) == 1);
            isFresh.drop();
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intset_insert(int cat, int *pass) {
    int i;
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        CK(*pass, isv.insert(1) == 0 && isv.insert(2) == 0);
        CK(*pass, isv.insert(1) == 0 && isv.size() == 2u);
        CK(*pass, isv.contains(1) == 1 && isv.contains(2) == 1);
        isv.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        intset isv = intset();
        for (i = 0; i < 5; ++i)
            CK(*pass, isv.insert(i) == 0);
        CK(*pass, isv.size() == 5u && isv.valid() == 1);
        isv.drop();
        break;
    }
    case CAT_ONE_OVER: {
        intset isv = intset();
        for (i = 0; i < 6; ++i)
            CK(*pass, isv.insert(i) == 0);
        CK(*pass, isv.size() == 6u);
        for (i = 0; i < 6; ++i)
            CK(*pass, isv.contains(i) == 1);
        isv.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intset isv = intset();
        CK(*pass, isv.insert((int)SIZE_MAX) == 0);
        CK(*pass, isv.contains((int)SIZE_MAX) == 1);
        isv.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.insert(1);
        isv.drop(); isv.drop();
        CK(*pass, isv.insert(2) == 0 && isv.size() == 1u && isv.valid() == 1);
        isv.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        int fp;
        for (fp = 1; fp <= 2; ++fp) {
            intset isFresh = intset();
            int j;
            for (j = 0; j < 5; ++j)
                isFresh.insert(j);
            stdlib_alloc_fail_at(fp);
            CK(*pass, isFresh.insert(99) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, isFresh.size() == 5u);
            CK(*pass, isFresh.contains(0) == 1 && isFresh.contains(99) == 0);
            isFresh.drop();
        }
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intset_contains(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        isv.insert(5);
        CK(*pass, isv.contains(5) == 1 && isv.contains(6) == 0);
        isv.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intset isv = intset();
        CK(*pass, isv.contains(0) == 0);
        isv.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intset isv = intset();
        CK(*pass, isv.contains((int)SIZE_MAX) == 0);
        isv.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.insert(1);
        isv.drop(); isv.drop();
        CK(*pass, isv.contains(1) == 0);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intset_remove(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        isv.insert(1); isv.insert(2);
        CK(*pass, isv.remove(1) == 0 && isv.size() == 1u);
        CK(*pass, isv.contains(1) == 0 && isv.contains(2) == 1);
        CK(*pass, isv.insert(3) == 0 && isv.contains(3) == 1);
        isv.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intset isv = intset();
        CK(*pass, isv.remove(0) == -1);
        isv.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        intset isv = intset();
        CK(*pass, isv.remove((int)SIZE_MAX) == -1);
        isv.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.insert(1);
        CK(*pass, isv.remove(1) == 0 && isv.remove(1) == -1);
        isv.drop();
        CK(*pass, isv.remove(1) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intset_clear(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        isv.insert(1); isv.insert(2);
        isv.clear();
        CK(*pass, isv.size() == 0u && isv.valid() == 1 && isv.contains(1) == 0);
        CK(*pass, isv.insert(3) == 0 && isv.size() == 1u);
        isv.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intset isv = intset();
        isv.clear();
        CK(*pass, isv.size() == 0u && isv.valid() == 0);
        isv.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.insert(1);
        isv.drop(); isv.drop();
        isv.clear();
        CK(*pass, isv.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_intset_drop(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        intset isv = intset();
        isv.insert(1);
        isv.drop();
        CK(*pass, isv.size() == 0u && isv.valid() == 0);
        isv.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        intset isv = intset();
        isv.drop(); isv.drop();
        CK(*pass, isv.size() == 0u && isv.valid() == 0);
        break;
    }
    case CAT_DOUBLE_DROP: {
        intset isv = intset();
        isv.insert(1); isv.insert(2);
        isv.drop(); isv.drop();
        CK(*pass, isv.size() == 0u && isv.valid() == 0);
        CK(*pass, isv.insert(3) == 0 && isv.size() == 1u);
        isv.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- deque -------------------------------------------------------------- */

static void api_COL_deque_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        CK(*pass, dq.size() == 0u && dq.capacity() == 0u);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.drop(); dq.drop();
        CK(*pass, dq.size() == 0u && dq.capacity() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        CK(*pass, dq.size() == 0u);
        dq.push_back(1); dq.push_front(0);
        CK(*pass, dq.size() == 2u);
        {
            int popped = -1;
            CK(*pass, dq.pop_front(&popped) == 0 && popped == 0);
        }
        CK(*pass, dq.size() == 1u);
        dq.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        deque dq = deque();
        CK(*pass, dq.size() == 0u && dq.pop_front(NULL) == -1 && dq.pop_back(NULL) == -1);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(1);
        dq.drop(); dq.drop();
        CK(*pass, dq.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        deque dq = deque();
        stdlib_alloc_fail_at(1);
        CK(*pass, dq.push_back(1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, dq.size() == 0u);
        dq.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_capacity(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        CK(*pass, dq.capacity() == 0u);
        CK(*pass, dq.push_back(1) == 0 && dq.capacity() == 8u);
        CK(*pass, dq.reserve(32u) == 0 && dq.capacity() == 32u);
        dq.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        deque dq = deque();
        CK(*pass, dq.reserve(8u) == 0 && dq.capacity() == 8u);
        CK(*pass, dq.reserve(8u) == 0 && dq.capacity() == 8u);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(1);
        dq.drop(); dq.drop();
        CK(*pass, dq.capacity() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_reserve(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        dq.push_back(1); dq.push_back(2); dq.push_front(0);
        CK(*pass, dq.reserve(32u) == 0 && dq.capacity() == 32u);
        CK(*pass, dq.size() == 3u);
        CK(*pass, dq.at_checked(0u, &v) == 0 && v == 0);
        CK(*pass, dq.at_checked(2u, &v) == 0 && v == 2);
        dq.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        deque dq = deque();
        CK(*pass, dq.reserve(0u) == 0 && dq.capacity() == 0u);
        dq.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        deque dq = deque();
        CK(*pass, dq.reserve((size_t)LIMIT / sizeof(int)) == 0);
        CK(*pass, dq.capacity() == (size_t)LIMIT / sizeof(int));
        dq.drop();
        break;
    }
    case CAT_ONE_OVER: {
        deque dq = deque();
        dq.push_back(7);
        CK(*pass, dq.reserve((size_t)LIMIT / sizeof(int) + 1u) == -1);
        CK(*pass, dq.size() == 1u && dq.capacity() == 8u);
        CK(*pass, dq.at_checked(0u, &v) == 0 && v == 7);
        dq.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        deque dq = deque();
        dq.push_back(7);
        CK(*pass, dq.reserve(SIZE_MAX) == -1 && dq.size() == 1u);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(7);
        dq.drop(); dq.drop();
        CK(*pass, dq.reserve(16u) == 0 && dq.capacity() == 16u);
        CK(*pass, dq.push_back(3) == 0 && dq.at_checked(0u, &v) == 0 && v == 3);
        dq.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        deque dq = deque();
        dq.push_back(1); dq.push_back(2);
        {
            size_t c0 = dq.capacity();
            stdlib_alloc_fail_at(1);
            CK(*pass, dq.reserve(64u) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, dq.capacity() == c0 && dq.size() == 2u);
            CK(*pass, dq.at_checked(0u, &v) == 0 && v == 1);
            CK(*pass, dq.at_checked(1u, &v) == 0 && v == 2);
        }
        dq.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_push_back(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        CK(*pass, dq.push_back(1) == 0 && dq.push_back(2) == 0 && dq.push_front(0) == 0);
        CK(*pass, dq.size() == 3u);
        CK(*pass, dq.at_checked(0u, &v) == 0 && v == 0);
        CK(*pass, dq.at_checked(2u, &v) == 0 && v == 2);
        dq.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        deque dq = deque();
        int i;
        CK(*pass, dq.reserve(8u) == 0);
        for (i = 0; i < 8; ++i)
            CK(*pass, dq.push_back(i) == 0);
        CK(*pass, dq.size() == 8u && dq.capacity() == 8u);
        CK(*pass, dq.push_back(8) == 0 && dq.capacity() == 16u);
        dq.drop();
        break;
    }
    case CAT_ONE_OVER: {
        deque dq = deque();
        size_t i;
        CK(*pass, dq.reserve(600u) == 0);
        for (i = 0; i < 600u; ++i)
            CK(*pass, dq.push_back((int)i) == 0);
        CK(*pass, dq.push_back(999) == -1 && dq.size() == 600u);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(1);
        dq.drop(); dq.drop();
        CK(*pass, dq.push_back(5) == 0 && dq.at_checked(0u, &v) == 0 && v == 5);
        dq.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        deque dq = deque();
        CK(*pass, dq.push_back(1) == 0 && dq.push_back(2) == 0);
        CK(*pass, dq.push_back(3) == 0 && dq.push_back(4) == 0);
        CK(*pass, dq.push_back(5) == 0 && dq.push_back(6) == 0);
        CK(*pass, dq.push_back(7) == 0 && dq.push_back(8) == 0);
        {
            size_t n0 = dq.size();
            size_t c0 = dq.capacity();
            stdlib_alloc_fail_at(1);
            CK(*pass, dq.push_back(2) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, dq.size() == n0 && dq.capacity() == c0);
            CK(*pass, dq.at_checked(0u, &v) == 0 && v == 1 && dq.at_checked(7u, &v) == 0 && v == 8);
        }
        dq.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_push_front(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        CK(*pass, dq.push_back(2) == 0 && dq.push_front(1) == 0 && dq.push_front(0) == 0);
        CK(*pass, dq.size() == 3u);
        CK(*pass, dq.at_checked(0u, &v) == 0 && v == 0);
        CK(*pass, dq.at_checked(1u, &v) == 0 && v == 1);
        CK(*pass, dq.at_checked(2u, &v) == 0 && v == 2);
        dq.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        deque dq = deque();
        int i;
        CK(*pass, dq.reserve(8u) == 0);
        for (i = 0; i < 8; ++i)
            CK(*pass, dq.push_front(i) == 0);
        CK(*pass, dq.size() == 8u && dq.capacity() == 8u);
        CK(*pass, dq.push_front(8) == 0 && dq.capacity() == 16u);
        dq.drop();
        break;
    }
    case CAT_ONE_OVER: {
        deque dq = deque();
        size_t i;
        CK(*pass, dq.reserve(600u) == 0);
        for (i = 0; i < 600u; ++i)
            CK(*pass, dq.push_front((int)i) == 0);
        CK(*pass, dq.push_front(999) == -1 && dq.size() == 600u);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_front(1);
        dq.drop(); dq.drop();
        CK(*pass, dq.push_front(5) == 0 && dq.at_checked(0u, &v) == 0 && v == 5);
        dq.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        deque dq = deque();
        CK(*pass, dq.push_front(1) == 0 && dq.push_front(2) == 0);
        CK(*pass, dq.push_front(3) == 0 && dq.push_front(4) == 0);
        CK(*pass, dq.push_front(5) == 0 && dq.push_front(6) == 0);
        CK(*pass, dq.push_front(7) == 0 && dq.push_front(8) == 0);
        {
            size_t n0 = dq.size();
            stdlib_alloc_fail_at(1);
            CK(*pass, dq.push_front(2) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, dq.size() == n0 && dq.at_checked(0u, &v) == 0 && v == 8 && dq.at_checked(7u, &v) == 0 && v == 1);
        }
        dq.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_pop_front(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        dq.push_back(1); dq.push_back(2);
        CK(*pass, dq.pop_front(&v) == 0 && v == 1 && dq.size() == 1u);
        CK(*pass, dq.at_checked(0u, &v) == 0 && v == 2);
        dq.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        deque dq = deque();
        CK(*pass, dq.pop_front(&v) == -1 && dq.pop_front(NULL) == -1);
        dq.drop();
        break;
    }
    case CAT_ONE_OVER: {
        deque dq = deque();
        dq.push_back(1);
        CK(*pass, dq.pop_front(&v) == 0 && v == 1);
        CK(*pass, dq.pop_front(&v) == -1 && dq.size() == 0u);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(1);
        dq.drop(); dq.drop();
        CK(*pass, dq.pop_front(&v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_pop_back(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        dq.push_back(1); dq.push_back(2);
        CK(*pass, dq.pop_back(&v) == 0 && v == 2 && dq.size() == 1u);
        CK(*pass, dq.at_checked(0u, &v) == 0 && v == 1);
        dq.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        deque dq = deque();
        CK(*pass, dq.pop_back(&v) == -1 && dq.pop_back(NULL) == -1);
        dq.drop();
        break;
    }
    case CAT_ONE_OVER: {
        deque dq = deque();
        dq.push_back(1);
        CK(*pass, dq.pop_back(&v) == 0 && v == 1);
        CK(*pass, dq.pop_back(&v) == -1 && dq.size() == 0u);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(1);
        dq.drop(); dq.drop();
        CK(*pass, dq.pop_back(&v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_at_checked(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        dq.push_front(0); dq.push_back(1); dq.push_back(2);
        CK(*pass, dq.at_checked(0u, &v) == 0 && v == 0);
        CK(*pass, dq.at_checked(2u, &v) == 0 && v == 2);
        dq.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        deque dq = deque();
        CK(*pass, dq.at_checked(0u, &v) == -1 && dq.at_checked(0u, NULL) == -1);
        dq.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        deque dq = deque();
        dq.push_back(9);
        CK(*pass, dq.at_checked(0u, &v) == 0 && v == 9);
        dq.drop();
        break;
    }
    case CAT_ONE_OVER: {
        deque dq = deque();
        dq.push_back(9);
        CK(*pass, dq.at_checked(1u, &v) == -1);
        dq.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        deque dq = deque();
        dq.push_back(9);
        CK(*pass, dq.at_checked(SIZE_MAX, &v) == -1);
        dq.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        deque dq = deque();
        dq.push_back(9);
        CK(*pass, dq.at_checked(50u, &v) == -1);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(9);
        dq.drop(); dq.drop();
        CK(*pass, dq.at_checked(0u, &v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_clear(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        dq.push_back(1); dq.push_back(2); dq.push_front(0);
        size_t c = dq.capacity();
        dq.clear();
        CK(*pass, dq.size() == 0u && dq.capacity() == c);
        CK(*pass, dq.at_checked(0u, &v) == -1);
        CK(*pass, dq.push_back(7) == 0 && dq.at_checked(0u, &v) == 0 && v == 7);
        dq.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        deque dq = deque();
        dq.clear();
        CK(*pass, dq.size() == 0u);
        dq.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(1);
        dq.drop(); dq.drop();
        dq.clear();
        CK(*pass, dq.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_deque_drop(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        deque dq = deque();
        dq.push_back(1);
        dq.drop();
        CK(*pass, dq.size() == 0u && dq.capacity() == 0u);
        CK(*pass, dq.push_back(3) == 0 && dq.at_checked(0u, &v) == 0 && v == 3);
        dq.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        deque dq = deque();
        dq.drop(); dq.drop();
        CK(*pass, dq.size() == 0u && dq.capacity() == 0u);
        break;
    }
    case CAT_DOUBLE_DROP: {
        deque dq = deque();
        dq.push_back(1);
        dq.drop();
        CK(*pass, dq.size() == 0u);
        dq.drop();
        CK(*pass, dq.capacity() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- list --------------------------------------------------------------- */

static void api_COL_list_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        CK(*pass, lst.size() == 0u);
        lst.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.drop(); lst.drop();
        CK(*pass, lst.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_list_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        CK(*pass, lst.size() == 0u);
        lst.push_back(1); lst.push_front(0);
        CK(*pass, lst.size() == 2u);
        {
            int popped = -1;
            CK(*pass, lst.pop_front(&popped) == 0 && popped == 0);
        }
        CK(*pass, lst.size() == 1u);
        lst.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        list lst = list();
        CK(*pass, lst.size() == 0u && lst.pop_front(NULL) == -1 && lst.pop_back(NULL) == -1);
        lst.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.push_back(1);
        lst.drop(); lst.drop();
        CK(*pass, lst.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        list lst = list();
        stdlib_alloc_fail_at(1);
        CK(*pass, lst.push_back(1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, lst.size() == 0u);
        lst.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_list_push_back(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        CK(*pass, lst.push_back(1) == 0 && lst.push_back(2) == 0);
        CK(*pass, lst.size() == 2u && lst.at_checked(0u, &v) == 0 && v == 1);
        CK(*pass, lst.at_checked(1u, &v) == 0 && v == 2);
        lst.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.push_back(1);
        lst.drop(); lst.drop();
        CK(*pass, lst.push_back(5) == 0 && lst.at_checked(0u, &v) == 0 && v == 5);
        lst.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        list lst = list();
        CK(*pass, lst.push_back(1) == 0);
        stdlib_alloc_fail_at(1);
        CK(*pass, lst.push_back(2) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, lst.size() == 1u && lst.at_checked(0u, &v) == 0 && v == 1);
        lst.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_list_push_front(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        CK(*pass, lst.push_back(2) == 0 && lst.push_front(1) == 0 && lst.push_front(0) == 0);
        CK(*pass, lst.size() == 3u && lst.at_checked(0u, &v) == 0 && v == 0);
        CK(*pass, lst.at_checked(2u, &v) == 0 && v == 2);
        lst.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.push_front(1);
        lst.drop(); lst.drop();
        CK(*pass, lst.push_front(5) == 0 && lst.at_checked(0u, &v) == 0 && v == 5);
        lst.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        list lst = list();
        stdlib_alloc_fail_at(1);
        CK(*pass, lst.push_front(1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, lst.size() == 0u && lst.at_checked(0u, &v) == -1);
        lst.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_list_pop_front(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        lst.push_back(1); lst.push_back(2);
        CK(*pass, lst.pop_front(&v) == 0 && v == 1 && lst.size() == 1u);
        CK(*pass, lst.at_checked(0u, &v) == 0 && v == 2);
        lst.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        list lst = list();
        CK(*pass, lst.pop_front(&v) == -1 && lst.pop_front(NULL) == -1);
        lst.drop();
        break;
    }
    case CAT_ONE_OVER: {
        list lst = list();
        lst.push_back(1);
        CK(*pass, lst.pop_front(&v) == 0 && v == 1);
        CK(*pass, lst.pop_front(&v) == -1 && lst.size() == 0u);
        lst.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.push_back(1);
        lst.drop(); lst.drop();
        CK(*pass, lst.pop_front(&v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_list_pop_back(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        lst.push_back(1); lst.push_back(2);
        CK(*pass, lst.pop_back(&v) == 0 && v == 2 && lst.size() == 1u);
        CK(*pass, lst.at_checked(0u, &v) == 0 && v == 1);
        lst.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        list lst = list();
        CK(*pass, lst.pop_back(&v) == -1 && lst.pop_back(NULL) == -1);
        lst.drop();
        break;
    }
    case CAT_ONE_OVER: {
        list lst = list();
        lst.push_back(1);
        CK(*pass, lst.pop_back(&v) == 0 && v == 1);
        CK(*pass, lst.pop_back(&v) == -1 && lst.size() == 0u);
        lst.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.push_back(1);
        lst.drop(); lst.drop();
        CK(*pass, lst.pop_back(&v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_list_at_checked(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        lst.push_back(1); lst.push_back(2); lst.push_back(3);
        CK(*pass, lst.at_checked(0u, &v) == 0 && v == 1);
        CK(*pass, lst.at_checked(2u, &v) == 0 && v == 3);
        lst.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        list lst = list();
        CK(*pass, lst.at_checked(0u, &v) == -1 && lst.at_checked(0u, NULL) == -1);
        lst.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        list lst = list();
        lst.push_back(7);
        CK(*pass, lst.at_checked(0u, &v) == 0 && v == 7);
        lst.drop();
        break;
    }
    case CAT_ONE_OVER: {
        list lst = list();
        lst.push_back(7);
        CK(*pass, lst.at_checked(1u, &v) == -1);
        lst.drop();
        break;
    }
    case CAT_SIZE_MAX: {
        list lst = list();
        lst.push_back(7);
        CK(*pass, lst.at_checked(SIZE_MAX, &v) == -1);
        lst.drop();
        break;
    }
    case CAT_INVALID_INDEX: {
        list lst = list();
        lst.push_back(7);
        CK(*pass, lst.at_checked(20u, &v) == -1);
        lst.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.push_back(7);
        lst.drop(); lst.drop();
        CK(*pass, lst.at_checked(0u, &v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_list_clear(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        lst.push_back(1); lst.push_back(2);
        lst.clear();
        CK(*pass, lst.size() == 0u && lst.at_checked(0u, &v) == -1);
        CK(*pass, lst.push_back(3) == 0 && lst.at_checked(0u, &v) == 0 && v == 3);
        lst.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        list lst = list();
        lst.clear();
        CK(*pass, lst.size() == 0u);
        lst.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.push_back(1);
        lst.drop(); lst.drop();
        lst.clear();
        CK(*pass, lst.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_list_drop(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        list lst = list();
        lst.push_back(1);
        lst.drop();
        CK(*pass, lst.size() == 0u && lst.at_checked(0u, &v) == -1);
        lst.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        list lst = list();
        lst.drop(); lst.drop();
        CK(*pass, lst.size() == 0u);
        break;
    }
    case CAT_DOUBLE_DROP: {
        list lst = list();
        lst.push_back(1); lst.push_back(2);
        lst.drop(); lst.drop();
        CK(*pass, lst.size() == 0u);
        CK(*pass, lst.push_back(3) == 0 && lst.at_checked(0u, &v) == 0 && v == 3);
        lst.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- stack -------------------------------------------------------------- */

static void api_COL_stack_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        stack stk = stack();
        CK(*pass, stk.size() == 0u);
        stk.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        stack stk = stack();
        stk.drop(); stk.drop();
        CK(*pass, stk.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_stack_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        stack stk = stack();
        CK(*pass, stk.size() == 0u);
        stk.push(1); stk.push(2);
        CK(*pass, stk.size() == 2u);
        {
            int popped = -1;
            CK(*pass, stk.pop(&popped) == 0 && popped == 2);
        }
        CK(*pass, stk.size() == 1u);
        stk.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        stack stk = stack();
        CK(*pass, stk.size() == 0u && stk.pop(NULL) == -1 && stk.top_checked(NULL) == -1);
        stk.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        stack stk = stack();
        stk.push(1);
        stk.drop(); stk.drop();
        CK(*pass, stk.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        stack stk = stack();
        stdlib_alloc_fail_at(1);
        CK(*pass, stk.push(1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, stk.size() == 0u);
        stk.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_stack_push(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        stack stk = stack();
        CK(*pass, stk.push(1) == 0 && stk.push(2) == 0 && stk.size() == 2u);
        CK(*pass, stk.top_checked(&v) == 0 && v == 2);
        stk.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        stack stk = stack();
        int i;
        for (i = 0; i < 8; ++i)
            CK(*pass, stk.push(i) == 0);
        CK(*pass, stk.size() == 8u);
        CK(*pass, stk.push(8) == 0 && stk.size() == 9u && stk.top_checked(&v) == 0 && v == 8);
        stk.drop();
        break;
    }
    case CAT_ONE_OVER: {
        stack stk = stack();
        size_t i;
        for (i = 0; i < (size_t)LIMIT / sizeof(int); ++i)
            CK(*pass, stk.push((int)i) == 0);
        CK(*pass, stk.push(999) == -1 && stk.size() == (size_t)LIMIT / sizeof(int));
        CK(*pass, stk.top_checked(&v) == 0 && v == (int)((size_t)LIMIT / sizeof(int)) - 1);
        stk.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        stack stk = stack();
        stk.push(1);
        stk.drop(); stk.drop();
        CK(*pass, stk.push(5) == 0 && stk.top_checked(&v) == 0 && v == 5);
        stk.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        stack stk = stack();
        CK(*pass, stk.push(1) == 0 && stk.push(2) == 0);
        CK(*pass, stk.push(3) == 0 && stk.push(4) == 0);
        CK(*pass, stk.push(5) == 0 && stk.push(6) == 0);
        CK(*pass, stk.push(7) == 0 && stk.push(8) == 0);
        {
            size_t n0 = stk.size();
            stdlib_alloc_fail_at(1);
            CK(*pass, stk.push(2) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, stk.size() == n0 && stk.top_checked(&v) == 0 && v == 8);
        }
        stk.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_stack_pop(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        stack stk = stack();
        stk.push(1); stk.push(2);
        CK(*pass, stk.pop(&v) == 0 && v == 2 && stk.size() == 1u);
        CK(*pass, stk.top_checked(&v) == 0 && v == 1);
        stk.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        stack stk = stack();
        CK(*pass, stk.pop(&v) == -1 && stk.pop(NULL) == -1);
        stk.drop();
        break;
    }
    case CAT_ONE_OVER: {
        stack stk = stack();
        stk.push(1);
        CK(*pass, stk.pop(&v) == 0 && v == 1);
        CK(*pass, stk.pop(&v) == -1 && stk.size() == 0u);
        stk.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        stack stk = stack();
        stk.push(1);
        stk.drop(); stk.drop();
        CK(*pass, stk.pop(&v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_stack_top_checked(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        stack stk = stack();
        stk.push(1); stk.push(2);
        CK(*pass, stk.top_checked(&v) == 0 && v == 2 && stk.size() == 2u);
        stk.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        stack stk = stack();
        CK(*pass, stk.top_checked(&v) == -1 && stk.top_checked(NULL) == -1);
        stk.drop();
        break;
    }
    case CAT_ONE_OVER: {
        stack stk = stack();
        stk.push(1);
        CK(*pass, stk.top_checked(&v) == 0 && v == 1);
        stk.pop(&v);
        CK(*pass, stk.top_checked(&v) == -1);
        stk.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        stack stk = stack();
        stk.push(1);
        stk.drop(); stk.drop();
        CK(*pass, stk.top_checked(&v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_stack_clear(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        stack stk = stack();
        stk.push(1); stk.push(2);
        stk.clear();
        CK(*pass, stk.size() == 0u && stk.top_checked(&v) == -1);
        CK(*pass, stk.push(3) == 0 && stk.top_checked(&v) == 0 && v == 3);
        stk.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        stack stk = stack();
        stk.clear();
        CK(*pass, stk.size() == 0u);
        stk.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        stack stk = stack();
        stk.push(1);
        stk.drop(); stk.drop();
        stk.clear();
        CK(*pass, stk.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_stack_drop(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        stack stk = stack();
        stk.push(1);
        stk.drop();
        CK(*pass, stk.size() == 0u && stk.top_checked(&v) == -1);
        stk.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        stack stk = stack();
        stk.drop(); stk.drop();
        CK(*pass, stk.size() == 0u);
        break;
    }
    case CAT_DOUBLE_DROP: {
        stack stk = stack();
        stk.push(1); stk.push(2);
        stk.drop(); stk.drop();
        CK(*pass, stk.size() == 0u);
        CK(*pass, stk.push(3) == 0 && stk.top_checked(&v) == 0 && v == 3);
        stk.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

/* ---- queue -------------------------------------------------------------- */

static void api_COL_queue_ctor(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        queue que = queue();
        CK(*pass, que.size() == 0u);
        que.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        queue que = queue();
        que.drop(); que.drop();
        CK(*pass, que.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_queue_size(int cat, int *pass) {
    switch (cat) {
    case CAT_VALID: {
        queue que = queue();
        CK(*pass, que.size() == 0u);
        que.push(1); que.push(2);
        CK(*pass, que.size() == 2u);
        {
            int popped = -1;
            CK(*pass, que.pop(&popped) == 0 && popped == 1);
        }
        CK(*pass, que.size() == 1u);
        que.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        queue que = queue();
        CK(*pass, que.size() == 0u && que.pop(NULL) == -1 && que.front_checked(NULL) == -1);
        que.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        queue que = queue();
        que.push(1);
        que.drop(); que.drop();
        CK(*pass, que.size() == 0u);
        break;
    }
    case CAT_ALLOC_FAIL: {
        queue que = queue();
        stdlib_alloc_fail_at(1);
        CK(*pass, que.push(1) == -1);
        stdlib_alloc_fail_reset();
        CK(*pass, que.size() == 0u);
        que.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_queue_push(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        queue que = queue();
        CK(*pass, que.push(1) == 0 && que.push(2) == 0 && que.size() == 2u);
        CK(*pass, que.front_checked(&v) == 0 && v == 1);
        que.pop(&v);
        CK(*pass, v == 1 && que.front_checked(&v) == 0 && v == 2);
        que.drop();
        break;
    }
    case CAT_EXACT_FIT: {
        queue que = queue();
        int i;
        for (i = 0; i < 8; ++i)
            CK(*pass, que.push(i) == 0);
        CK(*pass, que.size() == 8u && que.front_checked(&v) == 0 && v == 0);
        CK(*pass, que.push(8) == 0 && que.size() == 9u);
        que.drop();
        break;
    }
    case CAT_ONE_OVER: {
        queue que = queue();
        size_t i;
        for (i = 0; i < (size_t)LIMIT / sizeof(int); ++i)
            CK(*pass, que.push((int)i) == 0);
        CK(*pass, que.push(999) == -1 && que.size() == (size_t)LIMIT / sizeof(int));
        CK(*pass, que.front_checked(&v) == 0 && v == 0);
        que.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        queue que = queue();
        que.push(1);
        que.drop(); que.drop();
        CK(*pass, que.push(5) == 0 && que.front_checked(&v) == 0 && v == 5);
        que.drop();
        break;
    }
    case CAT_ALLOC_FAIL: {
        queue que = queue();
        CK(*pass, que.push(1) == 0 && que.push(2) == 0);
        CK(*pass, que.push(3) == 0 && que.push(4) == 0);
        CK(*pass, que.push(5) == 0 && que.push(6) == 0);
        CK(*pass, que.push(7) == 0 && que.push(8) == 0);
        {
            size_t n0 = que.size();
            stdlib_alloc_fail_at(1);
            CK(*pass, que.push(2) == -1);
            stdlib_alloc_fail_reset();
            CK(*pass, que.size() == n0 && que.front_checked(&v) == 0 && v == 1);
        }
        que.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_queue_pop(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        queue que = queue();
        que.push(1); que.push(2);
        CK(*pass, que.pop(&v) == 0 && v == 1 && que.size() == 1u);
        CK(*pass, que.front_checked(&v) == 0 && v == 2);
        que.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        queue que = queue();
        CK(*pass, que.pop(&v) == -1 && que.pop(NULL) == -1);
        que.drop();
        break;
    }
    case CAT_ONE_OVER: {
        queue que = queue();
        que.push(1);
        CK(*pass, que.pop(&v) == 0 && v == 1);
        CK(*pass, que.pop(&v) == -1 && que.size() == 0u);
        que.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        queue que = queue();
        que.push(1);
        que.drop(); que.drop();
        CK(*pass, que.pop(&v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_queue_front_checked(int cat, int *pass) {
    int v = -1;
    switch (cat) {
    case CAT_VALID: {
        queue que = queue();
        que.push(1); que.push(2);
        CK(*pass, que.front_checked(&v) == 0 && v == 1 && que.size() == 2u);
        que.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        queue que = queue();
        CK(*pass, que.front_checked(&v) == -1 && que.front_checked(NULL) == -1);
        que.drop();
        break;
    }
    case CAT_ONE_OVER: {
        queue que = queue();
        que.push(1);
        CK(*pass, que.front_checked(&v) == 0 && v == 1);
        que.pop(&v);
        CK(*pass, que.front_checked(&v) == -1);
        que.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        queue que = queue();
        que.push(1);
        que.drop(); que.drop();
        CK(*pass, que.front_checked(&v) == -1);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_queue_clear(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        queue que = queue();
        que.push(1); que.push(2);
        que.clear();
        CK(*pass, que.size() == 0u && que.front_checked(&v) == -1);
        CK(*pass, que.push(3) == 0 && que.front_checked(&v) == 0 && v == 3);
        que.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        queue que = queue();
        que.clear();
        CK(*pass, que.size() == 0u);
        que.drop();
        break;
    }
    case CAT_DOUBLE_DROP: {
        queue que = queue();
        que.push(1);
        que.drop(); que.drop();
        que.clear();
        CK(*pass, que.size() == 0u);
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

static void api_COL_queue_drop(int cat, int *pass) {
    int v = 0;
    switch (cat) {
    case CAT_VALID: {
        queue que = queue();
        que.push(1);
        que.drop();
        CK(*pass, que.size() == 0u && que.front_checked(&v) == -1);
        que.drop();
        break;
    }
    case CAT_NULL_EMPTY: {
        queue que = queue();
        que.drop(); que.drop();
        CK(*pass, que.size() == 0u);
        break;
    }
    case CAT_DOUBLE_DROP: {
        queue que = queue();
        que.push(1); que.push(2);
        que.drop(); que.drop();
        CK(*pass, que.size() == 0u);
        CK(*pass, que.push(3) == 0 && que.front_checked(&v) == 0 && v == 3);
        que.drop();
        break;
    }
    default:
        *pass = 0;
        break;
    }
}

typedef void (*api_fn)(int, int *);
struct api_ent {
    const char *name;
    api_fn fn;
    unsigned short cats;
};

static const struct api_ent g_apis[] = {
    {"string.hp::stdlib_utf8_validate", api_STR_utf8_validate, CV|CN|CX|CO|CS},
    {"string.hp::stdlib_string_calculate_growth", api_STR_string_calculate_growth, CV|CN|CX|CO|CS},
    {"string.hp::string_in", api_STR_string_in, CV|CN|CX|CO|CF},
    {"string.hp::string_view::string_view(char_ptr,size_t)", api_STR_view_ctor, CV|CN|CS},
    {"string.hp::string_view::size", api_STR_view_size, CV|CN|CD},
    {"string.hp::string_view::data", api_STR_view_data, CV|CN|CD},
    {"string.hp::string_view::equals", api_STR_view_equals, CV|CN|CX|CO},
    {"string.hp::string_view::is_valid_utf8", api_STR_view_is_valid_utf8, CV|CN|CX|CO|CS},
    {"string.hp::string::string(char_ptr)", api_STR_string_ctor, CV|CN|CX|CO|CF},
    {"string.hp::string::size", api_STR_string_size, CV|CN|CD|CF},
    {"string.hp::string::capacity", api_STR_string_capacity, CV|CN|CX|CD|CF},
    {"string.hp::string::valid", api_STR_string_valid, CV|CN|CD|CF},
    {"string.hp::string::at", api_STR_string_at, CV|CN|CX|CO|CS|CD|CI|CF},
    {"string.hp::string::c_str", api_STR_string_c_str, CV|CN|CD|CF},
    {"string.hp::string::equals", api_STR_string_equals, CV|CN|CX|CO|CA|CD},
    {"string.hp::string::compare", api_STR_string_compare, CV|CN|CX|CO|CA|CD},
    {"string.hp::string::find", api_STR_string_find, CV|CN|CA|CD},
    {"string.hp::string::reserve", api_STR_string_reserve, CV|CN|CX|CO|CS|CD|CF},
    {"string.hp::string::append", api_STR_string_append, CV|CN|CX|CO|CA|CD|CF},
    {"string.hp::string::clear", api_STR_string_clear, CV|CN|CD},
    {"string.hp::string::copy_from", api_STR_string_copy_from, CV|CN|CX|CA|CD|CF},
    {"string.hp::string::is_valid_utf8", api_STR_string_is_valid_utf8, CV|CN|CX|CO|CD},
    {"string.hp::string::substr", api_STR_string_substr, CV|CN|CX|CO|CS|CA|CD|CI},
    {"string.hp::string::count_fields", api_STR_string_count_fields, CV|CN|CX|CO|CD},
    {"string.hp::string::field", api_STR_string_field, CV|CN|CX|CO|CS|CA|CD|CI},
    {"string.hp::string::to_view", api_STR_string_to_view, CV|CN|CD},
    {"string.hp::string::drop", api_STR_string_drop, CV|CN|CD},
    {"vector.hp::vector_allocation_bytes", api_VEC_allocation_bytes, CV|CN|CX|CO|CS},
    {"vector.hp::vector_calculate_growth", api_VEC_calculate_growth, CV|CN|CX|CO|CS},
    {"vector.hp::vector_in", api_VEC_vector_in, CV|CN|CD|CF},
    {"vector.hp::slice::slice(int*,size_t)", api_VEC_slice_ctor, CV|CN|CS},
    {"vector.hp::slice::size", api_VEC_slice_size, CV|CN|CD},
    {"vector.hp::slice::stale", api_VEC_slice_stale, CV|CO|CD},
    {"vector.hp::slice::valid", api_VEC_slice_valid, CV|CN|CO|CD},
    {"vector.hp::slice::at_checked", api_VEC_slice_at_checked, CV|CN|CX|CO|CS|CD|CI},
    {"vector.hp::slice::sub", api_VEC_slice_sub, CV|CN|CX|CO|CS|CD|CI},
    {"vector.hp::vector::vector()", api_VEC_vector_ctor, CV|CD},
    {"vector.hp::vector::size", api_VEC_vector_size, CV|CN|CD|CF},
    {"vector.hp::vector::capacity", api_VEC_vector_capacity, CV|CX|CD},
    {"vector.hp::vector::reserve", api_VEC_vector_reserve, CV|CN|CX|CO|CS|CD|CF},
    {"vector.hp::vector::push_back", api_VEC_vector_push_back, CV|CX|CO|CD|CF},
    {"vector.hp::vector::at", api_VEC_vector_at, CV|CN|CX|CO|CS|CD|CI},
    {"vector.hp::vector::at_checked", api_VEC_vector_at_checked, CV|CN|CX|CO|CS|CD|CI},
    {"vector.hp::vector::pop_back", api_VEC_vector_pop_back, CV|CN|CX|CO|CD},
    {"vector.hp::vector::insert", api_VEC_vector_insert, CV|CN|CX|CO|CS|CD|CF},
    {"vector.hp::vector::erase", api_VEC_vector_erase, CV|CN|CX|CO|CS|CD|CI},
    {"vector.hp::vector::clear", api_VEC_vector_clear, CV|CN|CD},
    {"vector.hp::vector::copy_from", api_VEC_vector_copy_from, CV|CN|CX|CA|CD|CF},
    {"vector.hp::vector::shrink_to_fit", api_VEC_vector_shrink_to_fit, CV|CN|CX|CD|CF},
    {"vector.hp::vector::view", api_VEC_vector_view, CV|CN|CD},
    {"vector.hp::vector::drop", api_VEC_vector_drop, CV|CN|CD},
    {"collections.hp::stdlib_map_hash_int", api_COL_map_hash_int, CV|CN|CS},
    {"collections.hp::stdlib_map_hash_text", api_COL_map_hash_text, CV|CN|CX|CO},
    {"collections.hp::stdlib_map_next_pow2", api_COL_map_next_pow2, CV|CN|CX|CO|CS},
    {"collections.hp::stdlib_map_needs_room", api_COL_map_needs_room, CV|CN|CX|CO|CS},
    {"collections.hp::stdlib_map_rebuild_capacity", api_COL_map_rebuild_capacity, CV|CN|CX|CO|CS},
    {"collections.hp::stdlib_ring_copy", api_COL_ring_copy, CV|CN|CX|CO|CS|CF},
    {"collections.hp::intmap::intmap()", api_COL_intmap_ctor, CV|CD},
    {"collections.hp::intmap::size", api_COL_intmap_size, CV|CN|CD|CF},
    {"collections.hp::intmap::valid", api_COL_intmap_valid, CV|CN|CD},
    {"collections.hp::intmap::grow", api_COL_intmap_grow, CV|CN|CX|CO|CS|CD|CF},
    {"collections.hp::intmap::put", api_COL_intmap_put, CV|CX|CO|CS|CD|CF},
    {"collections.hp::intmap::get", api_COL_intmap_get, CV|CN|CS|CD},
    {"collections.hp::intmap::contains", api_COL_intmap_contains, CV|CN|CS|CD},
    {"collections.hp::intmap::remove", api_COL_intmap_remove, CV|CN|CS|CD},
    {"collections.hp::intmap::clear", api_COL_intmap_clear, CV|CN|CD},
    {"collections.hp::intmap::drop", api_COL_intmap_drop, CV|CN|CD},
    {"collections.hp::strmap::strmap()", api_COL_strmap_ctor, CV|CD},
    {"collections.hp::strmap::size", api_COL_strmap_size, CV|CN|CD|CF},
    {"collections.hp::strmap::valid", api_COL_strmap_valid, CV|CN|CD},
    {"collections.hp::strmap::grow", api_COL_strmap_grow, CV|CN|CX|CO|CS|CD|CF},
    {"collections.hp::strmap::put", api_COL_strmap_put, CV|CN|CX|CO|CD|CF},
    {"collections.hp::strmap::get", api_COL_strmap_get, CV|CN|CD},
    {"collections.hp::strmap::contains", api_COL_strmap_contains, CV|CN|CD},
    {"collections.hp::strmap::remove", api_COL_strmap_remove, CV|CN|CD},
    {"collections.hp::strmap::clear", api_COL_strmap_clear, CV|CN|CD},
    {"collections.hp::strmap::drop", api_COL_strmap_drop, CV|CN|CD},
    {"collections.hp::intset::intset()", api_COL_intset_ctor, CV|CD},
    {"collections.hp::intset::size", api_COL_intset_size, CV|CN|CD|CF},
    {"collections.hp::intset::valid", api_COL_intset_valid, CV|CN|CD},
    {"collections.hp::intset::grow", api_COL_intset_grow, CV|CN|CX|CO|CS|CD|CF},
    {"collections.hp::intset::insert", api_COL_intset_insert, CV|CX|CO|CS|CD|CF},
    {"collections.hp::intset::contains", api_COL_intset_contains, CV|CN|CS|CD},
    {"collections.hp::intset::remove", api_COL_intset_remove, CV|CN|CS|CD},
    {"collections.hp::intset::clear", api_COL_intset_clear, CV|CN|CD},
    {"collections.hp::intset::drop", api_COL_intset_drop, CV|CN|CD},
    {"collections.hp::deque::deque()", api_COL_deque_ctor, CV|CD},
    {"collections.hp::deque::size", api_COL_deque_size, CV|CN|CD|CF},
    {"collections.hp::deque::capacity", api_COL_deque_capacity, CV|CX|CD},
    {"collections.hp::deque::reserve", api_COL_deque_reserve, CV|CN|CX|CO|CS|CD|CF},
    {"collections.hp::deque::push_back", api_COL_deque_push_back, CV|CX|CO|CD|CF},
    {"collections.hp::deque::push_front", api_COL_deque_push_front, CV|CX|CO|CD|CF},
    {"collections.hp::deque::pop_front", api_COL_deque_pop_front, CV|CN|CO|CD},
    {"collections.hp::deque::pop_back", api_COL_deque_pop_back, CV|CN|CO|CD},
    {"collections.hp::deque::at_checked", api_COL_deque_at_checked, CV|CN|CX|CO|CS|CD|CI},
    {"collections.hp::deque::clear", api_COL_deque_clear, CV|CN|CD},
    {"collections.hp::deque::drop", api_COL_deque_drop, CV|CN|CD},
    {"collections.hp::list::list()", api_COL_list_ctor, CV|CD},
    {"collections.hp::list::size", api_COL_list_size, CV|CN|CD|CF},
    {"collections.hp::list::push_back", api_COL_list_push_back, CV|CD|CF},
    {"collections.hp::list::push_front", api_COL_list_push_front, CV|CD|CF},
    {"collections.hp::list::pop_front", api_COL_list_pop_front, CV|CN|CO|CD},
    {"collections.hp::list::pop_back", api_COL_list_pop_back, CV|CN|CO|CD},
    {"collections.hp::list::at_checked", api_COL_list_at_checked, CV|CN|CX|CO|CS|CD|CI},
    {"collections.hp::list::clear", api_COL_list_clear, CV|CN|CD},
    {"collections.hp::list::drop", api_COL_list_drop, CV|CN|CD},
    {"collections.hp::stack::stack()", api_COL_stack_ctor, CV|CD},
    {"collections.hp::stack::size", api_COL_stack_size, CV|CN|CD|CF},
    {"collections.hp::stack::push", api_COL_stack_push, CV|CX|CO|CD|CF},
    {"collections.hp::stack::pop", api_COL_stack_pop, CV|CN|CO|CD},
    {"collections.hp::stack::top_checked", api_COL_stack_top_checked, CV|CN|CO|CD},
    {"collections.hp::stack::clear", api_COL_stack_clear, CV|CN|CD},
    {"collections.hp::stack::drop", api_COL_stack_drop, CV|CN|CD},
    {"collections.hp::queue::queue()", api_COL_queue_ctor, CV|CD},
    {"collections.hp::queue::size", api_COL_queue_size, CV|CN|CD|CF},
    {"collections.hp::queue::push", api_COL_queue_push, CV|CX|CO|CD|CF},
    {"collections.hp::queue::pop", api_COL_queue_pop, CV|CN|CO|CD},
    {"collections.hp::queue::front_checked", api_COL_queue_front_checked, CV|CN|CO|CD},
    {"collections.hp::queue::clear", api_COL_queue_clear, CV|CN|CD},
    {"collections.hp::queue::drop", api_COL_queue_drop, CV|CN|CD},
};
#define N_APIS ((int)(sizeof(g_apis) / sizeof(g_apis[0])))

/* ======================================================================== */
/* fork-per-case driver                                                      */
/* ======================================================================== */

#define LOG_PATH "tests/edge100/containers/edge_cases.log"

/* 1 only when the child ran the exact case, exited cleanly, wrote a complete
 * verdict, left the canary untouched and reported PASS. */
static int classify(int waited_ok, int status, ssize_t got, struct verdict *v,
                    int api, int cat) {
    if (!waited_ok || WIFSIGNALED(status) || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0 || got != (ssize_t)sizeof(*v))
        return 0;
    if (v->api != api || v->cat != cat)
        return 0;
    if (v->canary != 1 || v->pass != 1)
        return 0;
    return 1;
}

static int selfcheck(void) {
    struct verdict v;
    int status = 0;
    int passed = 0;
    int checks = 0;
    v.api = 0; v.cat = 0; v.pass = 1; v.canary = 1;
    ++checks; if (classify(1, status, (ssize_t)sizeof(v), &v, 0, 0)) ++passed;
    ++checks; if (!classify(0, status, (ssize_t)sizeof(v), &v, 0, 0)) ++passed;
    ++checks; if (!classify(1, 11, (ssize_t)sizeof(v), &v, 0, 0)) ++passed;
    ++checks; if (!classify(1, (7 << 8), (ssize_t)sizeof(v), &v, 0, 0)) ++passed;
    ++checks; if (!classify(1, status, 2, &v, 0, 0)) ++passed;
    ++checks; if (!classify(1, status, (ssize_t)sizeof(v), &v, 1, 0)) ++passed;
    v.pass = 0;
    ++checks; if (!classify(1, status, (ssize_t)sizeof(v), &v, 0, 0)) ++passed;
    v.pass = 1; v.canary = 0;
    ++checks; if (!classify(1, status, (ssize_t)sizeof(v), &v, 0, 0)) ++passed;
    fprintf(stderr, "SELFCHECK %d %d\n", passed, checks);
    return passed == checks ? 0 : 1;
}

int main(void) {
    const char *selfcheck_env = getenv("EDGE100_SELFCHECK");
    int logfd;
    int line_len;
    char line[512];
    int ai, ci;
    int cases = 0;
    int passed = 0;
    int failed = 0;
    int stop = 0;
    int fds[2];
    pid_t pid;
    int status = 0;
    ssize_t got;
    struct verdict result;

    if (selfcheck_env != (const char *)0 && strcmp(selfcheck_env, "1") == 0)
        return selfcheck();

    stdlib_set_error_callback((stdlib_error_callback)0);
    stdlib_telemetry_set_enabled(0);

    logfd = open(LOG_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (logfd < 0) {
        fprintf(stderr, "error: cannot open %s\n", LOG_PATH);
        fprintf(stderr, "EDGE100 containers %d 0 0 0\n", N_APIS);
        return 2;
    }
    line_len = snprintf(line, sizeof(line),
                        "# EDGE100 containers apis=%d cats=%d limit=%d\n",
                        N_APIS, N_CATS, LIMIT);
    if (line_len > 0)
        (void)write(logfd, line, (size_t)line_len);

    for (ai = 0; ai < N_APIS && !stop; ++ai) {
        for (ci = 0; ci < N_CATS && !stop; ++ci) {
            int good;
            if ((g_apis[ai].cats & (1u << ci)) == 0u)
                continue;
            canary_init();
            if (pipe(fds) != 0) {
                fprintf(stderr, "error: pipe failed at %s/%s\n",
                        g_apis[ai].name, g_cat_names[ci]);
                stop = 1;
                break;
            }
            fflush(stdout);
            pid = fork();
            if (pid < 0) {
                fprintf(stderr, "error: fork failed at %s/%s\n",
                        g_apis[ai].name, g_cat_names[ci]);
                close(fds[0]);
                close(fds[1]);
                stop = 1;
                break;
            }
            if (pid == 0) {
                int child_pass = 1;
                struct verdict child_result;
                close(fds[0]);
                g_apis[ai].fn(ci, &child_pass);
                child_result.api = ai;
                child_result.cat = ci;
                child_result.pass = child_pass;
                child_result.canary = canary_ok();
                (void)write(fds[1], &child_result, sizeof(child_result));
                close(fds[1]);
                _exit(0);
            }
            close(fds[1]);
            got = read(fds[0], &result, sizeof(result));
            close(fds[0]);
            good = classify(waitpid(pid, &status, 0) == pid, status, got,
                            &result, ai, ci);
            ++cases;
            if (good)
                ++passed;
            else
                ++failed;
            line_len = snprintf(line, sizeof(line), "%d %s %s %s\n", ai,
                                g_apis[ai].name, g_cat_names[ci],
                                good ? "PASS" : "FAIL");
            if (line_len > 0)
                (void)write(logfd, line, (size_t)line_len);
        }
    }

    line_len = snprintf(line, sizeof(line),
                        "# SUMMARY apis=%d cases=%d passed=%d failed=%d\n",
                        N_APIS, cases, passed, failed);
    if (line_len > 0)
        (void)write(logfd, line, (size_t)line_len);
    close(logfd);

    fprintf(stderr, "EDGE100 containers %d %d %d %d\n", N_APIS, cases,
            passed, failed);
    return 0;
}
