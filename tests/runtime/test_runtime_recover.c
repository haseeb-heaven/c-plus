/* Proves a runtime out-of-bounds is reported and unwound, not aborted. */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "safe_runtime.h"
#include "safe_log.h"

static int timed_out_guard = 0;

/* The bug: read one past a 4-element array (adjacent OOB). */
static int oob_body(void *ctx) {
    (void)ctx;
    int arr[4] = {55, 66, 77, 44};
    volatile int sink = 0;
    int bad = arr[4];       /* out of bounds: detected at runtime */
    sink = bad;
    (void)sink;
    return 0;               /* would be reached only if not detected */
}

/* A correct body must complete normally. */
static int ok_body(void *ctx) {
    (void)ctx;
    int arr[4] = {1, 2, 3, 4};
    volatile int sink = 0;
    sink = arr[3];
    (void)sink;
    return 0;
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_runtime_set_handler(NULL);

    int rc = cplus_runtime_guard(oob_body, NULL);
    assert(rc == CPLUS_RUNTIME_ERROR);   /* detected + unwound, no abort */
    (void)timed_out_guard;

    rc = cplus_runtime_guard(ok_body, NULL);
    assert(rc == CPLUS_RUNTIME_OK);      /* clean path unaffected */

    printf("runtime_recover: ok\n");
    return 0;
}
