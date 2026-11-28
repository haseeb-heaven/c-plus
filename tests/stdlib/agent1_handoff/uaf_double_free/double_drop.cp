/* P0 reproducer: double release / double drop of a stdlib class-owned buffer.
 *
 * EXPECT: REJECT with a tagged C+ diagnostic, e.g.
 *   [CPLUS-OWNER] double free of 's'
 *
 * string::drop() releases the owned buffer exactly once; the second call
 * releases the same owner again.  In this stdlib implementation the second
 * drop() is a documented no-op (chars == NULL), so the program is safe to
 * build and run if the compiler fails to reject it.  The double release is
 * expressed through the public stdlib API, not by a raw pointer operation.
 *
 * OBSERVED with the current build/c+: ACCEPT (capability gap).  See NOTES.md.
 */
#include "stdlib/string.hp"

int main(void) {
    string s = string("abc");
    s.drop();
    s.drop(); /* second release of the same owner */
    return 0;
}
