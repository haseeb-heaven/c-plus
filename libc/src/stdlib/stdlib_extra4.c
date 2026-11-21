/* Platform-backed stdlib extras (C+ libc slice). Delegate to installed libc. */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <stdlib.h>
#include <inttypes.h>
#include <stdint.h>

int cplus_setenv(const char *n,const char *v,int o){ cplus_libc_log("setenv","enter"); return setenv(n,v,o); }
int cplus_unsetenv(const char *n){ cplus_libc_log("unsetenv","enter"); return unsetenv(n); }
int cplus_putenv(char *s){ cplus_libc_log("putenv","enter"); return putenv(s); }
int cplus_rand_r(unsigned int *seed){ cplus_libc_log("rand_r","enter"); return rand_r(seed); }
float cplus_strtof(const char *n,char **e){ cplus_libc_log("strtof","enter"); return strtof(n,e); }
long double cplus_strtold(const char *n,char **e){ cplus_libc_log("strtold","enter"); return strtold(n,e); }
intmax_t cplus_imaxabs(intmax_t j){ cplus_libc_log("imaxabs","enter"); return imaxabs(j); }
imaxdiv_t cplus_imaxdiv(intmax_t n,intmax_t d){ cplus_libc_log("imaxdiv","enter"); return imaxdiv(n,d); }
