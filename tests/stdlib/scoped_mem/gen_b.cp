/* TU B: a second generation must not collide with TU A's. */
#include "cplus/cplus.h"
#include "stdlib/datastack.hp"

extern stdlib_datastack g_a;
extern stdlib_datastack g_b;

int gen_b(void) {
    error e = error();
    if (stdlib_datastack_init(&g_b, 1024u, &e) != STDLIB_OK) return 1;
    return g_b.generation == g_a.generation ? 1 : 0;
}
