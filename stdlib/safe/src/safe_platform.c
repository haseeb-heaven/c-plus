#include "../safe_platform.h"
#include "../safe_log.h"
#include <stddef.h>

void cplus_safe_platform_probe(cplus_safe_platform_info *out) {
    cplus_safe_log(CPLUS_SAFE_LOG_TRACE, "safe_platform_probe", "enter");
    if (!out) { cplus_safe_log(CPLUS_SAFE_LOG_ERROR, "safe_platform_probe", "null out"); return; }
    out->has_fortify = 0;
    out->has_object_size = 0;
    out->has_annex_k = 0;
    out->has_apple_libc = 0;
    out->has_ucrt = 0;
    out->has_glibc = 0;
    out->has_musl = 0;
#ifdef _FORTIFY_SOURCE
    out->has_fortify = _FORTIFY_SOURCE;
#endif
#if defined(__has_builtin)
# if __has_builtin(__builtin_object_size)
    out->has_object_size = 1;
# endif
#endif
#ifdef __STDC_LIB_EXT1__
    out->has_annex_k = 1;
#endif
#if defined(__APPLE__)
    out->has_apple_libc = 1;
#endif
#if defined(_WIN32)
    out->has_ucrt = 1;
#endif
#if defined(__GLIBC__)
    out->has_glibc = 1;
#endif
#if defined(__MUSL__)
    out->has_musl = 1;
#endif
}
