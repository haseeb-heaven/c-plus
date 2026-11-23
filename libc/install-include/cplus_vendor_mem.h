/* C+ hardened libc: vendored safeclib memory-extension APIs.
 *
 * Additive public entry points for the verbatim upstream memchr_s/memccpy_s
 * implementations compiled into libc/src/safecrt_vendor/mem_group.c.
 *
 * Provenance: rurban/safeclib @ 90b076e94c4b0fb202dddf948d5d620e5395c1b0,
 * MIT license (third_party/safeclib/COPYING, Cisco 2012-2013 /
 * Reini Urban 2017). Upstream sources:
 *   third_party/safeclib/src/extmem/memchr_s.c
 *   third_party/safeclib/src/extmem/memccpy_s.c
 *
 * These are new, prefixed symbols only; they do not replace or shadow any
 * existing export. errno_t and rsize_t come from the pinned safeclib types
 * header (errno_t == int, rsize_t == size_t). */
#ifndef CPLUS_VENDOR_MEM_H
#define CPLUS_VENDOR_MEM_H

#include <stddef.h>
#include <safe_types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Find the first occurrence of ch (as unsigned char) in the first dmax
 * bytes of dest. On EOK the match is written to *resultp; on any other
 * return *resultp is NULL. Returns EOK, ESNOTFND, or a constraint code
 * (ESNULLP / ESZEROL / ESLEMAX). Matches safeclib memchr_s semantics. */
errno_t cplus_v_memchr_s(const void *dest, rsize_t dmax, int ch,
                         void **resultp);

/* Copy at most n bytes from src to dest, stopping after the first byte
 * equal to c. Returns EOK on success (including n == 0, which writes a
 * NUL to dest[0]), ESNULLP / ESZEROL / ESLEMAX / ESNOSPC / ESOVRLP on a
 * runtime-constraint violation. Matches safeclib memccpy_s semantics. */
errno_t cplus_v_memccpy_s(void *dest, rsize_t dmax, const void *src, int c,
                          rsize_t n);

#ifdef __cplusplus
}
#endif

#endif /* CPLUS_VENDOR_MEM_H */
