/* Issue #3 acceptance: an interactive loop survives invalid input and
 * reprompts instead of aborting. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"
#include "stdlib/io.hp"

int main(void) {
    error err = error();
    int accepted = 0;
    int total = 0;

    while (accepted < 2) {
        int value = 0;
        int status = read_int(&value, &err);
        if (status == STDLIB_EOF)
            break;
        if (status != STDLIB_OK) {
            print("rejected", err.category(), err.what());
            continue;
        }
        accepted++;
        total += value;
    }
    print("accepted", accepted, "total", total);
    return accepted == 2 ? 0 : 1;
}
