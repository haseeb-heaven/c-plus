#include "../../log/log.h"
#include <string.h>

char *cplus_strtok(char *str, const char *delim){ cplus_libc_log("strtok","enter"); return strtok(str,delim); }
