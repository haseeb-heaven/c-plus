/* Consumer: <stdlib.h> via ordinary ISO C spellings. */
#include <stdlib.h>
int stdc_stdlib_probe(void)
{
    void *p = malloc(8);
    int a = abs(-7);
    int allocated = (p != NULL);
    free(p);
    return a + allocated;
}
