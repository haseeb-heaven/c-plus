/* Driver for the issue #222 cross-translation-unit metadata regression.
 * Expected output: meta-tomb-cross-tu 0 16 0 16
 * The middle two numbers are the tombstone counts observed by the *other*
 * TU; each must be 16 both before and after the original bug is fixed. */
#include <stdio.h>
#include <stdlib.h>
#include "cplus/cplus.h"
#include "stdlib/error.hp"

int tu1_phase(void);
int tu2_phase(void);
int tu1_count_tombs(void);
int tu2_count_tombs(void);

static void reset_meta(void) {
    free(stdlib_state.meta_tab);
    stdlib_state.meta_tab = (stdlib_meta_slot *)0;
    stdlib_state.meta_cap = 0u;
    stdlib_state.meta_used = 0u;
    stdlib_state.meta_live = 0u;
    stdlib_state.meta_gen = 0u;
}

int main(void) {
    int a, b, c, d;
    reset_meta();
    a = tu1_phase();
    b = tu2_count_tombs(); /* TU B must recognise TU A's 16 tombstones */
    reset_meta();
    c = tu2_phase();
    d = tu1_count_tombs(); /* TU A must recognise TU B's 16 tombstones */
    print("meta-tomb-cross-tu", a, b, c, d);
    reset_meta();
    return (a == 0 && b == 16 && c == 0 && d == 16) ? 0 : 1;
}
