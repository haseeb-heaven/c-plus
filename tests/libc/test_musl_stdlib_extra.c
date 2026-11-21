/* musl-derived stdlib integer functions integrated into the C+ libc slice. */
#include <assert.h>
#include <stdlib.h>
div_t cplus_div(int,int); ldiv_t cplus_ldiv(long,long); lldiv_t cplus_lldiv(long long,long long);
long long cplus_llabs(long long); long long cplus_atoll(const char*);
int main(void){
    for(int i=-5;i<=5;i++){ if(i==0) continue; div_t d=cplus_div(100,i); assert(d.quot*i + d.rem==100); }
    { ldiv_t d=cplus_ldiv(1000000L,7L); assert(d.quot*7L + d.rem==1000000L); }
    { lldiv_t d=cplus_lldiv(1000000000000LL,13LL); assert(d.quot*13LL + d.rem==1000000000000LL); }
    for(int i=-6;i<=6;i++) assert(cplus_llabs((long long)i)==(long long)(i<0?-i:i));
    assert(cplus_atoll("12345")==12345LL);
    assert(cplus_atoll("-6789")==-6789LL);
    assert(cplus_atoll("   42")==42LL);
    for(int i=0;i<10;i++){ char b[16]; assert(cplus_atoll(i%2? "-7":"7")== (i%2?-7LL:7LL)); (void)b; }
    return 0;
}
