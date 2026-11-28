#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/exceptions/throw_catch.cp"
#include "cplus/cplus.h"
int main(void){ try { print("before"); throw "boom"; print("after"); }
 catch (Exception ex) { print("caught", ex.code, ex.message); }
 print("done"); return 0; }
