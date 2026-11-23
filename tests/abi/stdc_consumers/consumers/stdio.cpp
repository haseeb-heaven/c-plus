/* Consumer: <stdio.h> from C++17 via the same global C spellings. */
#include <stdio.h>
int stdc_stdio_probe()
{
    char buf[8];
    int n = snprintf(buf, sizeof buf, "%d", 42);
    return n + (fputs(buf, stdout) != EOF);
}
