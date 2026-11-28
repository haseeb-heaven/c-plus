// EXPECT: REJECT
// Reason: releasing the same String buffer twice is a double free.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("hello");
    void *buf = (void *)s.c_str();
    /* Two explicit releases of the buffer owned by this String. */
    cplus_release(buf);
    cplus_release(buf);
    return 0;
}
