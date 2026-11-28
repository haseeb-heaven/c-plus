/* Demo: owning string with growth, splitting and a borrowed view. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/string.hp"

int main(void) {
    string line = string("apples,pears,plums");
    print("size", line.size(), "capacity", line.capacity());

    string greeting = string("hello");
    greeting.append(", world");
    greeting.append("!");
    print("built", greeting.c_str(), greeting.find("world"),
          greeting.equals("hello, world!"));

    size_t fields = line.count_fields(',');
    char field[16];
    print("fields", fields);
    int i;
    for (i = 0; i < 3; i++) {
        line.field(',', (size_t)i, field, sizeof(field));
        print(" field", i, field);
    }

    string_view view = line.to_view();
    print("view", view.size(), view.data()[0],
          view.equals("apples,pears,plums"));

    char slice[8];
    greeting.substr(0, 5, slice, sizeof(slice));
    print("slice", slice);
    greeting.clear();
    print("cleared", greeting.size(), greeting.c_str()[0] == '\0');
    return 0;
}
