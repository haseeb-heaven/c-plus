/* Consumer: <stddef.h> size_t, NULL and offsetof from C++17. */
#include <stddef.h>
struct stdc_stddef_sample { char c; int i; };
int stdc_stddef_probe()
{
    size_t off = offsetof(struct stdc_stddef_sample, i);
    void *p = NULL;
    return (int)off + (p == NULL);
}
