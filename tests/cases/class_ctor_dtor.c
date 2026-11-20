#include "cplus/cplus.h"

class Point {
    int x;
    int y;

    Point(int a, int b) {
        self->x = a;
        self->y = b;
    }

    int sum(void) {
        return self->x + self->y;
    }

    void drop(void) {
        print("drop", self->sum());
    }
};

int main(void) {
    Point p = Point(2, 3);
    print("sum=", p.sum());
    return 0;
}
