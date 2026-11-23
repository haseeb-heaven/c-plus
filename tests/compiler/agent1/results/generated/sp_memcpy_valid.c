#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/core/positive/memcpy_valid.c"
#include <string.h>
int main(void){char d[8];memcpy(d,"abcdefg",8);return d[0]=='a'?0:1;}
