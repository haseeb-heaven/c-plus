#include "cplus/cplus.h"

class Box {
    int value;

    Box(int v) { this->value = v; }

    void add(int n) { this->value += n; }

    int get(void) { return this->value; }

    int doubled(void) { return this->get() * 2; }

    void drop(void) { print("drop", this->value); }
};

int main(void) {
    Box b = Box(10);
    b.add(5);
    print("v", b.get(), "d", b.doubled());
    return 0;
}
