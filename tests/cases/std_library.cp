/* All C+ standard-library .hp headers must be usable from a .cp source. */
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/file.hp"
#include "stdlib/thread.hp"
#include "stdlib/io.hp"
#include "stdlib/net.hp"

int main(void) {
    string s = string("hello");
    print("s", s.size(), s.at(1));

    vector v = vector();
    v.push_back(3);
    v.push_back(5);
    print("v", v.size(), v.at(1));

    mutex m = mutex();
    {
        guard g = guard(&m);
        println();
    }
    return 0;
}
