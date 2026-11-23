// EXPECT: REJECT
// Cross-function lifetime (P0): the callee store_ptr() has no lifetime
// annotation, so the caller's `&local` is written into a longer-lived sink.
// After produce() returns, *sink refers to a dead stack slot.  Detecting this
// requires an interprocedural summary of store_ptr (parameter is captured).
#include "cplus/cplus.h"

static int *sink;

static void store_ptr(int *p) {
    sink = p;                  /* callee captures an unannotated parameter */
}

static void produce(void) {
    int local = 7;
    store_ptr(&local);         /* address of a stack local escapes the callee */
}

int main(void) {
    produce();
    return *sink;              /* reads through the escaped pointer */
}
