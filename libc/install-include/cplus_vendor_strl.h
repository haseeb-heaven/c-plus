/* C+ hardened libc: vendored safeclib strlcpy()/strlcat() (prefixed ABI).
 *
 * Declares the two symbols emitted by libc/src/safecrt_vendor/strl_group.c.
 * That translation unit #includes the upstream sources verbatim; the only
 * change is the cplus_v_ prefix on the exported names (additive ABI).
 *
 * Provenance: rurban/safeclib @ 90b076e94c4b0fb202dddf948d5d620e5395c1b0,
 * MIT (third_party/safeclib/COPYING). Upstream:
 *   third_party/safeclib/src/extstr/strlcat.c
 *   third_party/safeclib/src/extstr/strlcpy.c
 * See docs/libc/vendor_STRL.md for the full record.
 *
 * The upstream _strlcat_chk/_strlcpy_chk prefix macros are defined but emit
 * no symbol, so only the two real functions are declared here. */
#ifndef CPLUS_VENDOR_STRL_H
#define CPLUS_VENDOR_STRL_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__cplusplus)
#  define CPLUS_VENDOR_STRL_RESTRICT
#elif defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#  define CPLUS_VENDOR_STRL_RESTRICT restrict
#else
#  define CPLUS_VENDOR_STRL_RESTRICT
#endif

/* Copies up to dsize-1 bytes from src to dest, NUL-terminating a non-zero
 * sized dest. Returns strlen(src); a return >= dsize means truncation.
 * On a NULL constraint the vendored upstream body invokes the already-checked
 * string constraint handler and returns 0 (writing '\0' to dest only when
 * src is NULL and dsize != 0). */
size_t cplus_v_strlcpy(char *CPLUS_VENDOR_STRL_RESTRICT dest,
                       const char *CPLUS_VENDOR_STRL_RESTRICT src,
                       size_t dsize);

/* Appends src to dest using at most dsize bytes total (NUL included),
 * always NUL-terminating when there is room. Returns the would-be total
 * length (initial dest length + src length); a return >= dsize means
 * truncation. */
size_t cplus_v_strlcat(char *CPLUS_VENDOR_STRL_RESTRICT dest,
                       const char *CPLUS_VENDOR_STRL_RESTRICT src,
                       size_t dsize);

#ifdef __cplusplus
}
#endif

#endif /* CPLUS_VENDOR_STRL_H */
