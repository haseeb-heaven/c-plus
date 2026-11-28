#include "../../log/log.h"
#include <string.h>
#include <errno.h>

char *cplus_strerror(int errnum){ cplus_libc_log("strerror","enter"); return strerror(errnum); }
