#ifndef CPLUS_LIBC_TIME_H
#define CPLUS_LIBC_TIME_H
#include <stddef.h>
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef time_t cplus_time_t;
typedef struct tm cplus_tm;
time_t time(time_t*); double difftime(time_t,time_t); clock_t clock(void);
struct tm *gmtime(const time_t*); struct tm *localtime(const time_t*); time_t mktime(struct tm*);
size_t strftime(char*,size_t,const char*,const struct tm*);
#ifdef __cplusplus
}
#endif
#endif
