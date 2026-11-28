/* Demo L: class constructor and destructor integrated with RAII.
 *
 * The constructor lowers to Point_ctor(), the destructor to Point_drop(),
 * and the compiler calls Point_drop(&p) when p leaves scope. */
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
        print("drop point", self->sum());
    }
};

int main(void) {
    Point p = Point(4, 6);
    print("total", p.sum());
    return 0;
}
