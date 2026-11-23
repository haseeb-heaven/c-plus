#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
#include "cplus/cplus.h"

struct Point {
int x;
int y;
};
typedef struct Point Point;
void Point_drop(Point *self);
#line 7 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
Point Point_ctor(int a , int b) {
Point _cplus_self = {0};
#line 7 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
{
        _cplus_self.x = a;
        _cplus_self.y = b;
    }
return _cplus_self;
Point_drop(&_cplus_self); 
#line 19 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
}
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
int Point_sum(Point *self)
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
{
        return self->x + self->y;
    }
#line 16 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
void Point_drop(Point *self)
#line 16 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
{
        print("drop", Point_sum(self));
    }
#line 21 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"


int main(void) {
    Point p = Point_ctor(2, 3);
    print("sum=", Point_sum(&p));
    { __typeof__(0) __cplus_ret = (0); Point_drop(&p);  return __cplus_ret;; }
Point_drop(&p); 
#line 39 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_ctor_dtor.c"
}