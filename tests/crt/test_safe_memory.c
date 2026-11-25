#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "safe_memory.h"
int main(void){
    char d[16]="xxxxxxxxxxxxxxx";
    char s[16]="abcdef";
    assert(cplus_memcpy_s(d,sizeof(d),s,7)==0);
    assert(strcmp(d,"abcdef")==0);
    assert(cplus_memcpy_s(d,3,s,7)!=0); /* dmax too small */
    int diff=999;
    assert(cplus_memcmp_s("abc",3,"abc",3,&diff)==0 && diff==0);
    assert(cplus_memcmp_s("abc",3,"abd",3,&diff)==0 && diff!=0);
    assert(cplus_memset_s(d,sizeof(d),'A',5)==0);
    assert(memcmp(d,"AAAAA",5)==0);
    assert(cplus_memzero_s(d,sizeof(d))==0);
    assert(d[0]==0 && d[15]==0);
    puts("test_safe_memory: ok");
    return 0;
}
