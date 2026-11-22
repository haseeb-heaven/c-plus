/* Catalog bug 570: strncpy length larger than the destination. */
#include <string.h>
int main(void) {
    char dst[8];
    strncpy(dst, "source", 64); /* dst has only 8 bytes */
    return 0;
}
