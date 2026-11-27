/* White-box regression for issue #191's stream_reserve() growth policy.
 * The translation unit includes safe_io.c so the file-local table helpers are
 * visible; the five external dependencies are replaced by inert stubs and the
 * normal SafeCRT archive still links (its safe_io member is never pulled in). */
#define CPLUS_SAFE_DIAG_NO_MACROS
#include "../../../stdlib/safe/src/safe_io.c"
#include <assert.h>

void cplus_safe_log(cplus_safe_log_level level, const char *api, const char *msg) {
    (void)level; (void)api; (void)msg;
}
void cplus_safe_stat_record(const char *api, int ok) { (void)api; (void)ok; }
void cplus_runtime_violation(const char *api, const char *detail) { (void)api; (void)detail; }
int cplus_safe_ptr_was_freed(const void *ptr) { (void)ptr; return 0; }
size_t cplus_safe_remaining_capacity(const void *ptr) { (void)ptr; return (size_t)-1; }
size_t cplus_safe_ptr_size(const void *ptr) { (void)ptr; return 0; }

int main(void) {
    stream_table t;
    FILE dummy;
    FILE *p = &dummy;

    /* First allocation still uses the initial slot count. */
    memset(&t, 0, sizeof(t));
    assert(stream_reserve(&t) == 1);
    assert(t.cap == STREAM_CACHE_INITIAL_SLOTS);
    assert(t.slots != NULL);
    free(t.slots);

    /* One live entry + 6 tombstones in an 8-slot table: load 7/8 forces a
     * reserve.  Compaction at the existing capacity is sufficient and MUST
     * not double the table (the tombstone-churn OOM in issue #191). */
    memset(&t, 0, sizeof(t));
    t.cap = 8u;
    t.slots = (FILE **)calloc(t.cap, sizeof(*t.slots));
    assert(t.slots != NULL);
    t.slots[0] = p;
    t.count = 1u;
    t.tombstones = 6u;
    assert(stream_reserve(&t) == 1);
    assert(t.cap == 8u);
    assert(t.count == 1u && t.tombstones == 0u);
    assert(stream_find(&t, p) < t.cap);
    free(t.slots);

    /* A genuinely dense live table must still grow. */
    memset(&t, 0, sizeof(t));
    t.cap = 8u;
    t.slots = (FILE **)calloc(t.cap, sizeof(*t.slots));
    assert(t.slots != NULL);
    t.slots[0] = p;
    t.count = 7u;
    t.tombstones = 1u;
    assert(stream_reserve(&t) == 1);
    assert(t.cap == 16u);
    assert(t.count == 7u && t.tombstones == 0u);
    assert(stream_find(&t, p) < t.cap);
    free(t.slots);

    puts("issue191_stream_reserve_compaction: ok");
    return 0;
}
