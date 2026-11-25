#include "../../log/log.h"
#include <stdlib.h>
#include <string.h>

int cplus_rand(void){ cplus_libc_log("rand","enter"); return rand(); }
void cplus_srand(unsigned int seed){ cplus_libc_log("srand","enter"); srand(seed); }
void cplus_abort(void){ cplus_libc_log("abort","enter"); abort(); }
int cplus_atexit(void (*func)(void)){ cplus_libc_log("atexit","enter"); return atexit(func); }
void cplus_exit(int status){ cplus_libc_log("exit","enter"); exit(status); }
char *cplus_getenv(const char *name){ cplus_libc_log("getenv","enter"); return getenv(name); }
int cplus_system(const char *cmd){ cplus_libc_log("system","enter"); return system(cmd); }
