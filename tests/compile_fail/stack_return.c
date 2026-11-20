#include <stdio.h>

int *get_number(void) {
    int local_var = 100;
    return &local_var;
}

int main(void) {
    int *p = get_number();
    printf("%d\n", *p);
    return 0;
}
