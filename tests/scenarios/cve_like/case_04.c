/* Original user-supplied crash scenario 4 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
struct Token { const char *start; size_t len; };
int main(void){ size_t cap=4,len=0; char *arena=malloc(cap); if(!arena) return 1;
 strcpy(arena,"hdr"); len=3; struct Token tok={.start=arena,.len=3};
 size_t extra=0; printf("extra: "); if(scanf("%zu",&extra)!=1) return 1;
 char *grown=realloc(arena,cap+extra); if(!grown){free(arena);return 1;} arena=grown;
 printf("byte: %c\n",tok.start[0]); free(arena); return 0; }
