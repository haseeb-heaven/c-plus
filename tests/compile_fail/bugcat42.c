/* Bug 690: overlapping memcpy instead of memmove. */
#include <string.h>

int main(void) {
    char text[16] = "abcdef";
    memcpy(text + 2, text, 4);
    return 0;
}
