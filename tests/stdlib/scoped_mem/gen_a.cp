/* TU A: first data-stack generation. */
#include "cplus/cplus.h"
#include "stdlib/datastack.hp"

extern stdlib_datastack g_a;

int gen_a(void) {
    error e = error();
    if (stdlib_datastack_init(&g_a, 1024u, &e) != STDLIB_OK) return 1;
    return 0;
}
