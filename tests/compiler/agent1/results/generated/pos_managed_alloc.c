#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/managed_alloc.c"
#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"

typedef struct Buffer {
    int size;
    char name[16];
} Buffer;

int main(void) {
     Buffer *b = cplus_new(Buffer); 
    b->size = 1024;
    strcpy(b->name, "buffer");
    printf("%d %s\n", b->size, b->name);
    { __typeof__(0) __cplus_ret = (0); cplus_release(b); b = NULL;  return __cplus_ret;; }
cplus_release(b); b = NULL; 
#line 17 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/managed_alloc.c"
}