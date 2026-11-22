/* Bug 666: sizeof(pointer) used as a buffer length for memset. */
#include <stdlib.h>
#include <string.h>

int main(void) {
    unsigned char *block = (unsigned char *)malloc(256);
    memset(block, 0, sizeof(block));
    free(block);
    return 0;
}
