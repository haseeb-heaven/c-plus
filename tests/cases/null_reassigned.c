#include <stdlib.h>
int main(void) {
    int *p = NULL;
    p = malloc(sizeof(int));
    if (!p) return 1;
    *p = 7;
    free(p);
    return 0;
}
