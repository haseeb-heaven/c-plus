#include <stdio.h>
#include <stddef.h>

struct S { char a; int b; char c; };

int main(void) {
    printf("sizeof=%zu\n", sizeof(struct S));
    printf("off_a=%zu off_b=%zu off_c=%zu\n",
           offsetof(struct S, a), offsetof(struct S, b), offsetof(struct S, c));
    return 0;
}
