/* Covers the platform-backed POSIX/C11 time wrappers added in batch 6
 * (clock_gettime / nanosleep / timespec_get). */
#define _DEFAULT_SOURCE
#include <assert.h>
#include <time.h>

/* C11 fixes TIME_UTC to 1, but glibc hides timespec_get and TIME_UTC unless
 * an ISO C11 (or GNU) feature level is active.  This runner compiles C99,
 * so supply the standard base value when the platform header omits it; the
 * assertion still checks that the wrapper returns that exact base value. */
#ifndef TIME_UTC
#define TIME_UTC 1
#endif

int cplus_clock_gettime(clockid_t,struct timespec*);
int cplus_nanosleep(const struct timespec*,struct timespec*);
int cplus_timespec_get(struct timespec*,int);

int main(void){
    struct timespec ts;
    assert(cplus_clock_gettime(CLOCK_REALTIME,&ts)==0);
    assert(ts.tv_sec>0);
    assert(ts.tv_nsec>=0 && ts.tv_nsec<1000000000L);

    struct timespec mono;
    assert(cplus_clock_gettime(CLOCK_MONOTONIC,&mono)==0);
    assert(mono.tv_sec>=0 && mono.tv_nsec>=0);

    /* nanosleep of 1ms: the monotonic clock must not go backwards */
    struct timespec before,after;
    assert(cplus_clock_gettime(CLOCK_MONOTONIC,&before)==0);
    struct timespec req={0,1000000L};
    assert(cplus_nanosleep(&req,NULL)==0);
    assert(cplus_clock_gettime(CLOCK_MONOTONIC,&after)==0);
    assert(after.tv_sec>before.tv_sec ||
           (after.tv_sec==before.tv_sec && after.tv_nsec>=before.tv_nsec));

    assert(cplus_timespec_get(&ts,TIME_UTC)==TIME_UTC);
    assert(ts.tv_sec>0);
    assert(ts.tv_nsec>=0 && ts.tv_nsec<1000000000L);

    for(int i=0;i<10;i++){
        struct timespec a;
        assert(cplus_clock_gettime(CLOCK_REALTIME,&a)==0);
        assert(a.tv_nsec>=0 && a.tv_nsec<1000000000L);
    }
    return 0;
}
