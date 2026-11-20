#include <stdio.h>
int work(int early) {
    defer puts("outer-1");
    defer puts("outer-2");
    {
        defer puts("inner-1");
        defer puts("inner-2");
        if (early) return 31;
    }
    return 9;
}
int main(void) { int x=work(1); printf("result=%d\n",x); return 0; }
