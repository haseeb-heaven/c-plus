/* Demo: growable vector with checked access and explicit results. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"

int main(void) {
    vector values = vector();
    int i;
    for (i = 0; i < 10; i++) {
        if (values.push_back(i * i) != 0)
            return 1;
    }
    print("size", values.size(), "capacity", values.capacity());

    int sum = 0;
    for (i = 0; i < 10; i++)
        sum += values.at((size_t)i);
    print("sum", sum);

    int out = 0;
    if (values.at_checked(9, &out) == 0)
        print("at(9)", out);
    if (values.at_checked(99, &out) != 0)
        print("at(99) rejected, buffer kept", out);

    int last = 0;
    int popped = 0;
    while (values.pop_back(&last) == 0)
        popped++;
    print("drained", values.size(), "popped", popped, "last", last,
          "push-again", values.push_back(7));
    return 0;
}
