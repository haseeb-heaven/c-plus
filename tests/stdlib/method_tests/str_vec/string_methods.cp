/* Per-method public-API test for stdlib/string.hp (24 entries).
 *
 * Every public callable enumerated by tests/stdlib/coverage.cp is exercised
 * here with a deterministic case that actually CALLS the method and verifies
 * its observable result.  One line is printed per method:
 *
 *     <header> <method> PASS|FAIL
 *
 * and a final summary line:
 *
 *     METHOD_TEST str_vec <total> <passed> <failed> <skipped>
 *
 * The process exits non-zero iff any method fails.  STDLIB_ALLOC_LIMIT is
 * pinned to 4096 bytes so exact-fit, one-over and refusal boundaries are
 * reachable without large allocations.  No random input is used.
 *
 * Note on the two stdlib_-prefixed free helpers in string.hp
 * (stdlib_utf8_validate / stdlib_string_calculate_growth): the coverage tool's
 * is_skipped() deliberately excludes stdlib_* names, so they are NOT counted
 * in the 24 string entries.  They are nevertheless invoked and verified here
 * inside the is_valid_utf8 / append method cases (documented in RUN_NOTES.md).
 */
#define STDLIB_ALLOC_LIMIT 4096

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"


static int g_total = 0;
static int g_passed = 0;
static int g_failed = 0;
static int g_skipped = 0;

static void report(const char *header, const char *method, int ok) {
    g_total++;
    if (ok)
        g_passed++;
    else
        g_failed++;
    print(header, method, ok ? "PASS" : "FAIL");
}

/* ====================================================================== */
/*                          string.hp  (24 entries)                        */
/* ====================================================================== */

/* 1. string_view constructor. */
static void t_string_view_ctor(void) {
    const char *text = "abcdef";
    string_view v = string_view(text, 3);
    int ok = 1;
    ok = ok && v.size() == 3;
    ok = ok && v.data() == text;
    ok = ok && v.data()[0] == 'a';
    report("string.hp", "string_view::string_view", ok);
}

/* 2. string_view::size. */
static void t_string_view_size(void) {
    string_view v = string_view("abc", 3);
    string_view z = string_view("abc", 0);
    report("string.hp", "string_view::size",
           v.size() == 3 && z.size() == 0);
}

/* 3. string_view::data. */
static void t_string_view_data(void) {
    string_view v = string_view("abcXYZ", 3);
    const char *expect = "abc";
    /* A view is a fixed-length window, not NUL-terminated at its boundary. */
    report("string.hp", "string_view::data",
           v.data() != NULL && memcmp(v.data(), expect, 3) == 0 &&
           v.data()[3] == 'X'); /* proves the pointer is into the source */
}

/* 4. string_view::equals (incl. NULL, length mismatch, empty). */
static void t_string_view_equals(void) {
    string_view v = string_view("abc", 3);
    string_view z = string_view("abc", 0);
    int ok = 1;
    ok = ok && v.equals("abc") == 1;
    ok = ok && v.equals("ab") == 0;
    ok = ok && v.equals("abcd") == 0;
    ok = ok && v.equals(NULL) == 0;
    ok = ok && z.equals("") == 1;
    report("string.hp", "string_view::equals", ok);
}

/* 5. string_view::is_valid_utf8. */
static void t_string_view_utf8(void) {
    string_view good = string_view("\xe2\x82\xac", 3);
    string_view bad = string_view("\xc0\x80", 2);
    report("string.hp", "string_view::is_valid_utf8",
           good.is_valid_utf8() == 1 && bad.is_valid_utf8() == 0);
}

/* 6. string constructor (normal, empty, NULL). */
static void t_string_ctor(void) {
    string s = string("hello");
    string e = string("");
    string n = string(NULL);
    const char *expect = "hello";
    int ok = 1;
    ok = ok && s.valid() == 1 && s.size() == 5 && strcmp(s.c_str(), expect) == 0;
    ok = ok && e.valid() == 1 && e.size() == 0 && e.capacity() == 1;
    ok = ok && n.valid() == 1 && n.size() == 0 && n.capacity() == 1;
    report("string.hp", "string::string", ok);
}

/* 7. string::size. */
static void t_string_size(void) {
    string s = string("hello world");
    string e = string("");
    report("string.hp", "string::size",
           s.size() == 11 && e.size() == 0);
}

/* 8. string::capacity (exact-fit n+1; capacity after growth). */
static void t_string_capacity(void) {
    string s = string("hello");
    int ok = 1;
    ok = ok && s.capacity() == 6;
    s.reserve(32);
    ok = ok && s.capacity() >= 32;
    report("string.hp", "string::capacity", ok);
}

/* 9. string::valid (ctor, drop, re-init via reserve). */
static void t_string_valid(void) {
    string s = string("abc");
    int ok = 1;
    ok = ok && s.valid() == 1;
    s.drop();
    ok = ok && s.valid() == 0;
    ok = ok && s.reserve(4) == 0 && s.valid() == 1;
    report("string.hp", "string::valid", ok);
}

/* 10. string::at (in range + refusal at/after size). */
static void t_string_at(void) {
    string s = string("hello");
    int ok = 1;
    ok = ok && s.at(0) == 'h';
    ok = ok && s.at(4) == 'o';
    ok = ok && s.at(5) == '\0';
    ok = ok && s.at(999) == '\0';
    report("string.hp", "string::at", ok);
}

/* 11. string::c_str (live and post-drop behaviour). */
static void t_string_c_str(void) {
    string s = string("hello");
    int ok = 1;
    ok = ok && s.c_str() != NULL && strcmp(s.c_str(), "hello") == 0;
    s.drop();
    ok = ok && s.c_str() != NULL && s.c_str()[0] == '\0';
    report("string.hp", "string::c_str", ok);
}

/* 12. string::equals (exact, mismatch, empty, NULL). */
static void t_string_equals(void) {
    string s = string("hello");
    string e = string("");
    int ok = 1;
    ok = ok && s.equals("hello") == 1;
    ok = ok && s.equals("hell") == 0;
    ok = ok && s.equals("hello!") == 0;
    ok = ok && s.equals(NULL) == 0;
    ok = ok && e.equals("") == 1;
    report("string.hp", "string::equals", ok);
}

/* 13. string::compare (-1/0/1 + NULL). */
static void t_string_compare(void) {
    string s = string("hello");
    int ok = 1;
    ok = ok && s.compare("hello") == 0;
    ok = ok && s.compare("hell") == 1;
    ok = ok && s.compare("i") == -1;
    ok = ok && s.compare(NULL) == 1;
    report("string.hp", "string::compare", ok);
}

/* 14. string::find (hit, miss, empty needle, NULL). */
static void t_string_find(void) {
    string s = string("hello world");
    int ok = 1;
    ok = ok && s.find("world") == 6;
    ok = ok && s.find("lo") == 3;
    ok = ok && s.find("xyz") == -1;
    ok = ok && s.find("") == 0;
    ok = ok && s.find(NULL) == -1;
    report("string.hp", "string::find", ok);
}

/* 15. string::reserve (grow, no-op shrink request, over-limit refusal).
 *     Also directly invokes stdlib_string_calculate_growth (bonus). */
static void t_string_reserve(void) {
    string s = string("abc");
    int ok = 1;
    ok = ok && s.reserve(16) == 0 && s.capacity() >= 16;
    ok = ok && strcmp(s.c_str(), "abc") == 0;
    ok = ok && s.reserve(2) == 0 && s.capacity() >= 16;
    ok = ok && s.reserve(4097) == -1;
    ok = ok && s.capacity() >= 16 && strcmp(s.c_str(), "abc") == 0;
    /* bonus: upstream additive growth helper, verified inline */
    ok = ok && stdlib_string_calculate_growth(5, 3) == 6;
    ok = ok && stdlib_string_calculate_growth(9, 3) == 9;
    report("string.hp", "string::reserve", ok);
}

/* 16. string::append (empty, NULL, exact-fit, growth, self-append,
 *     self-suffix, overflow refusal with failure atomicity). */
static void t_string_append(void) {
    string s = string("ab");
    int ok = 1;

    /* empty append is a no-op */
    ok = ok && s.append("") == 0 && strcmp(s.c_str(), "ab") == 0;
    /* NULL is refused and leaves the string intact */
    ok = ok && s.append(NULL) == -1 && strcmp(s.c_str(), "ab") == 0;
    /* exact fit: cap 3, appending 1 char needs 4 -> growth; use reserve first */
    ok = ok && s.reserve(16) == 0;
    ok = ok && s.append("cd") == 0 && strcmp(s.c_str(), "abcd") == 0;
    ok = ok && s.capacity() >= 16; /* no reallocation on this path */
    /* self-append in place (source aliases destination tail) */
    ok = ok && s.append(s.c_str()) == 0 && strcmp(s.c_str(), "abcdabcd") == 0;
    /* growth path with self suffix, old buffer alive during copy */
    {
        string g = string("abcd");
        ok = ok && g.append(g.c_str() + 2) == 0;
        ok = ok && strcmp(g.c_str(), "abcdcd") == 0;
        ok = ok && g.capacity() > 5;
    }
    /* overflow: refusal is failure-atomic (no small scratch buffer) */
    {
        string big = string("a");
        size_t i;
        size_t before_size;
        for (i = 0; i < 5000; i++)
            big.append("bbbbbbbbbb");
        before_size = big.size();
        ok = ok && before_size > 0;
        ok = ok && big.append("trigger overflow") == -1;
        ok = ok && big.size() == before_size;
        ok = ok && big.at(0) == 'a' && big.at(before_size - 1) == 'b';
    }
    report("string.hp", "string::append", ok);
}

/* 17. string::clear (size/contents reset, capacity preserved). */
static void t_string_clear(void) {
    string s = string("hello");
    size_t cap;
    int ok = 1;
    cap = s.capacity();
    s.clear();
    ok = ok && s.size() == 0 && s.c_str()[0] == '\0';
    ok = ok && s.capacity() == cap;
    ok = ok && s.append("x") == 0 && strcmp(s.c_str(), "x") == 0;
    report("string.hp", "string::clear", ok);
}

/* 18. string::copy_from (copy, empty, self, NULL). */
static void t_string_copy_from(void) {
    string src = string("source");
    string empty = string("");
    string dst = string("x");
    int ok = 1;
    ok = ok && dst.copy_from(&src) == 0;
    ok = ok && strcmp(dst.c_str(), "source") == 0 && dst.capacity() == 7;
    ok = ok && dst.copy_from(&dst) == 0 && strcmp(dst.c_str(), "source") == 0;
    ok = ok && dst.copy_from(NULL) == -1 && strcmp(dst.c_str(), "source") == 0;
    ok = ok && dst.copy_from(&empty) == 0;
    ok = ok && dst.size() == 0 && dst.capacity() == 1 && dst.valid() == 1;
    report("string.hp", "string::copy_from", ok);
}

/* 19. string::is_valid_utf8 (ASCII, empty, valid multibyte, overlong,
 *     surrogate).  Also directly invokes stdlib_utf8_validate (bonus). */
static void t_string_utf8(void) {
    string ascii = string("abc");
    string empty = string("");
    string euro = string("\xe2\x82\xac");
    string overlong = string("\xc0\x80");
    string surrogate = string("\xed\xa0\x80");
    int ok = 1;
    ok = ok && ascii.is_valid_utf8() == 1;
    ok = ok && empty.is_valid_utf8() == 1;
    ok = ok && euro.is_valid_utf8() == 1;
    ok = ok && overlong.is_valid_utf8() == 0;
    ok = ok && surrogate.is_valid_utf8() == 0;
    /* bonus: direct free-helper calls */
    ok = ok && stdlib_utf8_validate("abc", 3) == 1;
    ok = ok && stdlib_utf8_validate("\xc0\x80", 2) == 0;
    ok = ok && stdlib_utf8_validate(NULL, 1) == 0;
    ok = ok && stdlib_utf8_validate(NULL, 0) == 1;
    report("string.hp", "string::is_valid_utf8", ok);
}

/* 20. string::substr (normal, clamp, exact-fit, one-over, bad args). */
static void t_string_substr(void) {
    string s = string("hello world");
    char buf[16];
    char tight[6];
    char shortbuf[5];
    int ok = 1;
    buf[0] = '\0';
    tight[0] = '\0';
    shortbuf[0] = '\0';
    ok = ok && s.substr(6, 5, buf, sizeof(buf)) == 0;
    ok = ok && strcmp(buf, "world") == 0;
    ok = ok && s.substr(6, 100, buf, sizeof(buf)) == 0;
    ok = ok && strcmp(buf, "world") == 0; /* clamped to available */
    ok = ok && s.substr(6, 5, tight, 6) == 0 && strcmp(tight, "world") == 0;
    ok = ok && s.substr(6, 5, shortbuf, 5) == 0 && strcmp(shortbuf, "worl") == 0;
    ok = ok && s.substr(12, 1, buf, sizeof(buf)) == -1;
    ok = ok && s.substr(0, 5, NULL, sizeof(buf)) == -1;
    ok = ok && s.substr(0, 5, buf, 0) == -1;
    ok = ok && s.substr(11, 4, buf, sizeof(buf)) == 0 && buf[0] == '\0';
    report("string.hp", "string::substr", ok);
}

/* 21. string::count_fields. */
static void t_string_count_fields(void) {
    string a = string("a,b,c");
    string b = string("abc");
    string c = string("");
    string d = string(",,");
    report("string.hp", "string::count_fields",
           a.count_fields(',') == 3 && b.count_fields(',') == 1 &&
           c.count_fields(',') == 1 && d.count_fields(',') == 3);
}

/* 22. string::field (indices, missing index, truncation, NULL). */
static void t_string_field(void) {
    string s = string("a,b,c");
    char buf[8];
    int ok = 1;
    ok = ok && s.field(',', 0, buf, sizeof(buf)) == 0 && strcmp(buf, "a") == 0;
    ok = ok && s.field(',', 1, buf, sizeof(buf)) == 0 && strcmp(buf, "b") == 0;
    ok = ok && s.field(',', 2, buf, sizeof(buf)) == 0 && strcmp(buf, "c") == 0;
    ok = ok && s.field(',', 3, buf, sizeof(buf)) == -1;
    ok = ok && s.field(',', 1, buf, 2) == 0 && strcmp(buf, "b") == 0;
    ok = ok && s.field(',', 1, buf, 1) == 0 && buf[0] == '\0';
    ok = ok && s.field(',', 0, NULL, sizeof(buf)) == -1;
    report("string.hp", "string::field", ok);
}

/* 23. string::to_view (size, pointer identity, content via view methods). */
static void t_string_to_view(void) {
    string s = string("hello");
    string_view v = s.to_view();
    int ok = 1;
    ok = ok && v.size() == 5;
    ok = ok && v.data() == s.c_str();
    ok = ok && v.equals("hello") == 1;
    ok = ok && v.is_valid_utf8() == 1;
    report("string.hp", "string::to_view", ok);
}

/* 24. string::drop (invalidates, zeros size/cap, c_str empty, double drop). */
static void t_string_drop(void) {
    string s = string("abc");
    int ok = 1;
    s.drop();
    ok = ok && s.valid() == 0 && s.size() == 0 && s.capacity() == 0;
    ok = ok && s.c_str()[0] == '\0' && s.at(0) == '\0';
    ok = ok && s.equals("") == 1;
    s.drop();
    ok = ok && s.valid() == 0;
    report("string.hp", "string::drop", ok);
}


int main(void) {
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_set_enabled(0);

    t_string_view_ctor();
    t_string_view_size();
    t_string_view_data();
    t_string_view_equals();
    t_string_view_utf8();
    t_string_ctor();
    t_string_size();
    t_string_capacity();
    t_string_valid();
    t_string_at();
    t_string_c_str();
    t_string_equals();
    t_string_compare();
    t_string_find();
    t_string_reserve();
    t_string_append();
    t_string_clear();
    t_string_copy_from();
    t_string_utf8();
    t_string_substr();
    t_string_count_fields();
    t_string_field();
    t_string_to_view();
    t_string_drop();
    print("METHOD_TEST", "str_vec", g_total, g_passed, g_failed, g_skipped);
    return g_failed == 0 ? 0 : 1;
}
