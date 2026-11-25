#ifndef CPLUS_SAFE_PLATFORM_H
#define CPLUS_SAFE_PLATFORM_H
#include <stddef.h>
#if defined(__cplusplus)
extern "C" {
#endif
typedef struct cplus_safe_platform_info {
    int has_fortify;
    int has_object_size;
    int has_annex_k;
    int has_apple_libc;
    int has_ucrt;
    int has_glibc;
    int has_musl;
} cplus_safe_platform_info;
void cplus_safe_platform_probe(cplus_safe_platform_info *out);
#if defined(__cplusplus)
}
#endif
#endif
