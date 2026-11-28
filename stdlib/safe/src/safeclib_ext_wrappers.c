/* Thin C+ wrappers around the two vendored safeclib timingsafe sources.
 *
 * The upstream _timingsafe_*_chk symbols cannot be renamed with -D because
 * safeclib's BOS_OVR2 attribute macros reference the same token; compiling the
 * upstream files unprefixed and wrapping here keeps the dependency closure
 * intact while exporting only cplus_sc_* names. */
#define CPLUS_SAFE_NO_UNPREFIXED_MACROS
#include "../safe_ext_mem.h"
#include <stddef.h>

#define BOS_UNKNOWN ((size_t)-1)

extern int _timingsafe_bcmp_chk(const void *b1, const void *b2, size_t n,
                                size_t destbos, size_t srcbos);
extern int _timingsafe_memcmp_chk(const void *b1, const void *b2, size_t len,
                                  size_t destbos, size_t srcbos);

int cplus_sc_timingsafe_bcmp(const void *b1, const void *b2, size_t n) {
    return _timingsafe_bcmp_chk(b1, b2, n, BOS_UNKNOWN, BOS_UNKNOWN);
}

int cplus_sc_timingsafe_memcmp(const void *b1, const void *b2, size_t len) {
    return _timingsafe_memcmp_chk(b1, b2, len, BOS_UNKNOWN, BOS_UNKNOWN);
}
