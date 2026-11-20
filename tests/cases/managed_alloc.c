#include <stdio.h>
#include <string.h>
#include "cplus/cplus.h"

typedef struct Buffer {
    int size;
    char name[16];
} Buffer;

int main(void) {
    managed Buffer *b = cplus_new(Buffer);
    b->size = 1024;
    strcpy(b->name, "buffer");
    printf("%d %s\n", b->size, b->name);
    return 0;
}
