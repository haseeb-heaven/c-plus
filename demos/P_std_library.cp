/* Demo P: the C+ standard library is a set of .hp wrappers over libc/POSIX.
 *
 * String, IntVec, File, Mutex/Guard and sockets are all RAII types lowered
 * from .hp headers; nothing here reimplements libc. */
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/file.hp"
#include "stdlib/thread.hp"
#include "stdlib/io.hp"

int main(void) {
    String name = String("C+");
    IntVec scores = IntVec();
    scores.push(10);
    scores.push(20);

    print("language", name.c_str(), "scores", scores.len(), scores.at(1));

    Mutex mutex = Mutex_new();
    {
        Guard guard = Guard_lock(&mutex);
        println();
    }
    return 0;
}
