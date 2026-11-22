/* Deep append/reserve contract tests for stdlib/string.hp.
 *
 * Independent from deep_string_view.cp.  Three clearly separated sections:
 *   SUCCESS     - well-defined operations, exact contents/capacity checked
 *   RECOVERABLE - invalid or failing operations return -1/0 and are reported
 *   FATAL       - runtime fatal exit codes for deliberately broken raw
 *                 operations that string.hp's own guards prevent (verified in
 *                 a forked child so the parent stays alive).
 *
 * STDLIB_ALLOC_LIMIT is deliberately reduced to 4096 for this TU so the
 * overflow branches of append/reserve can be reached with small, fast inputs.
 * This is the documented allocator override, not a change to string.hp.
 *
 * No .expected file is used on purpose: every check is self-verifying and the
 * exact stdout is recorded in docs/stdlib/DEEP_STRING_CONTRACT.md.
 */
#define STDLIB_ALLOC_LIMIT ((size_t)4096)
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

static int failures = 0;
static int checks = 0;

static void ck(int cond, const char *label) {
    checks++;
    if (!cond) {
        failures++;
        print("FAIL", label);
    }
}

/* Error-callback counters for the RECOVERABLE section. */
static int cb_invalid = 0;
static int cb_bounds = 0;
static int cb_memory = 0;
static int cb_total = 0;
static int cb_last_code = -1;
static int cb_last_category = -1;

static void note_cb(int category, cplus_exit_code code, const char *message) {
    (void)message;
    cb_total++;
    cb_last_code = (int)code;
    cb_last_category = category;
    if (category == STDLIB_CATEGORY_INVALID)
        cb_invalid++;
    else if (category == STDLIB_CATEGORY_BOUNDS)
        cb_bounds++;
    else if (category == STDLIB_CATEGORY_MEMORY)
        cb_memory++;
}

static void cb_reset(void) {
    cb_invalid = 0;
    cb_bounds = 0;
    cb_memory = 0;
    cb_total = 0;
    cb_last_code = -1;
    cb_last_category = -1;
}

/* ------------------------------------------------------------------ SUCCESS */
static void success(void) {
    string a;
    string e;
    string u;
    string v;
    string s;
    string g;
    string p;
    string q;
    string r;
    string b;
    string d;
    string src;
    string dst;
    size_t i;
    int all_x;

    /* NULL constructor argument is documented as empty. */
    a = string(NULL);
    ck(a.valid() == 1, "ctor-null-valid");
    ck(a.size() == 0 && a.capacity() == 1, "ctor-null-size-cap");
    ck(strcmp(a.c_str(), "") == 0, "ctor-null-cstr");

    /* Empty string and empty append: no growth, no allocation. */
    e = string("");
    ck(e.valid() == 1 && e.size() == 0 && e.capacity() == 1,
       "empty-ctor");
    ck(e.append("") == 0 && e.size() == 0 && e.capacity() == 1,
       "empty-append-empty");

    /* Unrelated strings, growth by doubling clamped to exact need. */
    u = string("ab");                       /* count 2, space 3 */
    ck(u.append("cdef") == 0, "unrelated-append-rc");
    ck(u.size() == 6 && u.capacity() == 7, "unrelated-append-size-cap");
    ck(strcmp(u.c_str(), "abcdef") == 0, "unrelated-append-content");

    /* Append that fits in spare capacity must not reallocate. */
    v = string("abcdef");
    ck(v.reserve(20) == 0 && v.capacity() == 20, "spare-reserve");
    {
        const char *before = v.c_str();
        ck(v.append("gh") == 0 && v.size() == 8 && v.capacity() == 20,
           "spare-append-size-cap");
        ck(v.c_str() == before, "spare-append-no-realloc");
        ck(strcmp(v.c_str(), "abcdefgh") == 0, "spare-append-content");
    }

    /* Full self-append across a reallocation. */
    s = string("hello");                    /* count 5, space 6 */
    ck(s.append(s.c_str()) == 0, "self-append-rc");
    ck(s.size() == 10 && s.capacity() == 12, "self-append-size-cap");
    ck(strcmp(s.c_str(), "hellohello") == 0, "self-append-content");

    /* Repeated self-append: exponential growth stays correct. */
    g = string("x");
    all_x = 1;
    for (i = 0; i < 6; i++) {
        if (g.append(g.c_str()) != 0)
            all_x = 0;
    }
    ck(all_x == 1, "self-append-repeat-rc");
    ck(g.size() == 64, "self-append-repeat-size");
    for (i = 0; i < g.size(); i++)
        if (g.at(i) != 'x')
            all_x = 0;
    ck(all_x == 1, "self-append-repeat-content");

    /* Interior-pointer append across a reallocation (the aliasing path). */
    p = string("abcdef");                   /* count 6, space 7 */
    ck(p.append(p.c_str() + 3) == 0, "interior-realloc-rc");
    ck(p.size() == 9 && p.capacity() == 14, "interior-realloc-size-cap");
    ck(strcmp(p.c_str(), "abcdefdef") == 0, "interior-realloc-content");

    /* Interior-pointer append with spare capacity (no reallocation). */
    q = string("0123456789");
    ck(q.reserve(40) == 0, "interior-norealloc-reserve");
    {
        const char *before = q.c_str();
        ck(q.append(q.c_str() + 7) == 0, "interior-norealloc-rc");
        ck(q.c_str() == before, "interior-norealloc-no-realloc");
        ck(q.size() == 13 && strcmp(q.c_str(), "0123456789789") == 0,
           "interior-norealloc-content");
    }

    /* Interior pointer at the last character (offset == count - 1). */
    r = string("abcde");
    ck(r.append(r.c_str() + 4) == 0, "interior-last-rc");
    ck(strcmp(r.c_str(), "abcdee") == 0, "interior-last-content");
    /* Appending the terminator is an empty append: no change. */
    ck(r.append(r.c_str() + r.size()) == 0, "interior-empty-rc");
    ck(strcmp(r.c_str(), "abcdee") == 0, "interior-empty-content");

    /* reserve boundaries: no-op below/at capacity, growth preserves data. */
    b = string("xy");                       /* space 3 */
    ck(b.reserve(3) == 0 && b.capacity() == 3, "reserve-at-capacity");
    ck(b.reserve(2) == 0 && b.capacity() == 3, "reserve-below-capacity");
    ck(strcmp(b.c_str(), "xy") == 0, "reserve-noop-content");
    ck(b.reserve(64) == 0 && b.capacity() == 64, "reserve-grow-cap");
    ck(strcmp(b.c_str(), "xy") == 0, "reserve-grow-content");

    /* Operations after drop: empty view of state, then full re-initialise. */
    d = string("transient");
    d.drop();
    ck(d.valid() == 0 && d.size() == 0 && d.capacity() == 0, "drop-state");
    ck(strcmp(d.c_str(), "") == 0, "drop-cstr");
    ck(d.equals("") == 1 && d.equals("x") == 0, "drop-equals");
    ck(d.append("fresh") == 0 && d.valid() == 1,
       "drop-append-reinit-rc");
    ck(strcmp(d.c_str(), "fresh") == 0 && d.size() == 5,
       "drop-append-reinit-content");
    d.drop();
    d.drop();                                /* double drop is inert */
    ck(d.valid() == 0, "double-drop-inert");
    ck(d.reserve(8) == 0 && d.valid() == 1 && d.capacity() == 8,
       "drop-reserve-reinit");
    ck(strcmp(d.c_str(), "") == 0, "drop-reserve-content");
    /* reserve(0) is a legitimate no-op: it needs no buffer, so an invalid
     * string stays invalid until a non-zero reserve/append/copy_from. */
    d.drop();
    ck(d.reserve(0) == 0 && d.valid() == 0 && d.capacity() == 0,
       "drop-reserve-zero-noop");
    ck(d.reserve(1) == 0 && d.valid() == 1 && d.capacity() == 1,
       "drop-reserve-one-reinit");

    /* copy_from remains failure-atomic and handles self-copy. */
    src = string("payload");
    dst = string("old");
    ck(dst.copy_from(&src) == 0, "copy-from-rc");
    ck(strcmp(dst.c_str(), "payload") == 0, "copy-from-content");
    ck(dst.copy_from(&dst) == 0, "copy-self-rc");
    ck(strcmp(dst.c_str(), "payload") == 0, "copy-self-content");
}

/* --------------------------------------------------------------- RECOVERABLE */
static void recoverable(void) {
    string s;
    string bad;
    string big;
    string huge;
    char src[3000];
    int rc;
    const char *saved;

    stdlib_set_error_callback(note_cb);
    cb_reset();

    s = string("stable");

    /* NULL method arguments. */
    saved = s.c_str();
    rc = s.append(NULL);
    cb_reset(); /* reset again so per-case counters are exact below */
    ck(rc == -1, "append-null-rc");
    ck(s.size() == 6 && s.c_str() == saved && strcmp(s.c_str(), "stable") == 0,
       "append-null-unchanged");

    cb_reset();
    ck(s.equals(NULL) == 0, "equals-null");
    ck(cb_total == 1 && cb_invalid == 1 &&
       cb_last_code == (int)CPLUS_EXIT_UNSUPPORTED, "equals-null-note");

    cb_reset();
    ck(s.compare(NULL) == 1, "compare-null");
    ck(cb_total == 1 && cb_invalid == 1 &&
       cb_last_code == (int)CPLUS_EXIT_UNSUPPORTED, "compare-null-note");

    cb_reset();
    ck(s.find(NULL) == -1, "find-null");
    ck(cb_total == 0, "find-null-no-note");

    cb_reset();
    ck(s.copy_from(NULL) == -1, "copy-from-null");
    ck(cb_total == 1 && cb_invalid == 1 &&
       cb_last_code == (int)CPLUS_EXIT_UNSUPPORTED, "copy-from-null-note");

    {
        char out[8];
        cb_reset();
        ck(s.substr(0, 2, NULL, sizeof(out)) == -1, "substr-null-out");
        ck(s.field(',', 0, NULL, sizeof(out)) == -1, "field-null-out");
        ck(cb_total == 0, "substr-field-null-no-note");
    }

    /* Reserve over the configured limit: bounds note, no allocation. */
    cb_reset();
    rc = s.reserve(STDLIB_ALLOC_LIMIT + 1);
    ck(rc == -1, "reserve-over-limit-rc");
    ck(s.size() == 6 && strcmp(s.c_str(), "stable") == 0 &&
       s.capacity() == 7, "reserve-over-limit-unchanged");
    ck(cb_total == 1 && cb_last_category == STDLIB_CATEGORY_RANGE &&
       cb_last_code == (int)CPLUS_EXIT_BOUNDS, "reserve-over-limit-note");

    /* append overflow: count + extra + 1 exceeds STDLIB_ALLOC_LIMIT. */
    memset(src, 'b', sizeof(src));
    src[sizeof(src) - 1] = '\0';
    big = string(src);                      /* count 2999, fits in 4096 */
    ck(big.valid() == 1 && big.size() == 2999, "overflow-source-built");
    cb_reset();
    rc = big.append(src);                   /* 2999 + 2999 + 1 > 4096 */
    ck(rc == -1, "append-overflow-rc");
    ck(big.size() == 2999 && big.at(0) == 'b' &&
       big.at(2998) == 'b', "append-overflow-unchanged");
    ck(cb_total == 1 && cb_last_category == STDLIB_CATEGORY_RANGE &&
       cb_last_code == (int)CPLUS_EXIT_BOUNDS, "append-overflow-note");

    /* Deterministic alloc-fail injection: append is failure-atomic. */
    cb_reset();
    saved = s.c_str();
    stdlib_alloc_fail_at(1);
    rc = s.append("more");
    stdlib_alloc_fail_reset();
    ck(rc == -1, "append-allocfail-rc");
    ck(s.size() == 6 && s.c_str() == saved && strcmp(s.c_str(), "stable") == 0,
       "append-allocfail-atomic");
    ck(cb_total == 1 && cb_memory == 1 &&
       cb_last_code == (int)CPLUS_EXIT_RESOURCE, "append-allocfail-note");

    /* append uses exactly one allocation: fail_at(2) must still succeed. */
    cb_reset();
    stdlib_alloc_fail_at(2);
    rc = s.append("ok");
    stdlib_alloc_fail_reset();
    ck(rc == 0 && strcmp(s.c_str(), "stableok") == 0, "append-one-alloc");

    /* Deterministic alloc-fail injection: reserve is failure-atomic. */
    cb_reset();
    {
        size_t cap = s.capacity();
        saved = s.c_str();
        stdlib_alloc_fail_at(1);
        rc = s.reserve(cap + 100);
        stdlib_alloc_fail_reset();
        ck(rc == -1, "reserve-allocfail-rc");
        ck(s.capacity() == cap && s.c_str() == saved &&
           strcmp(s.c_str(), "stableok") == 0, "reserve-allocfail-atomic");
        ck(cb_total == 1 && cb_memory == 1 &&
           cb_last_code == (int)CPLUS_EXIT_RESOURCE, "reserve-allocfail-note");
    }

    /* Failed constructor produces an invalid string that re-initialises. */
    cb_reset();
    stdlib_alloc_fail_at(1);
    bad = string("abc");
    stdlib_alloc_fail_reset();
    ck(bad.valid() == 0 && bad.size() == 0 && bad.capacity() == 0,
       "ctor-allocfail-invalid");
    ck(strcmp(bad.c_str(), "") == 0 && bad.equals("") == 1 &&
       bad.at(0) == '\0', "ctor-allocfail-empty-reads");
    ck(bad.append("ok") == 0 && bad.valid() == 1 &&
       strcmp(bad.c_str(), "ok") == 0, "ctor-allocfail-reinit");

    /* fail_from: every later allocation fails until reset. */
    cb_reset();
    huge = string("z");
    stdlib_alloc_fail_after(0);              /* all subsequent allocs fail */
    rc = huge.append("zzzz");
    stdlib_alloc_fail_reset();
    ck(rc == -1 && strcmp(huge.c_str(), "z") == 0, "fail-from-atomic");

    stdlib_set_error_callback(NULL);
}

/* --------------------------------------------------------------------- FATAL */
/* Helpers run in a forked child; the parent records the exact exit status. */
typedef void (*release_fn)(void *);

static void child_double_release(void) {
    void *p = stdlib_alloc(8);
    /* Indirect call: the deliberate second release is a runtime fatal-path
     * probe, not a code path the C+ owner analyser should accept. */
    release_fn rel = stdlib_release;
    rel(p);
    rel(p);                                 /* fatal: double free */
}

static void child_stale_view_use(void) {
    string s = string("data");
    const char *stale = s.c_str();
    s.drop();
    cplus_check_live(stale);                 /* fatal: not a live block */
}

static void fatal(void) {
    pid_t pid;

    pid = fork();
    if (pid == 0) {
        child_double_release();
        _Exit(0);                            /* reached only if no fatal */
    }
    {
        int st = 0;
        waitpid(pid, &st, 0);
        ck(WIFEXITED(st) && WEXITSTATUS(st) == (int)CPLUS_EXIT_DOUBLE_FREE,
           "fatal-double-free-exit");
    }

    pid = fork();
    if (pid == 0) {
        child_stale_view_use();
        _Exit(0);
    }
    {
        int st = 0;
        waitpid(pid, &st, 0);
        ck(WIFEXITED(st) && WEXITSTATUS(st) == (int)CPLUS_EXIT_TYPE_CONFUSION,
           "fatal-stale-buffer-exit");
    }
}

int main(void) {
    success();
    recoverable();
    fatal();
    print("append-success", 1);
    print("append-recoverable", 1);
    print("append-fatal", 1);
    print("append-checks", checks);
    print("append-failures", failures);
    return failures == 0 ? 0 : 1;
}
