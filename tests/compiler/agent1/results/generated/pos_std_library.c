#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/std_library.cp"
/* All C+ standard-library .hp headers must be usable from a .cp source. */
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-1197-0.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-1197-1.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-1197-2.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-1197-3.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-1197-4.h"
#include "/var/folders/q0/rj0k1xgn28x14thfz4gnbm5c0000gn/T/cplus-hp-1197-5.h"

int main(void) {
    String s = String_ctor("hello");
    print("s", String_len(&s), String_at(&s, 1));

    IntVec v = IntVec_ctor();
    IntVec_push(&v, 3);
    IntVec_push(&v, 5);
    print("v", IntVec_len(&v), IntVec_at(&v, 1));

    Mutex m = Mutex_new();
    {
        Guard g = Guard_lock(&m);
        println();
    Guard_drop(&g); 
#line 23 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/std_library.cp"
}
    { __typeof__(0) __cplus_ret = (0); Mutex_drop(&m); IntVec_drop(&v); String_drop(&s);  return __cplus_ret;; }
Mutex_drop(&m); IntVec_drop(&v); String_drop(&s); 
#line 25 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/cases/std_library.cp"
}