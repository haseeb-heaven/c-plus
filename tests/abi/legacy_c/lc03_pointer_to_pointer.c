/* lc03: pointer-to-pointer -- rebinding a pointer through int** and walking
 * an array through char**.
 */
#include <stdio.h>

int main(void)
{
    int a = 3;
    int b = 7;
    int *pa = &a;
    int **ppa = &pa;
    const char *names[2] = {"alpha", "beta"};
    const char **np = names;
    int ok = 1;

    ok &= (**ppa == 3);
    *ppa = &b;
    ok &= (pa == &b && **ppa == 7);
    **ppa += 5;
    ok &= (b == 12 && a == 3);
    ok &= (np[0][0] == 'a' && np[1][0] == 'b');
    ++np;
    ok &= ((*np)[0] == 'b');

    printf("lc03_pointer_to_pointer: %s a=%d b=%d tail=%c\n",
           ok ? "OK" : "FAIL", a, b, (*np)[0]);
    return ok ? 0 : 1;
}
