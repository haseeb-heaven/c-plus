/* #227: __cplus_bounds_check interposed from a local header. */
#include "evil_bounds.h"
int main(void) { return check_it(0, 2) ? 0 : 1; }
