#ifndef CPLUS_SAFE_STRING_H
#define CPLUS_SAFE_STRING_H
#include <stddef.h>
#include <stdint.h>
#if defined(__cplusplus)
extern "C" {
#endif
#ifndef CPLUS_SAFE_ERRNO_T_DEFINED
typedef int errno_t;
#define CPLUS_SAFE_ERRNO_T_DEFINED
#endif
#ifndef RSIZE_MAX_STR
#define RSIZE_MAX_STR ((size_t)(SIZE_MAX >> 1))
#endif
#ifndef CPLUS_SAFE_RSIZE_T_DEFINED
typedef size_t rsize_t;
#define CPLUS_SAFE_RSIZE_T_DEFINED
#endif
errno_t cplus_strcpy_s(char *dest, rsize_t dmax, const char *src);
errno_t cplus_strncpy_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);
errno_t cplus_strcat_s(char *dest, rsize_t dmax, const char *src);
errno_t cplus_strncat_s(char *dest, rsize_t dmax, const char *src, rsize_t slen);
errno_t cplus_strcmp_s(const char *dest, rsize_t dmax, const char *src, int *indicator);
rsize_t cplus_strnlen_s(const char *str, rsize_t smax);
#if defined(__cplusplus)
}
#endif
#endif
