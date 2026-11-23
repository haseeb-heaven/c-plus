#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/null_reassigned.c"
#include <stdlib.h>
int main(void) {
    int *p = NULL;
    p = malloc(sizeof(int));
    if (!p) return 1;
    *p = 7;
    free(p);
    return 0;
}