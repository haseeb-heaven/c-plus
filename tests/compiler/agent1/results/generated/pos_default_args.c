#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/default_args.c"
#include "cplus/cplus.h"

int *get_data(size_t size ) {
    static int data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    if (size == 0)
        size = 1;
    return data;
}

int add(int a,  int b ,  int c ) {
    return a + b + c;
}

int main(void) {
    int *p = get_data(0);
    print("first", p[0], "add", add(1, 2, 3), add(1, 5, 3), add(1, 5, 9));
    return 0;
}