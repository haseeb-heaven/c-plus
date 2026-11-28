/* Platform-backed string extras (C+ libc slice). Delegate to installed libc. */
#define _DEFAULT_SOURCE
#include "../../log/log.h"
#include <string.h>

char *cplus_strsignal(int sig){ cplus_libc_log("strsignal","enter"); return strsignal(sig); }
int cplus_strcoll(const char *a,const char *b){ cplus_libc_log("strcoll","enter"); return strcoll(a,b); }
size_t cplus_strxfrm(char *d,const char *s,size_t n){ cplus_libc_log("strxfrm","enter"); return strxfrm(d,s,n); }
