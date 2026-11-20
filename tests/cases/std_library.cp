/* All C+ standard-library .hp headers must be usable from a .cp source. */
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/file.hp"
#include "stdlib/thread.hp"
#include "stdlib/io.hp"
#include "stdlib/net.hp"

int main(void) {
    String s = String("hello");
    print("s", s.len(), s.at(1));

    IntVec v = IntVec();
    v.push(3);
    v.push(5);
    print("v", v.len(), v.at(1));

    Mutex m = Mutex_new();
    {
        Guard g = Guard_lock(&m);
        println();
    }
    return 0;
}
