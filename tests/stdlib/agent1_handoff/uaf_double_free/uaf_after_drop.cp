/* P0 reproducer: use-after-free of a stdlib class-owned buffer.
 *
 * EXPECT: REJECT with a tagged C+ diagnostic, e.g.
 *   [CPLUS-LIFETIME] [CPLUS-SAFE-LIFETIME] use-after-free: ... 's'
 *
 * string::drop() releases the buffer owned by `s`.  The following s.at(0)
 * is a read through that freed buffer.  The library's at() guards the read
 * (it returns '\0' when chars == NULL), so this test never dereferences
 * stale memory itself; the invalid operation is expressed purely through
 * the standard-library API and must be caught by C+ compile-time analysis.
 *
 * OBSERVED with the current build/c+: ACCEPT (capability gap).  See NOTES.md.
 */
#include "stdlib/string.hp"

int main(void) {
    string s = string("abc");
    s.drop();
    print(s.at(0));
    return 0;
}
