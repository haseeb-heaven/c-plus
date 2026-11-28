/* Platform-backed floating-point environment wrappers (C+ libc slice).
 * Delegate to the installed system libc/m; logged per C+ convention.
 *
 * Hardening (F5): a required fenv_t* parameter that the host would dereference
 * is rejected with errno = EINVAL and -1 when NULL, matching fenv_extra.c.
 * Valid-input behaviour is passed through to the host unchanged. */
#include "../../log/log.h"
#include <errno.h>
#include <fenv.h>

int cplus_feclearexcept(int excepts){ cplus_libc_log("feclearexcept","enter"); return feclearexcept(excepts); }
int cplus_fetestexcept(int excepts){ cplus_libc_log("fetestexcept","enter"); return fetestexcept(excepts); }
int cplus_fegetround(void){ cplus_libc_log("fegetround","enter"); return fegetround(); }
int cplus_fesetround(int round){ cplus_libc_log("fesetround","enter"); return fesetround(round); }
int cplus_fegetenv(fenv_t *envp){
    cplus_libc_log("fegetenv","enter");
    if (!envp) { errno = EINVAL; return -1; }
    return fegetenv(envp);
}
int cplus_fesetenv(const fenv_t *envp){
    cplus_libc_log("fesetenv","enter");
    if (!envp) { errno = EINVAL; return -1; }
    return fesetenv(envp);
}
