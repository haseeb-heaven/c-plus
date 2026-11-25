#include <stddef.h>
int main(void) { char src[8]="hello"; char dst[8]; size_t i=0;
  while ((dst[i]=src[i]) != '\0') i++; return dst[4]=='o' ? 0 : 1; }
