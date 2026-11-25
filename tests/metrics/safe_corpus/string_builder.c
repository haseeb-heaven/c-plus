#include <string.h>
int main(void){ char out[32]; size_t k=0; const char *words[3]={"a","bb","ccc"};
  for(int i=0;i<3;i++){ size_t n=strlen(words[i]); memcpy(out+k,words[i],n); k+=n; out[k]=' '; k++; }
  out[k]='\0'; return k==8?0:1; }
