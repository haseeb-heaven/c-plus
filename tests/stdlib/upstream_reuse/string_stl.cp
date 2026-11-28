/* Upstream-derived regression tests for stdlib/string.hp.
 *
 * Capacity growth and overlap-safe append are ported from Microsoft STL
 * <xstring> (`_Calculate_growth`, `_Append`, `_Reallocate_grow_by`); see
 * string_stl_NOTES.md for the exact URL, commit, licence and adaptations.
 *
 * STDLIB_ALLOC_LIMIT is reduced to 4096 for this TU so the growth-refusal
 * branch is reachable with small, fast inputs.  This is the documented
 * allocator override, not a change to string.hp.  Every check is
 * self-verifying; no stdout fixture is needed.
 */
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

static int checks = 0;
static int failures = 0;

static void ck(int cond, const char *label) {
    checks++;
    if (!cond) {
        failures++;
        print("FAIL", label);
    }
}

/* 1. Self-append across a reallocation: source aliases the whole live prefix
 *    and the old buffer is retired only after the copy (MSVC _Append +
 *    _Reallocate_grow_by). */
static void self_append(void) {
    string s = string("hello");
    const char *old = s.c_str();
    ck(s.append(s.c_str()) == 0, "self-append-rc");
    ck(s.size() == 10 && s.capacity() == 12, "self-append-size-cap");
    ck(strcmp(s.c_str(), "hellohello") == 0, "self-append-content");
    ck(s.c_str() != old, "self-append-reallocated");

    /* Repeated doubling stays correct and keeps the x2 capacity contract. */
    string g = string("x");
    int all_x = 1;
    size_t i;
    for (i = 0; i < 6; i++) {
        if (g.append(g.c_str()) != 0)
            all_x = 0;
    }
    ck(all_x == 1 && g.size() == 64 && g.capacity() == 128,
       "self-append-repeat");
    all_x = 1;
    for (i = 0; i < g.size(); i++)
        if (g.at(i) != 'x')
            all_x = 0;
    ck(all_x == 1, "self-append-repeat-content");
}

/* 2. Interior-suffix append, both with and without reallocation. */
static void interior_suffix(void) {
    string p = string("abcdef");
    ck(p.append(p.c_str() + 3) == 0, "interior-grow-rc");
    ck(p.size() == 9 && p.capacity() == 14, "interior-grow-size-cap");
    ck(strcmp(p.c_str(), "abcdefdef") == 0, "interior-grow-content");

    string q = string("0123456789");
    const char *qold;
    ck(q.reserve(40) == 0, "interior-fit-reserve");
    qold = q.c_str();
    ck(q.append(q.c_str() + 7) == 0, "interior-fit-rc");
    ck(q.c_str() == qold && q.capacity() == 40, "interior-fit-no-realloc");
    ck(q.size() == 13 && strcmp(q.c_str(), "0123456789789") == 0,
       "interior-fit-content");
}

/* 3. Alias after clear(): the source pointer is inside the allocation but
 *    past the new (zero) count.  The in-place move must be overlap-safe. */
static void alias_after_clear(void) {
    string c = string("abcdef");
    const char *tail = c.c_str() + 3;
    c.clear();
    ck(c.size() == 0 && c.capacity() == 7, "clear-state");
    ck(c.append(tail) == 0, "alias-after-clear-rc");
    ck(strcmp(c.c_str(), "def") == 0, "alias-after-clear-content");

    string d = string("prefixSUFFIX");
    const char *suffix = d.c_str() + 6;
    d.clear();
    ck(d.append(suffix) == 0 && strcmp(d.c_str(), "SUFFIX") == 0,
       "alias-after-clear-suffix");

    /* Appending the terminator of an emptied string is an empty append. */
    string e = string("xy");
    e.clear();
    ck(e.append(e.c_str()) == 0 && e.size() == 0 && e.capacity() == 3,
       "alias-after-clear-empty");
}

/* 4. Growth refusal: arithmetic overflow and hard-limit checks happen before
 *    any allocation, and failure is atomic. */
static void growth_refusal(void) {
    char blob[3000];   /* strlen 2999 */
    char filler[1097]; /* strlen 1096 */
    char small[101];   /* strlen 100  */
    string r = string("");
    long seq;
    memset(blob, 'z', sizeof(blob) - 1);
    blob[sizeof(blob) - 1] = '\0';
    memset(filler, 'y', sizeof(filler) - 1);
    filler[sizeof(filler) - 1] = '\0';
    memset(small, 'q', sizeof(small) - 1);
    small[sizeof(small) - 1] = '\0';

    ck(r.reserve(STDLIB_ALLOC_LIMIT) == 0 &&
       r.capacity() == STDLIB_ALLOC_LIMIT, "refusal-reserve");
    ck(r.append(blob) == 0 && r.size() == 2999, "refusal-fill");
    seq = stdlib_alloc_seq();
    ck(r.append(blob) == -1, "refusal-overflow-rc");
    ck(stdlib_alloc_seq() == seq, "refusal-no-allocation");
    ck(r.size() == 2999 && r.capacity() == STDLIB_ALLOC_LIMIT &&
       r.at(2998) == 'z' && r.c_str()[2999] == '\0', "refusal-unchanged");

    /* Fill to the exact hard limit: one more byte must be refused atomically. */
    ck(r.append(filler) == 0 && r.size() == 4095 &&
       r.capacity() == STDLIB_ALLOC_LIMIT, "refusal-exact-fill");
    ck(r.append("z") == -1 && r.size() == 4095 &&
       r.c_str()[4095] == '\0', "refusal-one-past-limit");

    /* Near-limit growth cannot double (3000 * 2 > 4096); adapted upstream
     * "geometric overflow -> settle" allocates exactly the request. */
    string n = string(blob);
    ck(n.append(small) == 0 && n.size() == 3099 && n.capacity() == 3100,
       "near-limit-exact-growth");

    /* Injected allocation failure during growth is failure-atomic. */
    string f = string("abc");
    const char *fold = f.c_str();
    stdlib_alloc_fail_at(1);
    ck(f.append("defgh") == -1, "refusal-allocfail-rc");
    stdlib_alloc_fail_reset();
    ck(f.size() == 3 && f.capacity() == 4 && f.c_str() == fold &&
       strcmp(f.c_str(), "abc") == 0, "refusal-allocfail-atomic");
}

int main(void) {
    stdlib_set_error_callback(NULL);
    stdlib_alloc_fail_reset();
    stdlib_telemetry_reset();

    self_append();
    interior_suffix();
    alias_after_clear();
    growth_refusal();

    print("upstream-string-checks", checks);
    print("upstream-string-failures", failures);
    return failures == 0 ? 0 : 1;
}
