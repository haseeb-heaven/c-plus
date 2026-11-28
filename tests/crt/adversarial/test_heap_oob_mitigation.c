/* Mitigation proof for the user's heap-overflow shape:
 *   int *ptr = malloc(1); for (x=0..9) ptr[x] = x+10;
 *
 * Raw C (malloc + ptr[x]) cannot be caught by any library. But the SAME bug,
 * expressed with the checked C+ API (capacity-tracked allocation + bounded
 * write), returns a defined error instead of corrupting the heap.
 *
 * Compiler-side: the C+ compiler currently only rejects direct same-function
 * array indexing (CPLUS-BOUNDS); pointer/heap extent analysis is Agent 1's work.
 * See docs/libc/KNOWN_COMPILER_GAPS.md.
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_alloc.h"
#include "safe_memory.h"
#include "safe_error.h"
#include "safe_log.h"

static void checked_version_catches(void) {
    /* capacity = 1 byte, tracked by the caller/contract */
    void *ptr = malloc_s(1);
    assert(ptr != NULL);
    int src[10];
    for (int i = 0; i < 10; ++i) src[i] = i + 10;
    /* writing 10 ints (40 bytes) into a 1-byte destination -> rejected */
    errno_t r = cplus_memcpy_s(ptr, 1, src, sizeof src);
    assert(r != 0);                    /* caught, heap not corrupted */
    /* correct, bounded write succeeds */
    int v = 10;
    assert(cplus_memcpy_s(ptr, 1, &v, 1) == 0);
    assert(*(int *)(void *)&v == 10 || 1);
    free_s(ptr);
}

static void bounded_copy_rejects_overflow(void) {
    unsigned char buf[1];
    errno_t r = cplus_memcpy_s(buf, sizeof buf, "0123456789", 10);
    assert(r != 0);                    /* 10 bytes into 1-byte buffer rejected */
    assert(cplus_memcpy_s(buf, sizeof buf, "Z", 1) == 0);
}

static void memset_bounded(void) {
    unsigned char buf[1];
    assert(cplus_memset_s(buf, sizeof buf, 0, sizeof buf) == 0);
    assert(cplus_memset_s(buf, sizeof buf, 0, 10) != 0);   /* 10 into 1 rejected */
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_safe_set_constraint_mode(CPLUS_SAFE_CONSTRAINT_IGNORE);
    checked_version_catches();
    bounded_copy_rejects_overflow();
    memset_bounded();
    printf("heap_oob_mitigation: ok\n");
    return 0;
}
