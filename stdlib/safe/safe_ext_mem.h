#ifndef CPLUS_SAFE_EXT_MEM_H
#define CPLUS_SAFE_EXT_MEM_H
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif

/* Constant-time byte/word comparisons vendored from rurban/safeclib
 * (src/extmem/timingsafe_bcmp.c and timingsafe_memcmp.c, commit recorded in
 * third_party/safeclib/UPSTREAM_COMMIT). Exported under cplus_sc_* names so
 * the host CRT symbols are never interposed. */
int cplus_sc_timingsafe_bcmp(const void *b1, const void *b2, size_t n);
int cplus_sc_timingsafe_memcmp(const void *b1, const void *b2, size_t len);

#ifdef __cplusplus
}
#endif
#endif
