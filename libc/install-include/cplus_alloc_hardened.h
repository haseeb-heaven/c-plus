/* Additive, failure-atomic allocation wrappers for the C+ hardened libc.
 *
 * These are NEW symbols layered on top of the existing cplus_malloc /
 * cplus_realloc / cplus_free_checked metadata side table. They do not change
 * the pointer representation, replace any existing allocator entry point, or
 * require a different link line: link build/libc/libcplus_hardened.a as usual.
 *
 * Failure model:
 *   - cplus_alloc_atomic_realloc returns 0 on success, ENOMEM on an allocation
 *     failure, or EINVAL for an invalid argument / untracked pointer. On any
 *     non-zero return *ptr and the bytes it points to are left untouched.
 *   - cplus_alloc_checked_free returns 0 after freeing and NULLing *ptr, or
 *     EINVAL (with *ptr untouched) when ptr is NULL, *ptr is NULL, or *ptr is
 *     not a live tracked allocation (e.g. a double free).
 *   - cplus_alloc_state_dump writes a fixed-size, path-free diagnostic block
 *     to stream; a NULL stream is silently ignored.
 *
 * Threading: each wrapper serialises its own diagnostic counters. The
 * underlying metadata table is thread-safe, but a single void ** must still
 * be owned by one thread at a time (the check-then-act window is not atomic
 * across unrelated threads mutating the same pointer slot).
 */
#ifndef CPLUS_ALLOC_HARDENED_H
#define CPLUS_ALLOC_HARDENED_H

#include <stddef.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

int cplus_alloc_atomic_realloc(void **ptr, size_t new_size);
int cplus_alloc_checked_free(void **ptr);
void cplus_alloc_state_dump(FILE *stream);

#ifdef __cplusplus
}
#endif

#endif /* CPLUS_ALLOC_HARDENED_H */
