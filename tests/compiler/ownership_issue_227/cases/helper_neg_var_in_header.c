/* #227: reserved VarDecl in a local header must be reported too. */
#include "evil_var.h"
int main(void) { return __cplus_inline_state == 7 ? 0 : 1; }
