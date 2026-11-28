/* Consumer: <string.h> from C++17 via the same global C spellings. */
#include <string.h>
int stdc_string_probe()
{
    char buf[8];
    size_t n;
    memset(buf, 0, sizeof buf);
    n = strlen("abc");
    (void)memcpy(buf, "xy", 3);
    return (int)n + buf[0];
}
