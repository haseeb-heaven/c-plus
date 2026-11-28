/* Deep borrowed-view contract tests for stdlib/string.hp string_view.
 *
 * Independent from deep_string_append.cp.  Three clearly separated sections:
 *   SUCCESS     - view of a live owner, fixed-window semantics, UTF-8 checks
 *   RECOVERABLE - null arguments, zero-init views, and invalidation detection
 *                 through the allocator's metadata (no stale dereference)
 *   FATAL       - runtime fatal exit code when a stale view pointer is passed
 *                 to cplus_check_live (verified in a forked child)
 *
 * A string_view is a borrowed window: its `data()` pointer and its `size()`
 * are a snapshot taken at to_view() time.  In-place appends keep the pointer
 * valid but do not grow the view; a reallocating append or drop() invalidates
 * the pointer.  Dereferencing a stale view is a contract violation (UB) and is
 * deliberately never done in this test.
 *
 * No .expected file is used on purpose: every check is self-verifying and the
 * exact stdout is recorded in docs/stdlib/DEEP_STRING_CONTRACT.md.
 */
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

/* ------------------------------------------------------------------ SUCCESS */
static void success(void) {
    string s;
    string empty;
    string utf8;
    string badutf8;
    string_view v;
    string_view ve;
    string_view vu;
    string_view vb;
    string_view direct;
    const char *buf;

    /* View of a live string: size, pointer identity, content, UTF-8. */
    s = string("hello");
    v = s.to_view();
    ck(v.size() == 5, "view-size");
    ck(v.data() == s.c_str(), "view-data-identity");
    ck(strcmp(v.data(), "hello") == 0, "view-data-content");
    ck(v.equals("hello") == 1, "view-equals-exact");
    ck(v.equals("hell") == 0 && v.equals("hello!") == 0,
       "view-equals-length-mismatch");
    ck(v.equals("") == 0, "view-equals-empty");
    ck(v.is_valid_utf8() == 1, "view-utf8-ascii");

    /* Direct construction with an explicit length (no owner involved). */
    direct = string_view("abcXYZ", 3);
    ck(direct.size() == 3, "view-direct-size");
    ck(direct.equals("abc") == 1 && direct.equals("abcXYZ") == 0,
       "view-direct-equals");
    ck(direct.is_valid_utf8() == 1, "view-direct-utf8");

    /* Empty string view: size 0, equals(""), valid UTF-8. */
    empty = string("");
    ve = empty.to_view();
    ck(ve.size() == 0, "view-empty-size");
    ck(ve.data() == empty.c_str() && ve.data() != (const char *)0,
       "view-empty-data");
    ck(ve.equals("") == 1, "view-empty-equals");
    ck(ve.is_valid_utf8() == 1, "view-empty-utf8");

    /* UTF-8 validation over a borrowed window. */
    utf8 = string("\xc3\xa9\xe2\x82\xac");      /* é, € */
    vu = utf8.to_view();
    ck(vu.size() == 5, "view-utf8-multibyte-size");
    ck(vu.is_valid_utf8() == 1, "view-utf8-multibyte-valid");

    /* Invalid UTF-8 over a borrowed window: stray 0xFF byte. */
    badutf8 = string("\xc3\x28");
    vb = badutf8.to_view();
    ck(vb.size() == 2, "view-utf8-bad-size");
    ck(vb.is_valid_utf8() == 0, "view-utf8-bad-detected");

    /* UTF-8 validator boundaries over borrowed windows (RFC 3629). */
    {
        string_view v_overlong = string_view("\xc0\x80", 2);
        string_view v_surrogate = string_view("\xed\xa0\x80", 3);
        string_view v_too_big = string_view("\xf4\x90\x80\x80", 4);
        string_view v_truncated = string_view("\xe2\x82", 2);
        string_view v_continuation = string_view("\x80", 1);
        string_view v_nul = string_view("a\0b", 3);
        string_view v_max = string_view("\xf4\x8f\xbf\xbf", 4);
        ck(v_overlong.is_valid_utf8() == 0, "view-utf8-overlong");
        ck(v_surrogate.is_valid_utf8() == 0, "view-utf8-surrogate");
        ck(v_too_big.is_valid_utf8() == 0, "view-utf8-above-max");
        ck(v_truncated.is_valid_utf8() == 0, "view-utf8-truncated");
        ck(v_continuation.is_valid_utf8() == 0, "view-utf8-stray-cont");
        ck(v_nul.is_valid_utf8() == 1, "view-utf8-embedded-nul-valid");
        ck(v_max.is_valid_utf8() == 1, "view-utf8-u10ffff-valid");
        /* equals() takes a NUL-terminated argument, so an embedded NUL in the
         * window can never compare equal to the full 3-byte window. */
        ck(v_nul.equals("a\0b") == 0, "view-embedded-nul-equals");
    }

    /* Fixed-window semantics: in-place append keeps the pointer valid.
     * The view's size is a snapshot and must not change. */
    s = string("abc");
    ck(s.reserve(32) == 0, "view-fixed-reserve");
    v = s.to_view();
    buf = v.data();
    ck(s.append("de") == 0, "view-fixed-append-rc");
    ck(v.data() == buf && v.data() == s.c_str(), "view-fixed-pointer-stable");
    ck(v.size() == 3 && s.size() == 5, "view-fixed-size-snapshot");
    ck(v.equals("abc") == 1, "view-fixed-window-content");
    ck(v.equals("abcde") == 0, "view-fixed-window-not-grown");
}

/* --------------------------------------------------------------- RECOVERABLE */
static void recoverable(void) {
    string s;
    string bad;
    string_view v;
    string_view zero;
    string_view null_with_len;
    const char *old_buf;
    unsigned long long gen;
    const char *after;

    /* NULL and degenerate views are inert, never fatal. */
    zero = string_view(NULL, 0);   /* zero-window via the public constructor */
    ck(zero.size() == 0, "view-zero-size");
    ck(zero.data() == (const char *)0, "view-zero-data");
    ck(zero.equals("") == 1 && zero.equals("x") == 0,
       "view-zero-equals");
    ck(zero.equals(NULL) == 0, "view-zero-equals-null");
    ck(zero.is_valid_utf8() == 1, "view-zero-utf8");

    null_with_len = string_view(NULL, 3);
    ck(null_with_len.size() == 3, "view-null-len-size");
    ck(null_with_len.equals("abc") == 0, "view-null-len-equals");
    ck(null_with_len.equals(NULL) == 0, "view-null-len-equals-null");
    ck(null_with_len.is_valid_utf8() == 0,
       "view-null-len-utf8-rejected");

    /* NULL argument on a populated view. */
    s = string("abc");
    v = s.to_view();
    ck(v.equals(NULL) == 0, "view-populated-equals-null");

    /* View of an invalid string (failed constructor) is an empty window. */
    stdlib_alloc_fail_at(1);
    bad = string("abc");
    stdlib_alloc_fail_reset();
    {
        string_view vb = bad.to_view();
        ck(bad.valid() == 0 && vb.size() == 0, "view-bad-owner-size");
        ck(vb.data() == (const char *)0, "view-bad-owner-data-null");
        ck(vb.equals("") == 1 && vb.is_valid_utf8() == 1,
           "view-bad-owner-empty");
    }

    /* Invalidation by a reallocating append is detectable via metadata.
     * The stale view pointer is never dereferenced. */
    s = string("short");
    ck(s.reserve(8) == 0 && s.capacity() == 8, "inval-setup-reserve");
    v = s.to_view();
    old_buf = v.data();
    gen = stdlib_alloc_generation(old_buf);
    ck(gen != 0ULL, "inval-old-buffer-tracked");
    ck(s.append("-a-much-longer-suffix") == 0, "inval-realloc-append-rc");
    after = s.c_str();
    ck(after != old_buf, "inval-buffer-moved");
    /* Generation-checked views now fail closed after owner reallocation. */
    ck(v.data() == (const char *)0, "inval-view-refuses-stale");
    ck(stdlib_alloc_check_generation(old_buf, gen) == 0,
       "inval-old-generation-stale");
    ck(stdlib_alloc_generation(after) != 0ULL &&
       stdlib_alloc_generation(after) != gen,
       "inval-new-generation");

    /* Invalidation by drop(): old buffer is no longer a live allocation. */
    s = string("dropme");
    v = s.to_view();
    old_buf = v.data();
    gen = stdlib_alloc_generation(old_buf);
    ck(gen != 0ULL, "inval-drop-old-tracked");
    s.drop();
    ck(s.valid() == 0 && s.c_str()[0] == '\0', "inval-drop-owner-empty");
    ck(stdlib_alloc_check_generation(old_buf, gen) == 0,
       "inval-drop-generation-stale");
    ck(v.data() == (const char *)0, "inval-drop-view-refuses-stale");
    /* Never call v.equals(...) / v.is_valid_utf8() on old_buf here. */
}

/* --------------------------------------------------------------------- FATAL */
static void child_stale_view_check(void) {
    string s = string("live");
    string_view v = s.to_view();
    const char *stale = v.data();
    s.drop();                               /* invalidates the borrowed pointer */
    cplus_check_live(stale);                 /* fatal: no longer a live block */
}

static void fatal(void) {
    pid_t pid = fork();
    if (pid == 0) {
        child_stale_view_check();
        _Exit(0);                            /* reached only if no fatal */
    }
    {
        int st = 0;
        waitpid(pid, &st, 0);
        ck(WIFEXITED(st) && WEXITSTATUS(st) == (int)CPLUS_EXIT_TYPE_CONFUSION,
           "fatal-stale-view-exit");
    }
}

int main(void) {
    success();
    recoverable();
    fatal();
    print("view-success", 1);
    print("view-recoverable", 1);
    print("view-fatal", 1);
    print("view-checks", checks);
    print("view-failures", failures);
    return failures == 0 ? 0 : 1;
}
