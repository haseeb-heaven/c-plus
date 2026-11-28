#include <string.h>
int main(void){ char dst[16]={0}; strncpy(dst,"abc",sizeof dst-1); return dst[2]=='c'?0:1; }
