#ifndef CPLUS_SAFE_ALLOC_H
#define CPLUS_SAFE_ALLOC_H
#include <stddef.h>
#include <stdint.h>
#include "safe_error.h"
#if defined(__cplusplus)
extern "C" {
#endif
#ifndef RSIZE_MAX_MEM
#define RSIZE_MAX_MEM ((size_t)(SIZE_MAX >> 1))
#endif
/* Optional C+ extension with standard C allocator signatures and a
 * thread-safe live-allocation metadata table.
 *
 * Ownership model
 * ---------------
 * Every block returned by malloc_s/calloc_s/realloc_s/aligned_alloc_s is
 * recorded in a thread-safe live-allocation table (pointer, byte_size,
 * elem_size, generation).  The table is what makes the following detections
 * possible *for blocks that came from this allocator*:
 *   - double free      (pointer was released earlier; bounded recent-free cache)
 *   - invalid free     (pointer is not a live block of this allocator: stack,
 *                       static, interior pointer, or raw malloc() memory)
 *   - leak counting    (cplus_safe_alloc_live_count/bytes)
 *   - length queries   (cplus_safe_ptr_size, cplus_safe_ptr_elem_size,
 *                       cplus_safe_ptr_count, cplus_safe_query_length,
 *                       cplus_safe_alloc_get_meta)
 *   - stale metadata   (generation check detects freed or re-issued addresses)
 * Blocks from raw malloc() must be released with raw free(); passing them to
 * free_s/realloc_s is refused (the block is left alone: a leak is safer than
 * corrupting the heap) and sets errno = EINVAL.
 *
 * Limits (honest): the recent-free cache is bounded, so a double free of a
 * very old block whose address has not been reused is still reported as an
 * *invalid* free (never freed twice); a stale alias of a block whose address
 * was re-issued is indistinguishable from the new owner unless checked via
 * generation.  This is runtime diagnosis, not a proof of lifetime safety.
 *
 * Contracts (standard C parameter lists and return types):
 *   malloc_s(size): returns NULL (errno=EINVAL if size > RSIZE_MAX_MEM,
 *     errno=ENOMEM on OOM); size 0 yields a defined 1-byte tracked block.
 *   calloc_s(count, size): returns NULL with errno=EOVERFLOW on count*size
 *     overflow (> RSIZE_MAX_MEM); zero count/size yields a defined 1-byte
 *     zeroed block; records elem_size.
 *   realloc_s(ptr, size): ptr==NULL acts like malloc_s(size); on invalid ptr or
 *     size > RSIZE_MAX_MEM or OOM returns NULL (errno=EINVAL/ENOMEM) and leaves
 *     ptr live and untouched; on success preserves elem_size & bumps generation.
 *   free_s(ptr): NULL is a no-op; rejects double/invalid free safely.
 *   aligned_alloc_s(alignment, size): requires power-of-two >= sizeof(void*).
 */
void *malloc_s(size_t size);
void *calloc_s(size_t count, size_t size);
void *realloc_s(void *ptr, size_t size);
void free_s(void *ptr);

/* Checked extension returning CPLUS_SAFE_OK (0) for NULL or a valid live block,
 * or CPLUS_SAFE_EINVAL for a double free or unowned/invalid pointer (refused). */
int free_checked_s(void *ptr);

/* Aligned allocation returning NULL on invalid alignment or OOM. */
void *aligned_alloc_s(size_t alignment, size_t size);

/* Allocation metadata and safe length-query API (thread-safe). */
typedef struct cplus_safe_alloc_meta {
    size_t byte_size;
    size_t elem_size;
    size_t elem_count;
    unsigned long long generation;
    int is_live;
} cplus_safe_alloc_meta;

int    cplus_safe_alloc_get_meta(const void *ptr, cplus_safe_alloc_meta *out);
int    cplus_safe_ptr_is_live(const void *ptr);
int    cplus_safe_ptr_was_freed(const void *ptr);
size_t cplus_safe_ptr_size(const void *ptr);       /* 0 when not a live block */
size_t cplus_safe_ptr_elem_size(const void *ptr);  /* 0 when not a live block */
size_t cplus_safe_ptr_count(const void *ptr);      /* byte_size / elem_size */
int    cplus_safe_query_length(const void *ptr, size_t elem_size, size_t *out_count);
int    cplus_safe_ptr_check_generation(const void *ptr, unsigned long long generation);
size_t cplus_safe_alloc_live_count(void);
size_t cplus_safe_alloc_live_bytes(void);
#define cplus_safe_len(ptr) \
    ((ptr) != NULL ? (cplus_safe_ptr_size(ptr) / sizeof(*(ptr))) : (size_t)0)

/* Explicit ownership transfer: *dst takes the block, *src becomes NULL, and
 * the block's generation is bumped.  EINVAL if either address is NULL, *src
 * is not a live allocation, or *dst already holds a pointer. */
int cplus_safe_transfer_ownership(void **dst, void **src);

/* Allocation-failure injection (testing aid; OFF by default).
 * After `fail_after(n)`, the next n allocations succeed and every later
 * allocation through this API fails with ENOMEM until `fail_reset()`. */
void cplus_safe_alloc_fail_after(long count);
void cplus_safe_alloc_fail_reset(void);
unsigned long cplus_safe_alloc_seq(void);
#if defined(__cplusplus)
}
#endif
#endif
