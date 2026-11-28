#include <assert.h>
#include <string.h>
#include <stdio.h>
#include "safe_format.h"
int main(void) {
    char b[16];
    assert(cplus_sprintf_s(b,sizeof(b),"x%d",3)==2);
    assert(strcmp(b,"x3")==0);
    puts("unit sprintf_s: ok");
    return 0;
}
