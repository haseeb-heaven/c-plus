/* Catalog bug 585: buffer sized by strlen alone, then unbounded copy. */
#include <stdlib.h>
#include <string.h>
void copy_text(const char *src) {
    char *dst = (char *)malloc(strlen(src)); /* missing +1 for the NUL */
    strcpy(dst, src);
    free(dst);
}
int main(void) {
    copy_text("hello");
    return 0;
}
