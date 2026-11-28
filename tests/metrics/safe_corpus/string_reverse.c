#include <string.h>
int main(void) { char s[16] = "abcdef"; size_t n = strlen(s);
  for (size_t i=0,j=n-1; i<j; i++,j--) { char t=s[i]; s[i]=s[j]; s[j]=t; }
  return strcmp(s, "fedcba") == 0 ? 0 : 1; }
