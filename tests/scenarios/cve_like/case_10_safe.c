/* BASELINE: case_10.c crashes; SAFE: this file reports a defined refusal. */
/* Layer: default = SafeCRT ordinary C names (stdlib/safe/safe_c.h).
 *        -DPLAIN_LIBC = standalone hardened libc (cplus_libc_safe.h).
 * Set cplus_runtime_set_violation_trap(0) under SafeCRT so the capacity
 * refusal below is returned as NULL/ERANGE rather than _exit(70). */
#include <stdint.h>
#include <stddef.h>
#include <errno.h>
#ifdef PLAIN_LIBC
#include "cplus_libc_safe.h"
#else
#include "stdlib/safe/safe_c.h"
#include "safe_runtime.h"
#endif

#define HEADER_SIZE 24

int main(void) {
    uint32_t total = 0;

#ifdef CPLUS_SAFE_C_ENABLE
    cplus_runtime_set_violation_trap(0);
#endif

    printf("len: ");
    if (scanf("%u", &total) != 1) {
        printf("case_10: refused (no valid length)\n");
        return 0;
    }

    /* Caller check the raw C version omits: reject a packet length below
     * the header size BEFORE the subtraction.  In the original, 8 - 24
     * wraps to 4294967280 and is handed to memcpy as a byte count. */
    if (total < HEADER_SIZE) {
        printf("case_10: refused (length below header size)\n");
        return 0;
    }
    uint32_t payload = total - HEADER_SIZE;

    char *raw = calloc(128, 1);
    char *out = malloc(64);
    if (!raw || !out) {
        printf("case_10: refused (allocation failure)\n");
        free(out);
        free(raw);
        return 0;
    }

    /* The write still goes through an ordinary hardened memcpy.  `out` is a
     * tracked 64-byte block and `raw + HEADER_SIZE` is an interior pointer
     * of a tracked 128-byte block.  A payload larger than the destination
     * capacity is refused (ERANGE/EINVAL) with no write, not a crash. */
    errno = 0;
    memcpy(out, raw + HEADER_SIZE, payload);
    if (errno == ERANGE || errno == EINVAL) {
        printf("case_10: refused (capacity check on oversized payload)\n");
        free(out);
        free(raw);
        return 0;
    }

    printf("case_10: ok (payload=%u)\n", payload);
    free(out);
    free(raw);
    return 0;
}
