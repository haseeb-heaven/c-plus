/* KNOWN_OPEN negative fixture -- companion to cross_tu_owner_def.c (TU A).
 *
 * Translation unit B: the CALLER side.
 *
 * Declares the SAME symbol that TU A defines as `void resource_release(int *)`,
 * but here with the incompatible signature `void resource_release(long *)`.
 * On the host LP64 C ABI `int *` and `long *` have identical representation, so
 * the call links and runs cleanly and reaches TU A's definition.  Nothing in
 * this TU or in TU A diagnoses the conflicting ownership/pointer declaration:
 * a whole-program type view does not exist in this single-TU compiler.
 *
 * Expected observed behaviour (all three modes with the prebuilt compiler):
 *   - both TUs compile with rc=0 and no "mismatch"/"incompatible" diagnostic;
 *   - the link succeeds with rc=0;
 *   - the program prints "no cross-TU diagnostic" and exits 0;
 *   - STRICT emits only the per-TU [CPLUS-MULTITU] disclosure note, which
 *     states that cross-TU control/data flow is not safety-checked.
 */
#include <stdio.h>

void resource_release(long *p); /* INCOMPATIBLE with TU A's int* definition */
int *resource_acquire(void);

int main(void)
{
    long *p = (long *)resource_acquire();
    resource_release(p); /* silently resolves to TU A's `void(int *)` version */
    printf("no cross-TU diagnostic\n");
    return 0;
}
