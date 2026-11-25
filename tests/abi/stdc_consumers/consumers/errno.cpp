/* Consumer: <errno.h> errno lvalue and error macros from C++17. */
#include <errno.h>
int stdc_errno_probe()
{
    int e;
    errno = 0;
    e = EINVAL;
    return (errno == 0) + (e != 0) + (EDOM != 0);
}
