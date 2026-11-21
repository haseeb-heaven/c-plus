/* Demo P: the C+ standard library is a set of .hp wrappers over libc/POSIX.
 *
 * string, vector, file, mutex/guard and sockets are all RAII types lowered
 * from .hp headers; nothing here reimplements libc. */
#include "stdlib/string.hp"
#include "stdlib/vector.hp"
#include "stdlib/file.hp"
#include "stdlib/thread.hp"
#include "stdlib/io.hp"

int main(void) {
    string name = string("C+");
    vector scores = vector();
    scores.push_back(10);
    scores.push_back(20);

    print("language", name.c_str(), "scores", scores.size(), scores.at(1));

    mutex lock = mutex();
    {
        guard held = guard(&lock);
        println();
    }
    return 0;
}
