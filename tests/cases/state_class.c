#include "cplus/cplus.h"

class State {
    int value;

    void add(int n) {
        self->value += n;
    }

    int get(void) {
        return self->value;
    }
};

int main(void) {
    auto state = (State){0};
    state.add(2);
    state.add(3);
    print("state=", state.get());
    return 0;
}
