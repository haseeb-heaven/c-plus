/* TU B: includes thread.hp + io.hp, the same headers as TU A. */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/io.hp"
#include "stdlib/thread.hp"

int part_b(void) {
    Mutex mutex = Mutex_new();
    int i;
    int total = 0;
    if (!Mutex_valid(&mutex))
        return 0;
    for (i = 0; i < 3; i++) {
        Guard guard = Guard_lock(&mutex);
        if (Guard_holds(&guard))
            total++;
    }
    return total;
}
