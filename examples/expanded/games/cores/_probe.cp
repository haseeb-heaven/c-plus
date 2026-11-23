/* temporary probe */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/vector.hp"
#include "stdlib/string.hp"

class Cell {
    int v;
    Cell(int x) { this->v = x; }
    int get(void) { return this->v; }
    void add(int d) { this->v += d; }
};

int main(int argc, char **argv) {
    if (argc < 2) { print("need arg"); return 2; }
    const char *arg1 = NULL;
    __builtin_memcpy(&arg1, argv + 1, sizeof(arg1));
    string s = string(arg1);
    vector vals = vector();
    Cell t = Cell(3);
    vals.push_back(t.get());
    vals.push_back(7);
    Cell c = Cell(5);
    c.add(vals.at(0));
    print("s=", s.c_str(), "size=", s.size(), "v=", c.get(), "sum=", vals.at(0) + vals.at(1));
    for (auto i = 0; i < vals.size(); ++i) print("  vals", i, vals.at(i));
    slice sl = vals.view();
    int got = 0;
    sl.at_checked(0, &got);
    print("slice got=", got, "size=", vals.size());
    return 0;
}
