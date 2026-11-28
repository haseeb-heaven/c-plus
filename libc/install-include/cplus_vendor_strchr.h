/* C+ hardened libc: vendored rurban/safeclib strchr_s / strrchr_s.
 *
 * Declares the prefix-renamed entry points included verbatim in
 * libc/src/safecrt_vendor/strchr_group.c.
 *
 * Provenance (record exactly):
 *   upstream : rurban/safeclib
 *   commit   : 90b076e94c4b0fb202dddf948d5d620e5395c1b0
 *   license  : MIT (third_party/safeclib/COPYING)
 *   sources  : third_party/safeclib/src/extstr/strchr_s.c
 *              third_party/safeclib/src/extstr/strrchr_s.c
 *
 * Additive and C99-clean: rsize_t is size_t and errno_t is int in the pinned
 * safeclib headers, so those types are spelled out here to keep this header
 * self-contained. Only symbols actually defined by strchr_group.o are
 * declared: the two prefixed *_chk entry points. (The upstream internal
 * _strchr_s_uchk / _strrchr_s_uchk workers are also present in the object but
 * are not part of this public surface.) */
#ifndef CPLUS_VENDOR_STRCHR_H
#define CPLUS_VENDOR_STRCHR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error values from third_party/safeclib/include/safe_lib_errno.h @ pin. */
#define CPLUS_VENDOR_STRCHR_EOK       (0)
#define CPLUS_VENDOR_STRCHR_ESNULLP   (400) /* null pointer                */
#define CPLUS_VENDOR_STRCHR_ESZEROL   (401) /* length is zero              */
#define CPLUS_VENDOR_STRCHR_ESLEMAX   (403) /* length exceeds RSIZE_MAX    */
#define CPLUS_VENDOR_STRCHR_ESNOTFND  (409) /* not found                   */

/* BOS_UNKNOWN and RSIZE_MAX_STR as used by the pinned safeclib headers. */
#define CPLUS_VENDOR_STRCHR_BOS_UNKNOWN      ((size_t)-1)
#define CPLUS_VENDOR_STRCHR_RSIZE_MAX_STR    ((size_t)(4UL << 10))

/* Upstream: errno_t _strchr_s_chk(const char *restrict dest, rsize_t dmax,
 *   const int ch, char **restrict resultp, const size_t destbos)
 * Renamed to cplus_v__strchr_s_chk; body included verbatim. On EOK *resultp
 * points at the first match; on any other return *resultp is NULL. Pass
 * CPLUS_VENDOR_STRCHR_BOS_UNKNOWN as destbos for runtime-only checking. */
int cplus_v__strchr_s_chk(const char *restrict dest, size_t dmax,
                          const int ch, char **restrict resultp,
                          const size_t destbos);

/* Upstream: errno_t _strrchr_s_chk(...) with the same argument list.
 * Renamed to cplus_v__strrchr_s_chk; finds the last match and honors dmax. */
int cplus_v__strrchr_s_chk(const char *restrict dest, size_t dmax,
                           const int ch, char **restrict resultp,
                           const size_t destbos);

#ifdef __cplusplus
}
#endif

#endif /* CPLUS_VENDOR_STRCHR_H */
