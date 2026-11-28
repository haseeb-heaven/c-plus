/* Original user-supplied crash scenario 2 (minimally adjusted to compile). */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
struct Frame { int32_t length; char buffer; };
int main(void){ struct Frame *f=malloc(sizeof(struct Frame)); char *w=calloc(1024,1); if(!f||!w) return 1;
 printf("length: "); if(scanf("%d",&f->length)!=1) return 1;
 if(f->length<(int32_t)sizeof(f->buffer)) memcpy(f->buffer,w,(size_t)f->length);
 free(w); free(f); return 0; }
