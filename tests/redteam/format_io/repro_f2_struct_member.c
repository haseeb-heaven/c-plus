/*
 * FINDING F2 -- destination-capacity check bypass for struct members.
 *
 * Contract (stdlib/safe/safe_format.h): sprintf_s/snprintf_s "refuse dmax
 * exceeding known heap/object capacity" and "never write past dmax bytes".
 *
 * The unprefixed macro snprintf_s() passes __builtin_object_size(dest, 0) as
 * known_dmax. For a member of a struct, type-0 object size is the size of the
 * *enclosing struct*, not the member. A 16-byte write into an 8-byte member of
 * a 16-byte struct therefore passes the check and corrupts the adjacent field.
 *
 * Build + run (from repo root):
 *   /usr/bin/clang -std=c99 -O1 -g -Istdlib/safe -Ilibc/install-include \
 *     -Ithird_party/safeclib/include \
 *     tests/redteam/format_io/repro_f2_struct_member.c \
 *     build/safe/libcplus_safecrt.a build/libc/libcplus_hardened.a \
 *     -lm -pthread -o /tmp/repro_f2 && /tmp/repro_f2
 *
 * Expected (vulnerable): rc == 16, canary overwritten ("89AB" visible).
 * Expected (fixed):      rc == -1, errno == EINVAL, canary intact.
 */
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include "safe_format.h"
#include "safe_runtime.h"

struct S { char a[8]; char canary[8]; };

int main(void) {
    struct S s;
    memset(&s, 0x5A, sizeof s);
    printf("object_size(s.a,0)=%zu (should be 8, is 16)\n",
           (size_t)__builtin_object_size(s.a, 0));
    cplus_runtime_set_violation_trap(0);
    errno = 0;
    int rc = snprintf_s(s.a, 16, "%s", "0123456789ABCDE");
    printf("rc=%d errno=%d a=[%s] canary=[%s] corrupted=%d\n",
           rc, errno, s.a, s.canary,
           (unsigned char)s.canary[0] != 0x5A);
    return 0;
}
