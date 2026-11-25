/* BASELINE: case_02.c crashes; SAFE: this file reports a defined refusal. */
/* Layer: default = SafeCRT ordinary C names (stdlib/safe/safe_c.h).
 *        -DPLAIN_LIBC = standalone hardened libc (cplus_libc_safe.h).
 * Note: the original source passes `f->buffer` (a char) to memcpy, which does
 * not compile under C99; the intended address `&f->buffer` is used here. */
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "safe_runtime.h"
#endif

struct Frame { int32_t length; char buffer; };

static void refuse(struct Frame *f, char *w, const char *why) {
    printf("case_02: refused (%s)\n", why);
    free(w);
    free(f);
}

int main(void) {
    struct Frame *f = malloc(sizeof(struct Frame));
    char *w = calloc(1024, 1);
    if (!f || !w) {
        printf("case_02: refused (allocation failure)\n");
        free(w);
        free(f);
        return 0;
    }

#ifdef CPLUS_SAFE_C_ENABLE
    cplus_runtime_set_violation_trap(0);
#endif

    printf("length: ");
    if (scanf("%d", &f->length) != 1) {
        refuse(f, w, "no valid length");
        return 0;
    }

    /* Caller check the raw C version omits: a negative signed length passes
     * the original `length < 1` guard, then (size_t)length becomes a huge
     * unsigned count.  Reject it BEFORE any cast to size_t. */
    if (f->length < 0) {
        refuse(f, w, "negative signed length before size_t cast");
        return 0;
    }

    if (f->length < (int32_t)sizeof(f->buffer)) {
        size_t n = (size_t)f->length;  /* safe: non-negative */
        /* Ordinary hardened memcpy.  &f->buffer is an interior pointer of a
         * tracked heap block; if n ever exceeded its remaining capacity the
         * capacity check would refuse the write (ERANGE) instead of
         * corrupting the heap. */
        errno = 0;
        memcpy(&f->buffer, w, n);
        if (errno == ERANGE || errno == EINVAL) {
            refuse(f, w, "capacity check refused write");
            return 0;
        }
    }
    printf("case_02: ok (length=%d)\n", f->length);
    free(w);
    free(f);
    return 0;
}
