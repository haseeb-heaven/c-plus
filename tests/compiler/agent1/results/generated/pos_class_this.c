#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
#include "cplus/cplus.h"

struct Box {
int value;
};
typedef struct Box Box;
void Box_drop(Box *self);
#line 6 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
Box Box_ctor(int v) {
Box _cplus_self = {0};
#line 6 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ _cplus_self.value = v; }
return _cplus_self;
Box_drop(&_cplus_self); 
#line 15 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
}
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
void Box_add(Box *self, int n)
#line 8 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ self->value += n; }
#line 10 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
int Box_get(Box *self)
#line 10 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ return self->value; }
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
int Box_doubled(Box *self)
#line 12 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ return Box_get(self) * 2; }
#line 14 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
void Box_drop(Box *self)
#line 14 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
{ print("drop", self->value); }
#line 17 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"


int main(void) {
    Box b = Box_ctor(10);
    Box_add(&b, 5);
    print("v", Box_get(&b), "d", Box_doubled(&b));
    { __typeof__(0) __cplus_ret = (0); Box_drop(&b);  return __cplus_ret;; }
Box_drop(&b); 
#line 40 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/class_this.cp"
}