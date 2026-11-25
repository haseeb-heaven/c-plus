/* Minimal stdlib extras. Platform-backed where semantics require host/runtime.
 * Documented as partial; no full ISO C claim. */
#include <stdlib.h>
#include <errno.h>
#include <limits.h>
#include <ctype.h>
#include "../../log/log.h"

int cplus_abs(int x){ cplus_libc_log("abs","enter"); return x<0?-x:x; }
long cplus_labs(long x){ cplus_libc_log("labs","enter"); return x<0?-x:x; }
int cplus_atoi(const char *s){ cplus_libc_log("atoi","enter"); return (int)strtol(s? s:"0", NULL, 10); }
long cplus_atol(const char *s){ cplus_libc_log("atol","enter"); return strtol(s? s:"0", NULL, 10); }
long cplus_strtol(const char *nptr, char **endptr, int base){ cplus_libc_log("strtol","enter"); return strtol(nptr,endptr,base); }
unsigned long cplus_strtoul(const char *nptr, char **endptr, int base){ cplus_libc_log("strtoul","enter"); return strtoul(nptr,endptr,base); }
void cplus_qsort(void *base,size_t nmemb,size_t size,int (*compar)(const void*,const void*)){ cplus_libc_log("qsort","enter"); qsort(base,nmemb,size,compar); }
void *cplus_bsearch(const void*key,const void*base,size_t nmemb,size_t size,int (*compar)(const void*,const void*)){ cplus_libc_log("bsearch","enter"); return bsearch(key,base,nmemb,size,compar); }
