/* #227: reserved guard defined in a local header, not the main file.
   The __cplus_ string also keeps the driver's textual pre-filter active. */
#include "evil_guard.h"
int main(void) { return guarded_call(1); }
