/* Demo M: C++-style default arguments lowered to explicit arguments.
 *
 * get_data() becomes get_data(0); add(1) becomes add(1, 2, 3). */
#include "cplus/cplus.h"

int *get_data(size_t size = 0) {
    static int data[4] = {10, 20, 30, 40};
    if (size == 0)
        size = 1;
    return data;
}

int sum(int a, int b = 2, int c = 3) {
    return a + b + c;
}

int main(void) {
    int *values = get_data();
    print("first", values[0], "default", sum(1), "partial", sum(1, 5));
    return 0;
}
