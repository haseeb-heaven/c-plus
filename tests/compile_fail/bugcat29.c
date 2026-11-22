/* Catalog bug 560: overlapping memcpy (direct, provable form). */
#include <stdio.h>
#include <string.h>
int main(void) {
    char buf[32] = "abcdefghijklmnopqrstuvwxyz";
    memcpy(buf + 2, buf, 8); /* ranges overlap: undefined behaviour */
    printf("%s\n", buf);
    return 0;
}
