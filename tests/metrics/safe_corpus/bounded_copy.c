#include <string.h>
int main(void){ char dst[16]; const char *src="short"; size_t n=strlen(src)+1;
  if(n<=sizeof dst) memcpy(dst,src,n); else return 2; return dst[4]=='t'?0:1; }
