/* All C+ standard-library .hp headers must be usable from a .cp source. */
#include "cplus/std/string.hp"
#include "cplus/std/vector.hp"
#include "cplus/std/file.hp"
#include "cplus/std/thread.hp"
#include "cplus/std/io.hp"
#include "cplus/std/net.hp"

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
