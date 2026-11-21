#include "safeclib_private.h"
#include "safe_mem_lib.h"
#include "safe_str_lib.h"
#ifndef BOS_UNKNOWN
#define BOS_UNKNOWN ((size_t)-1)
#endif
#ifdef __cplusplus
extern "C" {
#endif
errno_t cplus_sc_memcpy_s(void *restrict dest, rsize_t dmax, const void *restrict src, rsize_t smax){ return cplus_sc__memcpy_s_chk(dest,dmax,src,smax,BOS_UNKNOWN,BOS_UNKNOWN); }
errno_t cplus_sc_memmove_s(void *restrict dest, rsize_t dmax, const void *restrict src, rsize_t smax){ return cplus_sc__memmove_s_chk(dest,dmax,src,smax,BOS_UNKNOWN,BOS_UNKNOWN); }
errno_t cplus_sc_memset_s(void *dest, rsize_t dmax, int value, rsize_t n){ return cplus_sc__memset_s_chk(dest,dmax,value,n,BOS_UNKNOWN); }
errno_t cplus_sc_memcmp_s(const void *dest, rsize_t dmax, const void *src, rsize_t smax, int *diff){ return cplus_sc__memcmp_s_chk(dest,dmax,src,smax,diff,BOS_UNKNOWN,BOS_UNKNOWN); }
errno_t cplus_sc_memzero_s(void *dest, rsize_t dmax){ return cplus_sc__memzero_s_chk(dest,dmax,BOS_UNKNOWN); }
errno_t cplus_sc_strcpy_s(char *restrict dest, rsize_t dmax, const char *restrict src){ return cplus_sc__strcpy_s_chk(dest,dmax,src,BOS_UNKNOWN); }
errno_t cplus_sc_strncpy_s(char *restrict dest, rsize_t dmax, const char *restrict src, rsize_t slen){ return cplus_sc__strncpy_s_chk(dest,dmax,src,slen,BOS_UNKNOWN,BOS_UNKNOWN); }
errno_t cplus_sc_strcat_s(char *restrict dest, rsize_t dmax, const char *restrict src){ return cplus_sc__strcat_s_chk(dest,dmax,src,BOS_UNKNOWN); }
errno_t cplus_sc_strncat_s(char *restrict dest, rsize_t dmax, const char *restrict src, rsize_t slen){ return cplus_sc__strncat_s_chk(dest,dmax,src,slen,BOS_UNKNOWN,BOS_UNKNOWN); }
rsize_t cplus_sc_strnlen_s(const char *str, rsize_t smax){ return cplus_sc__strnlen_s_chk(str,smax,BOS_UNKNOWN); }
errno_t cplus_sc_strcmp_s(const char *dest, rsize_t dmax, const char *src, int *indicator){ return cplus_sc__strcmp_s_chk(dest,dmax,src,indicator,BOS_UNKNOWN,BOS_UNKNOWN); }
#ifdef __cplusplus
}
#endif
