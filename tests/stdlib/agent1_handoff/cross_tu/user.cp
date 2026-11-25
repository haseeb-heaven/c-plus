/* P1 cross-TU reproducer, case B -- using translation unit.
 *
 * release_shared_resource() is defined in dropper.cp; the only knowledge this
 * TU has is the extern declaration in shared.hp.  The subscript therefore
 * reads a released pointer, but nothing in this TU records the release.
 */
#include <stdio.h>
#include "shared.hp"

int main(void) {
    release_shared_resource();
    return ((char *)shared_resource)[0]; /* use-after-free across TU boundary */
}
