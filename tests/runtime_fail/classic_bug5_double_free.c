/* Bug 5: double free.  C+ managed release detects it deterministically. */
#include "cplus/cplus.h"
int main(void) {
    int *data = cplus_alloc(sizeof(int) * 10);
    if (!data) return 1;
    cplus_release(data);
    cplus_release(data);
    return 0;
}
