/*
 * Bug CLASS: heap buffer overflow (raw C: malloc(n) + write m > n bytes).
 *
 * The SAME bug expressed with the checked C+ bounded API: the allocation is
 * tracked by the caller-declared capacity (dmax) handed to cplus_memcpy_s /
 * cplus_memset_s. A too-small dmax with an oversized source/count is REJECTED
 * with a defined non-zero constraint error instead of corrupting the heap.
 *
 * Guard bytes placed beyond dmax inside the (deliberately larger) heap block
 * must remain byte-for-byte intact: the checked op is bounded by dmax and the
 * constraint handler only clears up to dmax bytes.
 */
#include <stdio.h>
#include <string.h>
#include "safe_memory.h"
#include "safe_alloc.h"
#include "safe_error.h"
#include "safe_log.h"
#include "safe_lib_errno.h" /* ESNOSPC / ESLEMAX constraint codes */

static int failures = 0;
#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            failures++;                                                        \
            fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
        }                                                                      \
    } while (0)

#define BLK    64
#define USABLE 16

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);

    void *p = NULL;
    CHECK(malloc_s(&p, BLK) == CPLUS_SAFE_OK);
    CHECK(p != NULL);
    if (!p) {
        printf("bugclass_heap_overflow: FAILURES=%d\n", failures);
        return 1;
    }

    unsigned char src[40];
    memset(src, 0xCC, sizeof src);

    /* too-small dmax + oversized source -> rejected (not a heap smash) */
    memset(p, 0x5A, BLK); /* whole block = guard pattern */
    errno_t r = cplus_memcpy_s(p, USABLE, src, sizeof src);
    CHECK(r != 0);
    CHECK(r == ESNOSPC); /* slen > dmax, slen <= RSIZE_MAX -> ESNOSPC */
    for (int i = USABLE; i < BLK; ++i) CHECK(((unsigned char *)p)[i] == 0x5A);

    /* memset with n > dmax -> rejected, guard beyond dmax intact */
    memset(p, 0x5A, BLK);
    r = cplus_memset_s(p, USABLE, 0x77, BLK);
    CHECK(r != 0);
    CHECK(r == ESNOSPC);
    for (int i = USABLE; i < BLK; ++i) CHECK(((unsigned char *)p)[i] == 0x5A);

    /* exactly dmax bytes succeeds and never touches the guard region */
    memset(p, 0x5A, BLK);
    CHECK(cplus_memcpy_s(p, USABLE, src, USABLE) == 0);
    for (int i = 0; i < USABLE; ++i) CHECK(((unsigned char *)p)[i] == 0xCC);
    for (int i = USABLE; i < BLK; ++i) CHECK(((unsigned char *)p)[i] == 0x5A);

    /* one byte past dmax is already rejected */
    memset(p, 0x5A, BLK);
    CHECK(cplus_memcpy_s(p, USABLE, src, USABLE + 1) != 0);
    for (int i = USABLE; i < BLK; ++i) CHECK(((unsigned char *)p)[i] == 0x5A);

    /* a huge length is rejected before any store */
    memset(p, 0x5A, BLK);
    CHECK(cplus_memcpy_s(p, USABLE, src, (rsize_t)-1) != 0);
    for (int i = USABLE; i < BLK; ++i) CHECK(((unsigned char *)p)[i] == 0x5A);

    free_s(&p);
    CHECK(p == NULL);

    if (failures == 0) {
        printf("bugclass_heap_overflow: ok\n");
        return 0;
    }
    printf("bugclass_heap_overflow: FAILURES=%d\n", failures);
    return 1;
}
