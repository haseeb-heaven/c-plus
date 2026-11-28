/* C+ hardened libc: vendored safeclib field compare/copy + collation APIs.
 *
 * Declares the three prefixed entry points defined in
 * libc/src/safecrt_vendor/field_group.c. That translation unit #includes the
 * pinned upstream rurban/safeclib sources VERBATIM, applying symbol-prefix
 * renames only; the algorithms and constraint ordering are the upstream ones.
 *
 * Provenance: rurban/safeclib @ 90b076e94c4b0fb202dddf948d5d620e5395c1b0,
 * MIT license (third_party/safeclib/COPYING). See docs/libc/vendor_FIELD.md
 * for exact upstream paths, the prefix map, and verification evidence.
 *
 * Additive and C99/C++-clean: at the pinned safeclib headers rsize_t is a
 * typedef of size_t and errno_t is a typedef of int, so those underlying
 * types are spelled out here to keep this header self-contained (no build
 * output header is required). The upstream `restrict` qualifiers on strcoll_s
 * do not affect linkage and are omitted here for C++ compatibility. */
#ifndef CPLUS_VENDOR_FIELD_H
#define CPLUS_VENDOR_FIELD_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Error values from third_party/safeclib/include/safe_lib_errno.h @ pin. */
#define CPLUS_V_FIELD_EOK     (0)
#define CPLUS_V_FIELD_ESNULLP (400) /* null pointer                  */
#define CPLUS_V_FIELD_ESZEROL (401) /* length is zero                */
#define CPLUS_V_FIELD_ESLEMAX (403) /* length exceeds RSIZE_MAX_STR  */
#define CPLUS_V_FIELD_ESOVRLP (404) /* overlapping objects           */
#define CPLUS_V_FIELD_ESNOSPC (406) /* not enough space for dest     */

/* RSIZE_MAX_STR at the pin: (4UL << 10) == 4096 bytes. */
#define CPLUS_V_FIELD_RSIZE_MAX_STR ((size_t)(4UL << 10))

/* Value of safeclib's BOS_UNKNOWN: pass this as `destbos` when the caller
 * cannot supply the destination object's size. */
#define CPLUS_V_FIELD_BOS_UNKNOWN ((size_t)-1)

/* errno_t _strcmpfld_s_chk(const char *dest, rsize_t dmax,
 *                          const char *src, int *resultp, size_t destbos)
 * Upstream: third_party/safeclib/src/extstr/strcmpfld_s.c
 * Compares character arrays for dmax characters; the NUL does not stop the
 * comparison. On EOK the sign of *resultp is the comparison result; otherwise
 * *resultp is zeroed and the return code is one of the ES* values above (or
 * EOVERFLOW from <errno.h> when dmax exceeds destbos). */
int cplus_v__strcmpfld_s_chk(const char *dest, size_t dmax, const char *src,
                             int *resultp, size_t destbos);

/* errno_t _strcpyfld_s_chk(char *dest, rsize_t dmax, const char *src,
 *                          rsize_t slen, size_t destbos)
 * Upstream: third_party/safeclib/src/extstr/strcpyfld_s.c
 * Copies slen bytes from src to dest without stopping at NUL, then zeros the
 * remaining slack bytes of the field. EOK on success (including slen == 0);
 * otherwise an ES* value (or EOVERFLOW from <errno.h>). */
int cplus_v__strcpyfld_s_chk(char *dest, size_t dmax, const char *src,
                             size_t slen, size_t destbos);

/* errno_t _strcoll_s_chk(const char *dest, rsize_t dmax, const char *src,
 *                        int *resultp, size_t destbos)
 * Upstream: third_party/safeclib/src/extstr/strcoll_s.c
 * Locale-aware (LC_COLLATE) comparison of the two NUL-terminated strings.
 * On EOK the sign of *resultp is the collation result; otherwise *resultp is
 * zeroed and an ES* value (or EOVERFLOW) is returned. */
int cplus_v__strcoll_s_chk(const char *dest, size_t dmax, const char *src,
                           int *resultp, size_t destbos);

#ifdef __cplusplus
}
#endif

#endif /* CPLUS_VENDOR_FIELD_H */
