/* Consumer: <errno.h> errno lvalue and error macros. */
#include <errno.h>
int stdc_errno_probe(void)
{
    int e;
    errno = 0;
    e = EINVAL;
    return (errno == 0) + (e != 0) + (EDOM != 0);
}
