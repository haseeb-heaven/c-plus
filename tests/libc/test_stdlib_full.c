#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
int cplus_abs(int); long cplus_labs(long); int cplus_atoi(const char*); long cplus_atol(const char*);
long cplus_strtol(const char*,char**,int); unsigned long cplus_strtoul(const char*,char**,int);
void cplus_qsort(void*,size_t,size_t,int(*)(const void*,const void*)); void *cplus_bsearch(const void*,const void*,size_t,size_t,int(*)(const void*,const void*));
int cplus_rand(void); void cplus_srand(unsigned int); char *cplus_getenv(const char*); int cplus_system(const char*);
static int icmp(const void*a,const void*b){int x=*(const int*)a,y=*(const int*)b;return (x>y)-(x<y);} 
int main(void){
    for(int i=-10;i<=10;i++){ assert(cplus_abs(i)==abs(i)); assert(cplus_labs((long)i)==labs((long)i)); }
    for(int i=0;i<10;i++){ char b[32]; sprintf(b,"%d",i*3); assert(cplus_atoi(b)==i*3); assert(cplus_atol(b)==i*3); char*e=NULL; assert(cplus_strtol(b,&e,10)==i*3); assert(cplus_strtoul(b,&e,10)==(unsigned long)(i*3)); }
    int a[10]={9,7,5,3,1,8,6,4,2,0}; cplus_qsort(a,10,sizeof(int),icmp); for(int i=1;i<10;i++) assert(a[i-1]<=a[i]);
    for(int i=0;i<10;i++){ int *f=(int*)cplus_bsearch(&i,a,10,sizeof(int),icmp); assert(f && *f==i); }
    cplus_srand(1); int r1=cplus_rand(); cplus_srand(1); assert(cplus_rand()==r1);
    char *p=cplus_getenv("PATH"); assert(p==NULL||strlen(p)>=0);
    for(int i=0;i<10;i++) assert(cplus_system("true")==0);
    return 0;
}
