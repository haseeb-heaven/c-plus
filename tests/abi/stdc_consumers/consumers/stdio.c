/* Consumer: <stdio.h> via ordinary ISO C spellings. */
#include <stdio.h>
int stdc_stdio_probe(void)
{
    char buf[8];
    int n = snprintf(buf, sizeof buf, "%d", 42);
    return n + (fputs(buf, stdout) != EOF);
}
