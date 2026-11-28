/* Demo K: a class that keeps state, created with `auto`.
 *
 * Methods lower to State_add(&state, n) and State_get(&state); the object is
 * an ordinary C struct, so there is no hidden allocation or dispatch cost. */
#include "cplus/cplus.h"

class State {
    int count;
    int total;

    void add(int value) {
        self->count += 1;
        self->total += value;
    }

    int average(void) {
        return self->count ? self->total / self->count : 0;
    }
};

int main(void) {
    auto state = (State){0};
    state.add(4);
    state.add(8);
    state.add(12);
    print("count=", state.count, "average=", state.average());
    return 0;
}
