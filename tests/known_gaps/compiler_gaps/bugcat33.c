/* Catalog bug 595: memcpy length exceeds the source extent. */
#include <string.h>
int main(void) {
    char src[8] = {0};
    char dst[32];
    memcpy(dst, src, 64); /* source has only 8 bytes */
    return 0;
}
