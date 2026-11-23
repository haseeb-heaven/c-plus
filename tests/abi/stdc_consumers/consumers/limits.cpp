/* Consumer: <limits.h> integer representation limits from C++17. */
#include <limits.h>
int stdc_limits_probe()
{
    return (CHAR_BIT == 8) + (INT_MAX > 0) + (UINT_MAX > 0) + (LONG_MAX > INT_MAX);
}
