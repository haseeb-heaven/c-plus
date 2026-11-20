#include "cplus/cplus.h"

int *get_data(size_t size = 0) {
    static int data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    if (size == 0)
        size = 1;
    return data;
}

int add(int a, int b = 2, int c = 3) {
    return a + b + c;
}

int main(void) {
    int *p = get_data();
    print("first", p[0], "add", add(1), add(1, 5), add(1, 5, 9));
    return 0;
}
