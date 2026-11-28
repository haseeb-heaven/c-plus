#include <errno.h>
static __thread int cplus_errno_value = 0;
int *__cplus_errno_location(void) { return &cplus_errno_value; }
/* provide errno-compatible macro through install header; internal code uses __cplus_errno_location */
