/* Platform-backed floating-point environment wrappers (C+ libc slice).
 * Delegate to the installed system libc/m; logged per C+ convention. */
#include "../../log/log.h"
#include <fenv.h>

int cplus_feclearexcept(int excepts){ cplus_libc_log("feclearexcept","enter"); return feclearexcept(excepts); }
int cplus_fetestexcept(int excepts){ cplus_libc_log("fetestexcept","enter"); return fetestexcept(excepts); }
int cplus_fegetround(void){ cplus_libc_log("fegetround","enter"); return fegetround(); }
int cplus_fesetround(int round){ cplus_libc_log("fesetround","enter"); return fesetround(round); }
int cplus_fegetenv(fenv_t *envp){ cplus_libc_log("fegetenv","enter"); return fegetenv(envp); }
int cplus_fesetenv(const fenv_t *envp){ cplus_libc_log("fesetenv","enter"); return fesetenv(envp); }
