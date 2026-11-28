/* Deterministic regression for C3: string::clear() must invalidate views
 * snapshotted before the clear while the buffer stays owned and reusable.
 *
 * Exact stdout is pinned in string_clear_view_test.expected.  Every check is
 * self-verifying; the process exits non-zero on any failure.  No NULL is
 * passed to a string method (the toolchain's lifetime checker rejects those).
 */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/arena.hp"
#include "stdlib/string.hp"

static int failures = 0;
static int checks = 0;

static void ck(int cond) {
    ++checks;
    if (!cond)
        ++failures;
}

static int alloc_ops = 0;
static int release_ops = 0;

static void *clear_count_alloc(void *ctx, size_t size, size_t align) {
    (void)ctx; (void)align;
    ++alloc_ops;
    return cplus_alloc(size);
}

static void clear_count_release(void *ctx, void *ptr, size_t size) {
    (void)ctx; (void)size;
    ++release_ops;
    cplus_release(ptr);
}

static void clear_no_reset(void *ctx) { (void)ctx; }

static void check_clear_owned(void) {
    string text;
    string_view before;
    string_view after;
    unsigned long long old_token;
    const char *old_ptr;
    size_t cap;

    text = string("secret");
    before = text.to_view();
    old_ptr = before.raw_data(); /* identity only, never dereferenced when stale */
    old_token = stdlib_view_token_of(old_ptr);
    cap = text.capacity();
    ck(before.is_live() == 1 && before.data() == text.c_str());
    ck(before.equals("secret") == 1 && before.is_valid_utf8() == 1);
    print("clear-live", (int)before.is_live(), (int)text.size(), (int)cap);

    text.clear();
    ck(text.valid() == 1 && text.size() == 0u && text.capacity() == cap);
    ck(text.equals("") == 1 && text.c_str()[0] == '\0');
    ck(before.is_live() == 0 && before.data() == (const char *)0);
    ck(before.equals("secret") == 0 && before.is_valid_utf8() == 0);
    ck(stdlib_view_validate(old_ptr, old_token) == 0);
    print("clear-stale", (int)before.is_live(),
          (int)(before.data() == (const char *)0),
          (int)before.equals("secret"),
          (int)stdlib_view_validate(old_ptr, old_token));

    after = text.to_view();
    ck(after.is_live() == 1 && after.data() == text.c_str());
    ck(after.size() == 0u && after.equals("") == 1 &&
       after.is_valid_utf8() == 1);
    print("clear-fresh", (int)after.is_live(), (int)after.size(),
          (int)after.equals(""), (int)after.is_valid_utf8());

    /* The cleared buffer is reused in place; the stale view stays stale and
     * the fresh empty-window view keeps its size snapshot. */
    ck(text.append("new") == 0 && text.equals("new") == 1);
    ck(before.is_live() == 0 && before.equals("secret") == 0);
    ck(after.is_live() == 1 && after.size() == 0u && after.equals("") == 1);
    print("clear-reuse", (int)before.is_live(), (int)text.size(),
          (int)after.size(), (int)after.is_live());
}

static void check_clear_repeat(void) {
    string repeated;
    string_view first;
    string_view second;

    repeated = string("abc");
    first = repeated.to_view();
    repeated.clear();
    ck(first.is_live() == 0);
    repeated.append("xy");
    second = repeated.to_view();
    repeated.clear();
    ck(first.is_live() == 0 && second.is_live() == 0);
    ck(second.data() == (const char *)0 && repeated.size() == 0u);
    print("clear-repeat", (int)first.is_live(), (int)second.is_live(),
          (int)repeated.size());
}

static void check_clear_allocator(void) {
    error clear_error = error();
    stdlib_allocator custom;
    string bound;
    string_view bound_view;
    stdlib_telemetry_stats before_stats;
    stdlib_telemetry_stats after_stats;
    int alloc_before;
    int release_before;
    int heap_delta;

    custom.ctx = (void *)0;
    custom.alloc = clear_count_alloc;
    custom.realloc = (void *)0;
    custom.release = clear_count_release;
    custom.reset = clear_no_reset;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_telemetry_snapshot(&before_stats);
    alloc_before = alloc_ops;
    release_before = release_ops;

    bound = string_in(&custom, "abcd");
    bound_view = bound.to_view();
    ck(bound_view.is_live() == 1);
    bound.clear();
    ck(bound_view.is_live() == 0 && bound.valid() == 1);
    ck(alloc_ops == alloc_before + 1 && release_ops == release_before);
    ck(bound.append("ef") == 0 && bound.equals("ef") == 1);
    ck(alloc_ops == alloc_before + 1); /* in-place, no reallocation */
    bound.drop();
    ck(release_ops == release_before + 1);

    stdlib_telemetry_snapshot(&after_stats);
    heap_delta = (int)(after_stats.alloc_count - before_stats.alloc_count);
    ck(heap_delta == 0);
    print("clear-alloc", heap_delta, alloc_ops - alloc_before,
          release_ops - release_before, (int)bound.valid());
    stdlib_telemetry_set_enabled(0);
    (void)clear_error;
}

static void check_clear_inert(void) {
    string dropped;
    string_view dropped_view;

    dropped = string("gone");
    dropped_view = dropped.to_view();
    dropped.drop();
    dropped.clear();
    dropped.clear(); /* repeated clear on an invalid string stays inert */
    ck(dropped.valid() == 0 && dropped.size() == 0u);
    ck(dropped_view.is_live() == 0 &&
       dropped_view.data() == (const char *)0);
    print("clear-inert", (int)dropped.valid(), (int)dropped_view.is_live());
}

int main(void) {
    stdlib_set_error_callback((stdlib_error_callback)0);
    check_clear_owned();
    check_clear_repeat();
    check_clear_allocator();
    check_clear_inert();
    print("string-clear-view", checks, failures);
    return failures == 0 ? 0 : 1;
}
