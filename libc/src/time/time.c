#include "../../log/log.h"
#include <time.h>
#include <stddef.h>

time_t cplus_time(time_t *t){ cplus_libc_log("time","enter"); return time(t); }
double cplus_difftime(time_t end,time_t beg){ cplus_libc_log("difftime","enter"); return difftime(end,beg); }
clock_t cplus_clock(void){ cplus_libc_log("clock","enter"); return clock(); }
struct tm *cplus_gmtime(const time_t *t){ cplus_libc_log("gmtime","enter"); return gmtime(t); }
struct tm *cplus_localtime(const time_t *t){ cplus_libc_log("localtime","enter"); return localtime(t); }
time_t cplus_mktime(struct tm *tm){ cplus_libc_log("mktime","enter"); return mktime(tm); }
size_t cplus_strftime(char *s,size_t max,const char *fmt,const struct tm *tm){ cplus_libc_log("strftime","enter"); return strftime(s,max,fmt,tm); }
