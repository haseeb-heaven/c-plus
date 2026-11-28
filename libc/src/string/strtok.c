/* C+ strtok: same tokenisation contract as ISO C strtok, but the hidden
 * continuation pointer is thread-local (via the reentrant cplus_strtok_r)
 * instead of the shared global state inside the host strtok. A NULL delimiter
 * is refused instead of being passed to the host, which would dereference it.
 * str == NULL remains the legal "continue previous token" call.
 */
#include "../../log/log.h"
#include "../../install-include/cplus_libc.h"
#include <errno.h>
#include <string.h>

static __thread char *cplus_strtok_save;

char *cplus_strtok(char *str, const char *delim)
{
    cplus_libc_log("strtok", "enter");
    if (delim == NULL) {
        errno = EINVAL;
        return NULL;
    }
    return cplus_strtok_r(str, delim, &cplus_strtok_save);
}
