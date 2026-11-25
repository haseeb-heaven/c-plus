/* C+ unified exception handling: functional tests over vendored exceptions4c.
 * Covers: basic throw/catch, CATCH_ALL, FINALLY cleanup, nesting, RETHROW,
 * structured-error -> exception bridge, allocation-failure propagation. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "safe_except.h"
#include "safe_log.h"

static const struct e4c_exception_type OOPS      = {NULL, "Oops"};
static const struct e4c_exception_type OOM       = {NULL, "Out of memory"};
static const struct e4c_exception_type IO_FAIL   = {NULL, "I/O failure"};

static void basic_throw_catch(void) {
    volatile int caught = 0;
    TRY { THROW(OOPS, "boom %d", 7); }
    CATCH (OOPS) { caught = 1; assert(strcmp(e4c_get_exception()->name, "OOPS") == 0); }
    assert(caught == 1);
}

static void catch_all(void) {
    volatile int caught = 0;
    TRY { THROW(IO_FAIL, "x"); }
    CATCH (OOPS) { assert(0); }
    CATCH_ALL { caught = 1; }
    assert(caught == 1);
}

static void finally_cleanup(void) {
    volatile int *buf = NULL;
    volatile int finalized = 0;
    volatile int caught = 0;
    TRY {
        buf = (int *)malloc(64 * sizeof(int));
        if (!buf) { THROW(OOM, "alloc failed"); }
        THROW(OOPS, "after alloc");
    }
    CATCH (OOPS) { caught = 1; }
    FINALLY {
        free((void *)buf);
        buf = NULL;
        finalized = 1;
    }
    assert(caught == 1 && finalized == 1 && buf == NULL);
}

static void nested_try(void) {
    volatile int inner = 0, outer = 0;
    TRY {
        TRY { THROW(OOPS, "inner"); }
        CATCH (OOPS) { inner = 1; }
        THROW(IO_FAIL, "outer");
    }
    CATCH (IO_FAIL) { outer = 1; }
    assert(inner == 1 && outer == 1);
}

static void rethrow_propagates(void) {
    volatile int outer = 0;
    TRY {
        TRY { THROW(OOPS, "rethrow me"); }
        CATCH (OOPS) { RETHROW(); }
    }
    CATCH (OOPS) { outer = 1; }
    assert(outer == 1);
}

static int structured_op(int fail) {
    return fail ? 22 /* EINVAL-like */ : 0;   /* structured error by default */
}

static void structured_bridge(void) {
    volatile int caught = 0;
    TRY { CPLUS_THROW_IF(structured_op(1), OOPS, "structured status propagated"); }
    CATCH (OOPS) { caught = 1; }
    assert(caught == 1);

    volatile int ok = 0;
    TRY { CPLUS_THROW_IF(structured_op(0), OOPS, "should not throw"); ok = 1; }
    CATCH_ALL { assert(0); }
    assert(ok == 1);
}

int main(void) {
    cplus_safe_log_set_level(CPLUS_SAFE_LOG_OFF);
    cplus_except_init();

    basic_throw_catch();
    for (int i = 0; i < 10; ++i) basic_throw_catch();   /* repeat/stress */
    catch_all();
    finally_cleanup();
    nested_try();
    rethrow_propagates();
    structured_bridge();

    printf("safe_except: ok\n");
    return 0;
}
