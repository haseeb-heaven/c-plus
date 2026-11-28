/* Consumer: <assert.h> assert macro. */
#include <assert.h>
int stdc_assert_probe(void)
{
    int x = 3;
    assert(x == 3);
    return x;
}
