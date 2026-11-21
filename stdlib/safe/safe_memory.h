#ifndef CPLUS_SAFE_MEMORY_H
#define CPLUS_SAFE_MEMORY_H
#include <stddef.h>
#include <stdint.h>
#if defined(__cplusplus)
extern "C" {
#endif
#ifndef CPLUS_SAFE_ERRNO_T_DEFINED
typedef int errno_t;
#define CPLUS_SAFE_ERRNO_T_DEFINED
#endif
#ifndef RSIZE_MAX_MEM
#define RSIZE_MAX_MEM ((size_t)(SIZE_MAX >> 1))
#endif
#ifndef CPLUS_SAFE_RSIZE_T_DEFINED
typedef size_t rsize_t;
#define CPLUS_SAFE_RSIZE_T_DEFINED
#endif
errno_t cplus_memcpy_s(void *dest, rsize_t dmax, const void *src, rsize_t smax);
errno_t cplus_memmove_s(void *dest, rsize_t dmax, const void *src, rsize_t smax);
errno_t cplus_memset_s(void *dest, rsize_t dmax, int value, rsize_t n);
errno_t cplus_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff);
errno_t cplus_memzero_s(void *dest, rsize_t dmax);
#if defined(__cplusplus)
}
#endif
#endif
