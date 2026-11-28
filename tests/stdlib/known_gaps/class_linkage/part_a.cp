/* KNOWN GAP reproduction: two TUs that both include a class-bearing .hp.
 *
 * Class methods are lowered without internal linkage, so a class declared in a
 * .hp produces one external definition per including translation unit and the
 * link fails with duplicate symbols.  Free functions are unaffected because
 * the library defines them `static inline`.
 *
 * Expected today: link error (duplicate symbols for string_*).
 * See docs/MODERN_STDLIB.md ("Multi-translation-unit linkage").
 */
#include <stdio.h>
#include "cplus/cplus.h"
#include "stdlib/string.hp"

int gap_a(void) {
    string s = string("aaaa");
    return (int)s.size();
}
