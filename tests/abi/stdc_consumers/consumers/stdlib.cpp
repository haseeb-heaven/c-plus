/* Consumer: <stdlib.h> from C++17 via the same global C spellings. */
#include <stdlib.h>
int stdc_stdlib_probe()
{
    void *p = malloc(8);
    int a = abs(-7);
    int allocated = (p != NULL);
    free(p);
    return a + allocated;
}
