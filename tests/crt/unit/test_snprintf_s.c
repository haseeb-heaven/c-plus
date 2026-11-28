#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_format.h"
int main(void) {
    char b[16];
    assert(cplus_snprintf_s(b,sizeof(b),"x%d",3)==2);
    assert(strcmp(b,"x3")==0);
    int n = cplus_snprintf_s(b,3,"%s","abcd");
    assert(n==4);
    assert(b[2]=='\0');
    puts("unit snprintf_s: ok");
    return 0;
}
