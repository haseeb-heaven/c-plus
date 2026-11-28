/* Minimal portable shim for macOS-only <sys/syslimits.h> on Linux CI. */
#ifndef CPLUS_TEST_SYS_SYSLIMITS_H
#define CPLUS_TEST_SYS_SYSLIMITS_H
#include <limits.h>
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif
#ifndef NAME_MAX
#define NAME_MAX 255
#endif
#endif
