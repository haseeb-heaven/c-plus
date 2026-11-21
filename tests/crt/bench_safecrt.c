/* Simple benchmark: system libc vs cplus checked APIs. Not a safety claim. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "safe_memory.h"
#include "safe_string.h"
#include "safe_alloc.h"

static double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1000000.0;
}
int main(void) {
    const int N = 200000;
    char src[64], dst[64], sd[64];
    memset(src, 'a', sizeof(src)-1); src[63]=0;
    volatile double sink=0;
    double t0=now_ms();
    for(int i=0;i<N;i++){ memcpy(dst,src,64); sink+=dst[0]; }
    double t1=now_ms();
    for(int i=0;i<N;i++){ cplus_memcpy_s(dst,64,src,64); sink+=dst[0]; }
    double t2=now_ms();
    for(int i=0;i<N;i++){ strcpy(sd,src); sink+=sd[0]; }
    double t3=now_ms();
    for(int i=0;i<N;i++){ cplus_strcpy_s(sd,64,src); sink+=sd[0]; }
    double t4=now_ms();
    for(int i=0;i<N/10;i++){ void*p=malloc(64); if(p){((char*)p)[0]=1; sink+=((char*)p)[0]; free(p);} }
    double t5=now_ms();
    for(int i=0;i<N/10;i++){ void*p=NULL; if(malloc_s(&p,64)==CPLUS_SAFE_OK){((char*)p)[0]=1; sink+=((char*)p)[0]; free_s(&p);} }
    double t6=now_ms();
    printf("memcpy %.3fms memcpy_s %.3fms\n", t1-t0, t2-t1);
    printf("strcpy %.3fms strcpy_s %.3fms\n", t3-t2, t4-t3);
    printf("malloc/free %.3fms malloc_s/free_s %.3fms\n", t5-t4, t6-t5);
    printf("sink %.0f\n", sink);
    return 0;
}
