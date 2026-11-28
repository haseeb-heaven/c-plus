/* Internal check-and-act helpers shared by alloc.c and alloc_hardened.c.
 * Not an installed API; only translation units inside libc/src/stdlib include
 * this header. The hardened wrappers must not open a check-then-act window
 * across the public metadata APIs: another thread could free or reallocate
 * the same address in between. These helpers hold g_meta_lock across the
 * generation check, the host operation and the metadata update, so the
 * generation observed by the caller is the generation actually operated on.
 */
#ifndef CPLUS_ALLOC_INTERNAL_H
#define CPLUS_ALLOC_INTERNAL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Internal result codes (errno-style values are mapped by the caller). */
enum {
    CPLUS_ALLOC_GEN_OK = 0,
    CPLUS_ALLOC_GEN_INVALID = 1, /* NULL slot / untracked or freed pointer */
    CPLUS_ALLOC_GEN_STALE = 2,   /* generation changed since the caller's check */
    CPLUS_ALLOC_GEN_NOMEM = 3    /* injection or metadata reserve failure */
};

/* expected_gen == 0 means "operate on the current generation". A non-zero
 * value must match the slot's generation exactly. On any failure *inout is
 * left unchanged. */
int cplus_alloc_realloc_generation(void **inout, size_t size,
                                   unsigned long long expected_gen);
int cplus_alloc_free_generation(void **inout, unsigned long long expected_gen);

#ifdef __cplusplus
}
#endif
#endif /* CPLUS_ALLOC_INTERNAL_H */
