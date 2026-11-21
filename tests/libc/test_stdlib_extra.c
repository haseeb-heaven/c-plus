#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
int cplus_abs(int); long cplus_labs(long); int cplus_atoi(const char*); long cplus_atol(const char*);
long cplus_strtol(const char*,char**,int); unsigned long cplus_strtoul(const char*,char**,int);
void cplus_qsort(void*,size_t,size_t,int(*)(const void*,const void*));
void *cplus_bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
static int icmp(const void*a,const void*b){ int x=*(const int*)a, y=*(const int*)b; return (x>y)-(x<y); }
int main(void){
    assert(cplus_abs(-3)==3); assert(cplus_labs(-5L)==5L);
    assert(cplus_atoi("42")==42); assert(cplus_atol("-7")==-7L);
    char *e=NULL; assert(cplus_strtol("12x",&e,10)==12 && e && *e=='x');
    int a[5]={5,1,4,2,3}; cplus_qsort(a,5,sizeof(int),icmp);
    assert(a[0]==1 && a[4]==5);
    int k=4; const int *f=(const int*)cplus_bsearch(&k,a,5,sizeof(int),icmp);
    assert(f && *f==4);
    puts("stdlib_extra: ok");
    return 0;
}
