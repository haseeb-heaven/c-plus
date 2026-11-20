/* Bug 10: overwriting a pointer that still owns an allocation. */
#include <stdlib.h>
int main(void) {
    char *buf = malloc(100);
    if (!buf) return 1;
    buf = malloc(200);
    free(buf);
    return 0;
}
