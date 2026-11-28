#include "cplus/cplus.h"
class State {
    int count;
    int total;
    void add(int value) { this->count += 1; this->total += value; }
    int average(void) { return this->count ? this->total / this->count : 0; }
};
int main(void) {
    auto state = (State){0};
    State state_vec[] = {state};
    state.add(4);
    state.add(8);
    state.add(12);
    return state_vec[0].average() == 0 && state.average() == 8 ? 0 : 1;
}
