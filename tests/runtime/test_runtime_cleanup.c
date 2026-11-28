#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_runtime.h"

static char log_order[32];
static size_t log_len = 0;
static int live_cnt = 0;

typedef struct tracked_res {
    char tag;
    void *ptr;
} tracked_res;

static void drop_res(void *arg) {
    tracked_res *r = (tracked_res *)arg;
    if (log_len + 1 < sizeof(log_order)) {
        log_order[log_len++] = r->tag;
        log_order[log_len] = '\0';
    }
    if (r->ptr) {
        free(r->ptr);
        r->ptr = NULL;
        --live_cnt;
    }
}

static int guarded_inner_unwind(void *ctx) {
    volatile int arr[2] = {0, 0};
    volatile int oob = 4;
    tracked_res *pair = (tracked_res *)ctx;
    assert(cplus_runtime_push_cleanup(drop_res, &pair[2]) == 0);
    assert(cplus_runtime_push_cleanup(drop_res, &pair[3]) == 0);
    arr[oob] = 1; /* UBSan bounds trap -> __ubsan_on_report -> longjmp */
    return 0;
}

static int guarded_outer_unwind(void *ctx) {
    tracked_res *pair = (tracked_res *)ctx;
    int inner_rc;
    assert(cplus_runtime_push_cleanup(drop_res, &pair[0]) == 0);
    assert(cplus_runtime_push_cleanup(drop_res, &pair[1]) == 0);
    inner_rc = cplus_runtime_guard(guarded_inner_unwind, ctx);
    assert(inner_rc == CPLUS_RUNTIME_ERROR);
    /* Inner cleanups D, C were unwound; outer cleanups A, B are still intact
     * until popped explicitly or unwound on exit. */
    assert(strcmp(log_order, "DC") == 0);
    cplus_runtime_pop_cleanup(1); /* B */
    cplus_runtime_pop_cleanup(1); /* A */
    return 0;
}

static int guarded_leaked_cleanup(void *ctx) {
    tracked_res *r = (tracked_res *)ctx;
    assert(cplus_runtime_push_cleanup(drop_res, r) == 0);
    return 0; /* forgot to pop before normal return */
}

int main(void) {
    tracked_res res[4];
    int mark;
    int i;

    cplus_runtime_set_handler(NULL);
    cplus_runtime_set_failstop(0);
    cplus_runtime_reset_counters();

    /* 1. Manual mark / unwind_to LIFO order */
    log_len = 0;
    log_order[0] = '\0';
    for (i = 0; i < 4; ++i) {
        res[i].tag = (char)('A' + i);
        res[i].ptr = malloc(16);
        assert(res[i].ptr != NULL);
        ++live_cnt;
    }
    mark = cplus_runtime_cleanup_mark();
    assert(cplus_runtime_push_cleanup(drop_res, &res[0]) == 0);
    assert(cplus_runtime_push_cleanup(drop_res, &res[1]) == 0);
    assert(cplus_runtime_cleanup_depth() == mark + 2);
    cplus_runtime_pop_cleanup(1); /* runs B */
    cplus_runtime_pop_cleanup(1); /* runs A */
    assert(strcmp(log_order, "BA") == 0);
    assert(cplus_runtime_cleanup_depth() == mark);
    free(res[2].ptr); res[2].ptr = NULL; --live_cnt;
    free(res[3].ptr); res[3].ptr = NULL; --live_cnt;
    assert(live_cnt == 0);

    /* 2. Nested guard recovery unwinds inner then outer in exact LIFO order */
    log_len = 0;
    log_order[0] = '\0';
    for (i = 0; i < 4; ++i) {
        res[i].tag = (char)('A' + i);
        res[i].ptr = malloc(16);
        assert(res[i].ptr != NULL);
        ++live_cnt;
    }
    assert(cplus_runtime_guard(guarded_outer_unwind, res) == CPLUS_RUNTIME_OK);
    assert(strcmp(log_order, "DCBA") == 0);
    assert(cplus_runtime_cleanup_depth() == 0);
    assert(cplus_runtime_unwound_cleanups() == 2);
    assert(live_cnt == 0);

    /* 3. Normal return leaving unpopped cleanups still runs them and counts leak */
    cplus_runtime_reset_counters();
    log_len = 0;
    log_order[0] = '\0';
    res[0].tag = 'Z';
    res[0].ptr = malloc(8);
    assert(res[0].ptr != NULL);
    ++live_cnt;
    assert(cplus_runtime_guard(guarded_leaked_cleanup, &res[0]) == CPLUS_RUNTIME_OK);
    assert(strcmp(log_order, "Z") == 0);
    assert(cplus_runtime_leaked_cleanups() == 1);
    assert(live_cnt == 0);

    /* 4. Direct API edge cases: handler, failstop, unwind_cleanups, invalid args */
    cplus_runtime_reset_counters();
    cplus_runtime_set_failstop(1);
    assert(cplus_runtime_get_failstop() == 1);
    cplus_runtime_set_failstop(0);
    assert(cplus_runtime_get_failstop() == 0);
    assert(cplus_runtime_push_cleanup(NULL, NULL) == -1);
    assert(cplus_runtime_guard(NULL, NULL) == CPLUS_RUNTIME_ERROR);
    cplus_runtime_pop_cleanup(0); /* empty stack no-op */
    cplus_runtime_report(NULL);

    log_len = 0;
    log_order[0] = '\0';
    res[0].tag = 'M';
    res[0].ptr = malloc(8);
    assert(res[0].ptr != NULL);
    res[1].tag = 'N';
    res[1].ptr = malloc(8);
    assert(res[1].ptr != NULL);
    live_cnt = 2;
    assert(cplus_runtime_push_cleanup(drop_res, &res[0]) == 0);
    assert(cplus_runtime_push_cleanup(drop_res, &res[1]) == 0);
    cplus_runtime_pop_cleanup(0); /* skip N without running */
    free(res[1].ptr);
    res[1].ptr = NULL;
    --live_cnt;
    cplus_runtime_unwind_cleanups(); /* unwinds M */
    cplus_runtime_cleanup_unwind_to(-1);
    assert(strcmp(log_order, "M") == 0);
    assert(live_cnt == 0);
    assert(cplus_runtime_cleanup_depth() == 0);

    puts("runtime_cleanup: ok");
    return 0;
}
