#line 1 "/Users/haseeb-mir/Documents/Code/Cpp/c-plus-core/tests/compiler/exceptions/nested.cp"
#include "cplus/cplus.h"
int main(void){ try { try { throw 42; } catch (Exception e) { print("inner", e.code); }
 print("middle"); } catch (Exception e) { print("outer"); } print("end"); return 0; }
