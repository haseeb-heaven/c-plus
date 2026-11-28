/* Multi-TU regression for C4: generation counters must be shared across TUs.
 * TU A and TU B each initialise a data stack; their generations must differ. */
#include "cplus/cplus.h"
#include "stdlib/datastack.hp"

stdlib_datastack g_a;
stdlib_datastack g_b;

int gen_a(void);
int gen_b(void);

int main(void) {
    int fails = 0;
    error e = error();
    stdlib_set_error_callback((stdlib_error_callback)0);
    fails += gen_a();
    fails += gen_b();
    stdlib_datastack_destroy(&g_a, &e);
    stdlib_datastack_destroy(&g_b, &e);
    print("GEN_TEST", fails);
    return fails ? 1 : 0;
}
