#include <stdio.h>
#define TWICE(x) do { (x); (x); } while (0)
int main(void){ int n=1; TWICE(n++); printf("%d\n", n); return 0; }
