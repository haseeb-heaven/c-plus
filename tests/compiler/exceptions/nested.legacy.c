#line 1 "/private/tmp/resolve3/tests/compiler/exceptions/nested.cp"
#include "cplus/cplus.h"
int main(void){ try { try { throw 42; } catch (Exception e) { print("inner", e.code); }
 print("middle"); } catch (Exception e) { print("outer"); } print("end"); return 0; }
