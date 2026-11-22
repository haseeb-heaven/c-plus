/* C + SafeCRT + Hardened Libc + SafeRuntime scenarios for the 45-scenario
 * memory-safety matrix. Compiled with UBSan and run by run_safety_matrix.sh. */
#include <assert.h>
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "install-include/cplus_libc.h"
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_format.h"
#include "safe_io.h"
#include "safe_log.h"
#include "safe_memory.h"
#include "safe_runtime.h"
#include "safe_string.h"

typedef struct scoped_res {
    void *blk;
    int closed;
} scoped_res;

static void release_scoped(void *arg) {
    scoped_res *r = (scoped_res *)arg;
    if (r->blk) {
        free_s(r->blk);
        r->blk = NULL;
    }
    r->closed = 1;
}

static char g_unwind_trace[16];
static size_t g_unwind_pos = 0;

typedef struct order_node {
    char tag;
    void *blk;
} order_node;

static void release_ordered(void *arg) {
    order_node *n = (order_node *)arg;
    if (g_unwind_pos + 1 < sizeof(g_unwind_trace)) {
        g_unwind_trace[g_unwind_pos++] = n->tag;
        g_unwind_trace[g_unwind_pos] = '\0';
    }
    if (n->blk) {
        free_s(n->blk);
        n->blk = NULL;
    }
}

static int guard_unwind_body(void *ctx) {
    order_node *nodes = (order_node *)ctx;
    volatile int arr[2] = {10, 20};
    volatile int idx = 5;
    assert(cplus_runtime_push_cleanup(release_ordered, &nodes[0]) == 0);
    assert(cplus_runtime_push_cleanup(release_ordered, &nodes[1]) == 0);
    arr[idx] = 99; /* UBSan bounds trap triggers __ubsan_on_report -> longjmp */
    return 0;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    size_t safe_base = cplus_safe_alloc_live_count();
    size_t libc_base = cplus_alloc_live_count();

    /* S01-S05: Checked allocation, zero-size, overflow, alignment, OOM fail-preserve */
    {
        void *p = malloc_s(0);
        assert(p != NULL && cplus_safe_ptr_size(p) == 1);
        free_s(p);

        errno = 0;
        assert(calloc_s(SIZE_MAX / 2 + 1, 2) == NULL && errno == EOVERFLOW);
        errno = 0;
        assert(aligned_alloc_s(6, 64) == NULL && errno == EINVAL);

        void *ap = aligned_alloc_s(64, 128);
        assert(ap != NULL && (((uintptr_t)ap) & 63u) == 0);
        free_s(ap);

        int *arr = (int *)calloc_s(8, sizeof(int));
        assert(arr != NULL && cplus_safe_len(arr) == 8 && cplus_safe_ptr_elem_size(arr) == sizeof(int));
        arr[0] = 0x12345678;
        cplus_safe_alloc_fail_after(0);
        assert(realloc_s(arr, 1024) == NULL && cplus_safe_ptr_is_live(arr));
        cplus_safe_alloc_fail_reset();
        assert(arr[0] == 0x12345678);
        free_s(arr);
    }

    /* S06-S10: Double free, invalid free (stack/interior/raw malloc), generation ABA, ownership */
    {
        void *a = malloc_s(32);
        cplus_safe_alloc_meta meta;
        assert(a != NULL && cplus_safe_alloc_get_meta(a, &meta) == CPLUS_SAFE_OK);
        unsigned long long g0 = meta.generation;

        void *b = NULL;
        assert(cplus_safe_transfer_ownership(&b, &a) == CPLUS_SAFE_OK);
        assert(a == NULL && b != NULL);
        assert(cplus_safe_ptr_check_generation(b, g0) == 0);
        assert(cplus_safe_ptr_check_generation(b, g0 + 1) == 1);

        int stack_v = 1;
        assert(free_checked_s(&stack_v) == CPLUS_SAFE_EINVAL);
        assert(free_checked_s((char *)b + 1) == CPLUS_SAFE_EINVAL);
        assert(free_checked_s(b) == CPLUS_SAFE_OK);
        assert(free_checked_s(b) == CPLUS_SAFE_EINVAL);

        void *raw = malloc(16);
        assert(raw && free_checked_s(raw) == CPLUS_SAFE_EINVAL);
        free(raw);
    }

    /* S11-S15: Libc hardened allocator metadata & ownership */
    {
        long *lp = (long *)cplus_calloc(4, sizeof(long));
        size_t cnt = 0;
        cplus_alloc_meta lm;
        assert(lp != NULL && cplus_alloc_len(lp) == 4);
        assert(cplus_alloc_query_length(lp, sizeof(long), &cnt) == 0 && cnt == 4);
        assert(cplus_alloc_get_meta(lp, &lm) == 0);
        void *lpsrc = lp;
        void *lp2 = NULL;
        assert(cplus_transfer_ownership(&lp2, &lpsrc) == 0 && lpsrc == NULL);
        assert(cplus_alloc_check_generation(lp2, lm.generation) == 0);
        assert(cplus_free_checked(&lp2) == 0 && lp2 == NULL);
    }

    /* S16-S22: Checked memory/string/format/IO bounds & double-close prevention */
    {
        char dst[8] = "hi";
        char tokbuf[] = "k1=v1;k2=v2";
        char *ctx = NULL;
        rsize_t dmax = sizeof(tokbuf);
        assert(cplus_memcpy_s(dst, 2, "abcdef", 6) != 0);
        assert(cplus_memcpy_s(dst + 1, 7, dst, 2) != 0); /* overlap ESOVRLP */
        assert(cplus_memmove_s(dst + 1, 7, dst, 2) == 0);
        assert(cplus_strcpy_s(dst, 4, "toolong") != 0);
        assert(cplus_snprintf_s(dst, 4, "%s", "abcdef") == 6 && dst[3] == '\0');
        assert(cplus_strtok_s(tokbuf, &dmax, "=;", &ctx) != NULL);

        const char *path = "/tmp/cplus_scen_c.tmp";
        FILE *f = cplus_fopen_s(path, "wb+");
        assert(f != NULL);
        assert(cplus_fwrite_s("hello", 4, 1, 5, f) == 4);
        assert(cplus_fclose_s(f) == 0);
        assert(cplus_fclose_s(f) == EOF); /* double close blocked */
        unlink(path);
    }

    /* S23-S25: Scoped cleanup stack + UBSan guarded unwind LIFO order */
    {
        scoped_res r = { malloc_s(16), 0 };
        int m = cplus_runtime_cleanup_mark();
        assert(cplus_runtime_push_cleanup(release_scoped, &r) == 0);
        cplus_runtime_cleanup_unwind_to(m);
        assert(r.closed == 1 && r.blk == NULL);

        order_node nodes[2];
        nodes[0].tag = 'A'; nodes[0].blk = malloc_s(16);
        nodes[1].tag = 'B'; nodes[1].blk = malloc_s(16);
        g_unwind_pos = 0;
        g_unwind_trace[0] = '\0';
        assert(cplus_runtime_guard(guard_unwind_body, nodes) == CPLUS_RUNTIME_ERROR);
        assert(strcmp(g_unwind_trace, "BA") == 0);
        assert(nodes[0].blk == NULL && nodes[1].blk == NULL);
    }

    assert(cplus_safe_alloc_live_count() == safe_base);
    assert(cplus_alloc_live_count() == libc_base);
    puts("test_scenarios_c: ok");
    return 0;
}
