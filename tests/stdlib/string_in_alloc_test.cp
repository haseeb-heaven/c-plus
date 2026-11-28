/* Regression for issue #77: string_in() must never touch the default checked
 * heap, leak a placeholder allocation, or pair a heap pointer with a custom
 * allocator.  The counters below observe both the checked-heap telemetry and
 * the custom allocator's own alloc/release call counts. */
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/arena.hp"
#include "stdlib/string.hp"

static int g_alloc_ops;
static int g_release_ops;

static void *counting_alloc(void *ctx, size_t size, size_t align) {
    (void)ctx;
    (void)align;
    ++g_alloc_ops;
    return cplus_alloc(size);
}

static void counting_release(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)size;
    ++g_release_ops;
    cplus_release(ptr);
}

static void *refusing_alloc(void *ctx, size_t size, size_t align) {
    (void)ctx;
    (void)size;
    (void)align;
    ++g_alloc_ops;
    return (void *)0;
}

static void counting_noop_release(void *ctx, void *ptr, size_t size) {
    (void)ctx;
    (void)ptr;
    (void)size;
    ++g_release_ops; /* would be a foreign-pointer release on the old code */
}

static void no_reset(void *ctx) {
    (void)ctx;
}

int main(void) {
    error e = error();
    stdlib_allocator custom;
    stdlib_allocator refusing;
    stdlib_arena arena;
    stdlib_telemetry_stats before;
    stdlib_telemetry_stats after;
    int failures = 0;
    int i;

    custom.ctx = (void *)0;
    custom.alloc = counting_alloc;
    custom.realloc = (void *)0;
    custom.release = counting_release;
    custom.reset = no_reset;

    refusing.ctx = (void *)0;
    refusing.alloc = refusing_alloc;
    refusing.realloc = (void *)0;
    refusing.release = counting_noop_release;
    refusing.reset = no_reset;

    stdlib_telemetry_set_enabled(1);
    stdlib_telemetry_reset();
    stdlib_telemetry_snapshot(&before);

    /* Many calls: every byte must come from `custom`, and every drop must go
     * back to `custom`.  The old string("") placeholder leaked one checked
     * heap byte per call and appeared in alloc_count/bytes_allocated. */
    g_alloc_ops = 0;
    g_release_ops = 0;
    for (i = 0; i < 32; ++i) {
        string s = string_in(&custom, "hello world");
        if (!s.valid() || !s.equals("hello world") || s.size() != 11u)
            ++failures;
    }
    stdlib_telemetry_snapshot(&after);
    if (after.alloc_count - before.alloc_count != 0ULL ||
        after.free_count - before.free_count != 0ULL ||
        after.bytes_allocated - before.bytes_allocated != 0ULL) {
        ++failures;
    }
    if (g_alloc_ops != 32 || g_release_ops != 32)
        ++failures;
    print("string_in-custom", g_alloc_ops, g_release_ops,
          (int)(after.alloc_count - before.alloc_count));

    /* Arena-backed construction: arena setup is outside the measured window,
     * the factory itself must not fall back to the checked heap. */
    if (stdlib_arena_init(&arena, 4096, &e) != STDLIB_OK)
        ++failures;
    {
        stdlib_allocator a = stdlib_arena_as_allocator(&arena);
        stdlib_telemetry_snapshot(&before);
        for (i = 0; i < 16; ++i) {
            string s = string_in(&a, "arena");
            if (!s.valid() || !s.equals("arena") || s.size() != 5u)
                ++failures;
        }
        stdlib_telemetry_snapshot(&after);
        if (after.alloc_count - before.alloc_count != 0ULL)
            ++failures;
        print("string_in-arena", (int)(after.alloc_count - before.alloc_count));
    }

    /* Allocation refusal yields an invalid empty string, never a heap pointer
     * owned by a foreign allocator; drop() must not call the custom release. */
    g_alloc_ops = 0;
    g_release_ops = 0;
    stdlib_telemetry_snapshot(&before);
    {
        string bad = string_in(&refusing, "x");
        if (bad.valid() || bad.size() != 0u || strcmp(bad.c_str(), "") != 0)
            ++failures;
        bad.drop();
    }
    stdlib_telemetry_snapshot(&after);
    if (g_alloc_ops != 1 || g_release_ops != 0 ||
        after.alloc_count - before.alloc_count != 0ULL)
        ++failures;
    print("string_in-refused", g_alloc_ops, g_release_ops,
          (int)(after.alloc_count - before.alloc_count));

    /* An empty text still allocates exactly once through `a` (no placeholder
     * heap byte) and releases exactly once. */
    g_alloc_ops = 0;
    g_release_ops = 0;
    {
        string empty = string_in(&custom, "");
        if (!empty.valid() || empty.size() != 0u || !empty.equals(""))
            ++failures;
    }
    if (g_alloc_ops != 1 || g_release_ops != 1)
        ++failures;
    print("string_in-empty-text", g_alloc_ops, g_release_ops);

    stdlib_telemetry_set_enabled(0);
    print("string_in-alloc-regression", failures == 0 ? 1 : 0);
    return failures == 0 ? 0 : 1;
}
