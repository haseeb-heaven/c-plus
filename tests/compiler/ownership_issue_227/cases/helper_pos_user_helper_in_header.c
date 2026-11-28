/* Control: widening to !isInSystemHeader must not ban all cplus_* names. */
#include "user_helper.h"
int main(void) { return cplus_core_helper(1) == 2 ? 0 : 1; }
