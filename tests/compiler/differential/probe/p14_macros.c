#include <stdio.h>

#define SQR(x) ((x) * (x))
#define STR(x) #x
#define CAT(a, b) a##b
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define DOUBLE(x) ((x) + (x))

int main(void) {
    int v = 5;
    printf("sqr=%d\n", SQR(v + 1));
    printf("str=%s\n", STR(hello_world));

    int CAT(fo, o) = 7;
    printf("foo=%d\n", foo);

    printf("max=%d\n", MAX(3, 9));
    printf("double=%d\n", DOUBLE(4));
    return 0;
}
