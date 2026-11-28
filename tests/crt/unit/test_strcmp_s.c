#include <assert.h>
#include <stdio.h>
#include "safe_string.h"
int main(void) {
    int ind=5;
    assert(cplus_strcmp_s("aa",2,"aa",&ind)==0 && ind==0);
    assert(cplus_strcmp_s("aa",2,"ab",&ind)==0 && ind<0);
    assert(cplus_strcmp_s("aa",2,NULL,&ind)!=0);
    puts("unit strcmp_s: ok");
    return 0;
}
