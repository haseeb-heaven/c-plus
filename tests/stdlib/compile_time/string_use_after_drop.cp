// EXPECT: REJECT
// Reason: reading the String's managed buffer after it has been released is a use-after-free.
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int main(void) {
    string s = string("hello");
    void *buf = (void *)s.c_str();
    /* string::drop() is not statically tracked; release the owned buffer
     * directly so the C+ ownership checker can prove the violation. */
    cplus_release(buf);
    return ((const char *)buf)[0];
}
