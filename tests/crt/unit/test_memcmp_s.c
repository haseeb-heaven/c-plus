#include <assert.h>
#include <stdio.h>
#include "safe_memory.h"
int main(void) {
    int diff = 9;
    assert(cplus_memcmp_s("abc",3,"abc",3,&diff)==0 && diff==0);
    assert(cplus_memcmp_s("abc",3,"abd",3,&diff)==0 && diff<0);
    assert(cplus_memcmp_s(NULL,3,"abd",3,&diff)!=0);
    assert(cplus_memcmp_s("a",1,"a",1,NULL)!=0);
    puts("unit memcmp_s: ok");
    return 0;
}
