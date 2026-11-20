#include "cplus/cplus.h"

void mark(int id) {
    print("DEFER", id);
}

int test_defer(int early) {
    defer mark(1);
    {
        defer mark(2);
        if (early) return 7;
    }
    return 5;
}

int main(void) {
    int first = test_defer(1);
    print("RESULT", first);
    int second = test_defer(0);
    print("RESULT", second);
    return 0;
}
