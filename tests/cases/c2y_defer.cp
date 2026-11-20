#include "cplus/cplus.h"

static int process(int early) {
    print("open");
    defer print("close");
    print("alloc");
    defer print("free");
    if (early) return -1;
    print("work");
    return 0;
}

int main(void) {
    int a = process(0);
    print("a", a);
    int b = process(1);
    print("b", b);
    return 0;
}
