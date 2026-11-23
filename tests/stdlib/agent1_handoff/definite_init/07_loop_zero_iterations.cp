// EXPECT: REJECT
// A loop may execute zero times (argc == 1).  The only write to the scalar is
// inside the loop body, so after the loop the value is indeterminate on the
// zero-iteration path.  Definite initialization is path-sensitive here.
#include "cplus/cplus.h"

int main(int argc, char **argv) {
    int last;
    for (int i = 0; i < argc - 1; i++)
        last = i;
    return last; /* indeterminate when the loop runs zero times */
}
