/* GAP CASE 05: raw double free.
   STRICT static expectation: rejected with a CPLUS-OWNER diagnostic. */
#include <stdlib.h>

int main(void) {
    int *p = (int *)malloc(sizeof(int));
    if (!p) return 1;
    free(p);
    free(p); /* second release of the same owner */
    return 0;
}
