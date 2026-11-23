#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/state_class.c"
#include "cplus/cplus.h"

struct State {
int value;
};
typedef struct State State;
#line 6 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/state_class.c"
void State_add(State *self, int n)
#line 6 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/state_class.c"
{
        self->value += n;
    }
#line 10 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/state_class.c"
int State_get(State *self)
#line 10 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/state_class.c"
{
        return self->value;
    }
#line 15 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/state_class.c"


int main(void) {
    __typeof__( (State){0}) state = (State){0};
    State_add(&state, 2);
    State_add(&state, 3);
    print("state=", State_get(&state));
    return 0;
}
