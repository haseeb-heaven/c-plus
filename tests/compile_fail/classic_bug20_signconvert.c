/* Bug 20: signed-to-unsigned comparison. */
#include <stdio.h>
int main(void) {
    int index = -1;
    size_t length = 10;
    if (index < length) printf("in\n");
    return 0;
}
